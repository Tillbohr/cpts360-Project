#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "db.h"

static sqlite3 *db_handle = NULL;

/* ── Single-entry simulation cache ─────────────────────────── */
typedef struct {
    int       valid;
    int       sim_id;
    char      algo[64];
    int       quantum;
    char     *text;                      /* heap-allocated, DB_TEXT_SIZE bytes */
    Process   procs[MAX_PROCESSES];
    int       n_procs;
    GanttSlot slots[MAX_SLOTS];
    int       n_slots;
} SimCache;

static SimCache sim_cache = { .valid = 0 };

static void cache_invalidate(void)
{
    if (sim_cache.valid && sim_cache.text) {
        free(sim_cache.text);
        sim_cache.text = NULL;
    }
    sim_cache.valid = 0;
}

/* ── Schema ─────────────────────────────────────────────── */
static const char *CREATE_SQL =
    "PRAGMA foreign_keys = ON;"

    "CREATE TABLE IF NOT EXISTS simulations ("
    "  id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  algorithm           TEXT    NOT NULL,"
    "  quantum             INTEGER NOT NULL DEFAULT 0,"
    "  timestamp           TEXT    NOT NULL,"
    "  avg_waiting_time    REAL    NOT NULL DEFAULT 0,"
    "  avg_turnaround_time REAL    NOT NULL DEFAULT 0,"
    "  text_output         TEXT    NOT NULL DEFAULT ''"
    ");"

    "CREATE TABLE IF NOT EXISTS process_results ("
    "  simulation_id   INTEGER NOT NULL"
    "    REFERENCES simulations(id) ON DELETE CASCADE,"
    "  pid             INTEGER NOT NULL,"
    "  arrival_time    INTEGER NOT NULL,"
    "  burst_time      INTEGER NOT NULL,"
    "  waiting_time    INTEGER NOT NULL,"
    "  turnaround_time INTEGER NOT NULL"
    ");"

    "CREATE TABLE IF NOT EXISTS gantt_slots ("
    "  simulation_id INTEGER NOT NULL"
    "    REFERENCES simulations(id) ON DELETE CASCADE,"
    "  slot_order    INTEGER NOT NULL,"
    "  pid           INTEGER NOT NULL,"
    "  start_time    INTEGER NOT NULL,"
    "  end_time      INTEGER NOT NULL"
    ");";

