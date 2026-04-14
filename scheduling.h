#ifndef SCHEDULING_H
#define SCHEDULING_H

#define MAX_PROCESSES 100

typedef struct {
    int pid;
    int arrival_time;
    int burst_time;
    int remaining_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
} Process;

typedef struct {
    Process *processes[MAX_PROCESSES];
    int front, back;
} ProcessQueue;

/* Queue helpers */
void     enqueue(ProcessQueue *pq, Process *p);
Process *dequeue(ProcessQueue *pq);
int      isEmpty(ProcessQueue *pq);
int      isInQueue(ProcessQueue *pq, int targetPid, int length);

/* Scheduling algorithms */
int  read_processes(const char *filename, Process processes[]);
void reset_processes(Process processes[], int n);
void fcfs(Process processes[], int n);
void sjf(Process processes[], int n);
void round_robin(Process processes[], int n, int quantum);
void print_metrics(Process processes[], int n);
void print_gantt(int pid, int start, int end);

/* Output buffer — results are written here instead of stdout */
void        clear_sched_output(void);
const char *get_sched_output(void);

/* Gantt slots — structured data for graphical chart */
typedef struct {
    int pid;
    int start;
    int end;
} GanttSlot;

#define MAX_SLOTS 1024

const GanttSlot *get_gantt_slots(void);
int              get_gantt_count(void);

/* Restore scheduling state from a loaded database record */
void set_gantt_slots(const GanttSlot *slots, int count);
void set_sched_output(const char *text);

#endif /* SCHEDULING_H */
