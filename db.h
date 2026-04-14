#ifndef DB_H
#define DB_H

#include "scheduling.h"

/* Max size of the stored text output (matches sched_buf in scheduling.c) */
#define DB_TEXT_SIZE 65536

/* Summary row returned by db_load_simulations() */
typedef struct {
    int    id;
    char   algorithm[64];
    int    quantum;            /* 0 for FCFS / SJF              */
    char   timestamp[32];
    double avg_waiting;
    double avg_turnaround;
} SimRecord;

/* Open (or create) the SQLite database at path.
   Returns 0 on success, -1 on error. */
int  db_open(const char *path);
void db_close(void);

/* Save the current simulation result.
   Returns the new row id on success, -1 on error. */
int db_save_simulation(
    const char      *algorithm,   /* short name: "FCFS", "SJF", "Round Robin" */
    int              quantum,     /* 0 unless Round Robin                      */
    Process          procs[],
    int              n_procs,
    const GanttSlot *slots,
    int              n_slots,
    const char      *text_output  /* raw sched_printf output                   */
);

/* Load a summary list of all simulations (newest first).
   *out is malloc'd; free it with db_free_records().
   Returns the number of records. */
int  db_load_simulations(SimRecord **out);
void db_free_records(SimRecord *records);

/* Fully load one simulation by id.
   text_out must point to a DB_TEXT_SIZE byte buffer.
   Returns 0 on success, -1 on error. */
int db_load_simulation(
    int        sim_id,
    char       algo_out[64],
    int       *quantum_out,
    char      *text_out,           /* DB_TEXT_SIZE bytes  */
    Process    procs_out[],
    int       *n_procs_out,
    GanttSlot  slots_out[],
    int       *n_slots_out
);

#endif /* DB_H */