/* ── Open / close ───────────────────────────────────────── */
int db_open(const char *path)
{
    if (sqlite3_open(path, &db_handle) != SQLITE_OK) {
        fprintf(stderr, "db_open: %s\n", sqlite3_errmsg(db_handle));
        return -1;
    }
    char *err = NULL;
    if (sqlite3_exec(db_handle, CREATE_SQL, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "db_open schema: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

void db_close(void)
{
    cache_invalidate();
    if (db_handle) {
        sqlite3_close(db_handle);
        db_handle = NULL;
    }
}

/* ── Save simulation ────────────────────────────────────── */
int db_save_simulation(
    const char      *algorithm,
    int              quantum,
    Process          procs[],
    int              n_procs,
    const GanttSlot *slots,
    int              n_slots,
    const char      *text_output)
{
    if (!db_handle) return -1;

    /* Compute averages from process array */
    double total_w = 0, total_t = 0;
    for (int i = 0; i < n_procs; i++) {
        total_w += procs[i].waiting_time;
        total_t += procs[i].turnaround_time;
    }
    double avg_w = n_procs > 0 ? total_w / n_procs : 0.0;
    double avg_t = n_procs > 0 ? total_t / n_procs : 0.0;

    sqlite3_exec(db_handle, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    /* ── Insert simulation header ── */
    const char *SIM_SQL =
        "INSERT INTO simulations"
        " (algorithm, quantum, timestamp,"
        "  avg_waiting_time, avg_turnaround_time, text_output)"
        " VALUES (?, ?, datetime('now','localtime'), ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_handle, SIM_SQL, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "db_save sim prepare: %s\n",
                sqlite3_errmsg(db_handle));
        sqlite3_exec(db_handle, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }
    sqlite3_bind_text  (stmt, 1, algorithm,   -1, SQLITE_STATIC);
    sqlite3_bind_int   (stmt, 2, quantum);
    sqlite3_bind_double(stmt, 3, avg_w);
    sqlite3_bind_double(stmt, 4, avg_t);
    sqlite3_bind_text  (stmt, 5, text_output, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "db_save sim step: %s\n",
                sqlite3_errmsg(db_handle));
        sqlite3_finalize(stmt);
        sqlite3_exec(db_handle, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }
    sqlite3_finalize(stmt);
    sqlite3_int64 sim_id = sqlite3_last_insert_rowid(db_handle);

    /* ── Insert process results ── */
    const char *PROC_SQL =
        "INSERT INTO process_results"
        " (simulation_id, pid, arrival_time, burst_time,"
        "  waiting_time, turnaround_time)"
        " VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_handle, PROC_SQL, -1, &stmt, NULL)
            == SQLITE_OK) {
        for (int i = 0; i < n_procs; i++) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, sim_id);
            sqlite3_bind_int  (stmt, 2, procs[i].pid);
            sqlite3_bind_int  (stmt, 3, procs[i].arrival_time);
            sqlite3_bind_int  (stmt, 4, procs[i].burst_time);
            sqlite3_bind_int  (stmt, 5, procs[i].waiting_time);
            sqlite3_bind_int  (stmt, 6, procs[i].turnaround_time);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    /* ── Insert Gantt slots ── */
    const char *SLOT_SQL =
        "INSERT INTO gantt_slots"
        " (simulation_id, slot_order, pid, start_time, end_time)"
        " VALUES (?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_handle, SLOT_SQL, -1, &stmt, NULL)
            == SQLITE_OK) {
        for (int i = 0; i < n_slots; i++) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, sim_id);
            sqlite3_bind_int  (stmt, 2, i);
            sqlite3_bind_int  (stmt, 3, slots[i].pid);
            sqlite3_bind_int  (stmt, 4, slots[i].start);
            sqlite3_bind_int  (stmt, 5, slots[i].end);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db_handle, "COMMIT;", NULL, NULL, NULL);
    cache_invalidate();   /* new save invalidates any cached load */
    return (int)sim_id;
}

/* ── Load simulation list ───────────────────────────────── */
int db_load_simulations(SimRecord **out)
{
    if (!db_handle || !out) { *out = NULL; return 0; }

    const char *SEL =
        "SELECT id, algorithm, quantum, timestamp,"
        "       avg_waiting_time, avg_turnaround_time"
        " FROM simulations ORDER BY id DESC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_handle, SEL, -1, &stmt, NULL) != SQLITE_OK) {
        *out = NULL;
        return 0;
    }

    /* Count rows first */
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) count++;
    sqlite3_reset(stmt);

    if (count == 0) {
        sqlite3_finalize(stmt);
        *out = NULL;
        return 0;
    }

    SimRecord *recs = malloc(count * sizeof(SimRecord));
    if (!recs) { sqlite3_finalize(stmt); *out = NULL; return 0; }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        recs[idx].id = sqlite3_column_int(stmt, 0);

        const char *algo = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(recs[idx].algorithm, algo ? algo : "", 63);
        recs[idx].algorithm[63] = '\0';

        recs[idx].quantum = sqlite3_column_int(stmt, 2);

        const char *ts = (const char *)sqlite3_column_text(stmt, 3);
        strncpy(recs[idx].timestamp, ts ? ts : "", 31);
        recs[idx].timestamp[31] = '\0';

        recs[idx].avg_waiting    = sqlite3_column_double(stmt, 4);
        recs[idx].avg_turnaround = sqlite3_column_double(stmt, 5);
        idx++;
    }
    sqlite3_finalize(stmt);

    *out = recs;
    return idx;
}

void db_free_records(SimRecord *records) { free(records); }

