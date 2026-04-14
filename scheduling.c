#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "scheduling.h"

/* -----------------------------
Output buffer — replaces printf so results display in the GUI
------------------------------ */
static char sched_buf[65536];
static int  sched_pos = 0;

void clear_sched_output(void) {
    sched_pos = 0;
    sched_buf[0] = '\0';
}

const char *get_sched_output(void) {
    return sched_buf;
}

static void sched_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int space = (int)sizeof(sched_buf) - sched_pos - 1;
    if (space > 0) {
        int n = vsnprintf(sched_buf + sched_pos, space, fmt, ap);
        if (n > 0) sched_pos += n;
    }
    va_end(ap);
}

/* -----------------------------
Helper functions for ProcessQueue
------------------------------ */
void enqueue(ProcessQueue *pq, Process *p) {
    pq->processes[pq->back++] = p;
}

Process *dequeue(ProcessQueue *pq) {
    return pq->processes[pq->front++];
}

int isEmpty(ProcessQueue *pq) {
    return pq->front == pq->back;
}

int isInQueue(ProcessQueue *pq, int targetPid, int length) {
    for (int i = pq->front; i < pq->front + length - 1; i++) {
        if (pq->processes[i]->pid == targetPid) return 1;
    }
    return 0;
}

/* -----------------------------
Read input file
------------------------------ */
int read_processes(const char *filename, Process processes[]) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Error opening file");
        return -1;
    }
    int count = 0;
    while (fscanf(fp, "%d %d %d",
            &processes[count].pid,
            &processes[count].arrival_time,
            &processes[count].burst_time) == 3) {
        processes[count].remaining_time = processes[count].burst_time;
        count++;
    }
    fclose(fp);
    return count;
}

/* -----------------------------
Reset calculated fields
------------------------------ */
void reset_processes(Process processes[], int n) {
    for (int i = 0; i < n; i++) {
        processes[i].remaining_time  = processes[i].burst_time;
        processes[i].completion_time = 0;
        processes[i].waiting_time    = 0;
        processes[i].turnaround_time = 0;
    }
}

/* -----------------------------
FCFS Scheduling
------------------------------ */
void fcfs(Process processes[], int n) {
    int current_time = 0;
    for (int i = 0; i < n; i++) {
        if (current_time < processes[i].arrival_time)
            current_time = processes[i].arrival_time;
        int start = current_time;
        current_time += processes[i].burst_time;
        print_gantt(processes[i].pid, start, current_time);
        processes[i].completion_time = current_time;
    }
}

/* -----------------------------
SJF Scheduling (Non-preemptive)
------------------------------ */
void sjf(Process processes[], int n) {
    int current_time = 0;
    for (int i = 0; i < n; i++) {
        /* Find available process with shortest burst */
        Process *shortestJob = NULL;
        int min_burst = INT_MAX;
        for (int j = 0; j < n; j++) {
            if (processes[j].arrival_time <= current_time &&
                processes[j].completion_time == 0 &&
                processes[j].burst_time < min_burst) {
                min_burst    = processes[j].burst_time;
                shortestJob  = &processes[j];
            }
        }
        /* CPU idle — advance to next arrival */
        if (!shortestJob) {
            i--;
            current_time++;
            continue;
        }
        int start = current_time;
        current_time += shortestJob->burst_time;
        int end = current_time;
        print_gantt(shortestJob->pid, start, end);
        shortestJob->completion_time = end;
        /* Update waiting times for jobs still in queue */
        for (int j = 0; j < n; j++) {
            if (processes[j].pid != shortestJob->pid &&
                processes[j].arrival_time <= current_time &&
                processes[j].completion_time == 0) {
                if (processes[j].waiting_time == 0)
                    processes[j].waiting_time += shortestJob->burst_time - processes[j].arrival_time;
                else
                    processes[j].waiting_time += shortestJob->burst_time;
            }
        }
    }
}

static int searchProcessArray(Process array[], int length, int pid) {
    for (int i = 0; i < length; i++)
        if (array[i].pid == pid) return 1;
    return 0;
}

/* -----------------------------
Round Robin Scheduling
------------------------------ */
void round_robin(Process processes[], int n, int quantum) {
    int current_time = 0;
    int completed    = 0;
    ProcessQueue *queue = malloc(sizeof(ProcessQueue));
    queue->front = 0;
    queue->back  = 0;

    /* Enqueue processes that have already arrived */
    for (int i = 0; i < n; i++) {
        if (processes[i].arrival_time <= current_time)
            enqueue(queue, &processes[i]);
    }

    while (completed < n) {
        /* CPU idle — advance time and check for arrivals */
        if (isEmpty(queue)) {
            current_time++;
            for (int i = 0; i < n; i++) {
                if (processes[i].arrival_time <= current_time &&
                    processes[i].remaining_time > 0 &&
                    !isInQueue(queue, processes[i].pid, n))
                    enqueue(queue, &processes[i]);
            }
            continue;
        }

        Process *p   = dequeue(queue);
        int exec     = (p->remaining_time > quantum) ? quantum : p->remaining_time;
        int start    = current_time;
        current_time += exec;
        print_gantt(p->pid, start, current_time);
        p->remaining_time -= exec;

        /* Enqueue processes that arrived during this quantum */
        for (int i = 0; i < n; i++) {
            if (processes[i].arrival_time <= current_time &&
                processes[i].remaining_time > 0 &&
                processes[i].pid != p->pid &&
                !isInQueue(queue, processes[i].pid, (queue->back - queue->front) + 1))
                enqueue(queue, &processes[i]);
        }

        if (p->remaining_time == 0) {
            p->completion_time = current_time;
            completed++;
        } else {
            enqueue(queue, p);
        }
    }
    free(queue);
}

/* -----------------------------
Print Metrics
------------------------------ */
void print_metrics(Process processes[], int n) {
    double total_wait = 0, total_turnaround = 0;
    sched_printf("\nPID\tArrival\tBurst\tWaiting\tTurnaround\n");
    sched_printf("------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        processes[i].turnaround_time =
            processes[i].completion_time - processes[i].arrival_time;
        processes[i].waiting_time =
            processes[i].turnaround_time - processes[i].burst_time;
        total_wait       += processes[i].waiting_time;
        total_turnaround += processes[i].turnaround_time;
        sched_printf("%d\t%d\t%d\t%d\t%d\n",
            processes[i].pid,
            processes[i].arrival_time,
            processes[i].burst_time,
            processes[i].waiting_time,
            processes[i].turnaround_time);
    }
    sched_printf("\nAverage Waiting Time:    %.2f\n", total_wait / n);
    sched_printf("Average Turnaround Time: %.2f\n",  total_turnaround / n);
}

/* -----------------------------
Gantt Chart Helper
------------------------------ */
void print_gantt(int pid, int start, int end) {
    sched_printf("P%d\t[%d -> %d]\n", pid, start, end);
}