/* ── Load one full simulation ───────────────────────────── */
int db_load_simulation(
    int        sim_id,
    char       algo_out[64],
    int       *quantum_out,
    char      *text_out,
    Process    procs_out[],
    int       *n_procs_out,
    GanttSlot  slots_out[],
    int       *n_slots_out)
{
    if (!db_handle) return -1;

    /* ── Cache hit ── */
    if (sim_cache.valid && sim_cache.sim_id == sim_id) {
        strncpy(algo_out, sim_cache.algo, 63);
        algo_out[63] = '\0';
        *quantum_out = sim_cache.quantum;
        strncpy(text_out, sim_cache.text, DB_TEXT_SIZE - 1);
        text_out[DB_TEXT_SIZE - 1] = '\0';
        memcpy(procs_out, sim_cache.procs,
               sim_cache.n_procs * sizeof(Process));
        *n_procs_out = sim_cache.n_procs;
        memcpy(slots_out, sim_cache.slots,
               sim_cache.n_slots * sizeof(GanttSlot));
        *n_slots_out = sim_cache.n_slots;
        return 0;
    }

    /* ── Cache miss: query the database ── */
    sqlite3_stmt *stmt;

    /* Simulation header */
    const char *SEL_SIM =
        "SELECT algorithm, quantum, text_output"
        " FROM simulations WHERE id = ?;";
    if (sqlite3_prepare_v2(db_handle, SEL_SIM, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, sim_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    const char *algo = (const char *)sqlite3_column_text(stmt, 0);
    strncpy(algo_out, algo ? algo : "", 63);
    algo_out[63] = '\0';
    *quantum_out = sqlite3_column_int(stmt, 1);
    const char *txt = (const char *)sqlite3_column_text(stmt, 2);
    strncpy(text_out, txt ? txt : "", DB_TEXT_SIZE - 1);
    text_out[DB_TEXT_SIZE - 1] = '\0';
    sqlite3_finalize(stmt);

    /* Process results */
    const char *SEL_PROCS =
        "SELECT pid, arrival_time, burst_time,"
        "       waiting_time, turnaround_time"
        " FROM process_results WHERE simulation_id = ?"
        " ORDER BY rowid;";
    if (sqlite3_prepare_v2(db_handle, SEL_PROCS, -1, &stmt, NULL)
            != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, sim_id);
    int np = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && np < MAX_PROCESSES) {
        procs_out[np].pid             = sqlite3_column_int(stmt, 0);
        procs_out[np].arrival_time    = sqlite3_column_int(stmt, 1);
        procs_out[np].burst_time      = sqlite3_column_int(stmt, 2);
        procs_out[np].waiting_time    = sqlite3_column_int(stmt, 3);
        procs_out[np].turnaround_time = sqlite3_column_int(stmt, 4);
        procs_out[np].remaining_time  = 0;
        procs_out[np].completion_time =
            procs_out[np].arrival_time + procs_out[np].turnaround_time;
        np++;
    }
    *n_procs_out = np;
    sqlite3_finalize(stmt);

    /* Gantt slots */
    const char *SEL_SLOTS =
        "SELECT pid, start_time, end_time"
        " FROM gantt_slots WHERE simulation_id = ?"
        " ORDER BY slot_order;";
    if (sqlite3_prepare_v2(db_handle, SEL_SLOTS, -1, &stmt, NULL)
            != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, sim_id);
    int ns = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && ns < MAX_SLOTS) {
        slots_out[ns].pid   = sqlite3_column_int(stmt, 0);
        slots_out[ns].start = sqlite3_column_int(stmt, 1);
        slots_out[ns].end   = sqlite3_column_int(stmt, 2);
        ns++;
    }
    *n_slots_out = ns;
    sqlite3_finalize(stmt);

    /* ── Populate cache ── */
    if (!sim_cache.text) {
        sim_cache.text = malloc(DB_TEXT_SIZE);
    }
    if (sim_cache.text) {
        sim_cache.sim_id  = sim_id;
        strncpy(sim_cache.algo, algo_out, 63);
        sim_cache.algo[63] = '\0';
        sim_cache.quantum = *quantum_out;
        strncpy(sim_cache.text, text_out, DB_TEXT_SIZE - 1);
        sim_cache.text[DB_TEXT_SIZE - 1] = '\0';
        memcpy(sim_cache.procs, procs_out, np * sizeof(Process));
        sim_cache.n_procs = np;
        memcpy(sim_cache.slots, slots_out, ns * sizeof(GanttSlot));
        sim_cache.n_slots = ns;
        sim_cache.valid   = 1;
    }

    return 0;
}
