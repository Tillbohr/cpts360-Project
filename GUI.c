#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduling.h"
#include "db.h"

/* ── Gantt chart drawing constants ──────────────────────── */
#define GANTT_PX_PER_UNIT  44
#define GANTT_LEFT_PAD     10
#define GANTT_RIGHT_PAD    24
#define GANTT_BAR_Y        16
#define GANTT_BAR_H        54
#define GANTT_TICK_Y       76
#define GANTT_LABEL_Y      92
#define GANTT_CHART_H      108
#define GANTT_BAR_GAP      2

static const double GANTT_COLORS[8][3] = {
    {0.27, 0.52, 0.85},
    {0.90, 0.47, 0.13},
    {0.20, 0.70, 0.40},
    {0.84, 0.32, 0.32},
    {0.60, 0.40, 0.80},
    {0.76, 0.69, 0.13},
    {0.13, 0.70, 0.80},
    {0.88, 0.40, 0.65},
};

/* ── App state ──────────────────────────────────────────── */
typedef struct {
    /* Widgets */
    GtkWidget *file_label;
    GtkWidget *process_view;
    GtkWidget *result_view;
    GtkWidget *btn_fcfs;
    GtkWidget *btn_sjf;
    GtkWidget *btn_rr;
    GtkWidget *quantum_spin;
    GtkWidget *run_btn;
    GtkWidget *save_btn;
    GtkWidget *load_btn;
    GtkWidget *gantt_area;
    GtkWidget *gantt_scroll;
    GtkWidget *status_label;
    /* Data */
    Process procs[MAX_PROCESSES];
    int     n_procs;
    char    last_algo[64];  /* short name saved to DB    */
    int     last_quantum;
    int     has_result;
} AppState;

/* ── Load-dialog state ──────────────────────────────────── */
typedef struct {
    AppState  *app;
    GtkWidget *list_box;
    GtkWidget *window;
    SimRecord *records;
    int        n_records;
} LoadDialogData;

/* ── Forward declarations ───────────────────────────────── */
static void refresh_process_view(AppState *s);

/* ── Gantt draw function ────────────────────────────────── */
static void draw_gantt_chart(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data)
{
    (void)area; (void)width; (void)height; (void)user_data;

    int n = get_gantt_count();

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    if (n == 0) {
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
        cairo_select_font_face(cr, "Sans",
                               CAIRO_FONT_SLANT_ITALIC,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13.0);
        cairo_move_to(cr, GANTT_LEFT_PAD + 8,
                          GANTT_BAR_Y + GANTT_BAR_H / 2.0 + 5);
        cairo_show_text(cr,
            "Run a simulation to see the Gantt chart.");
        return;
    }

    const GanttSlot *slots = get_gantt_slots();

    int max_time = 0;
    for (int i = 0; i < n; i++)
        if (slots[i].end > max_time) max_time = slots[i].end;

    /* Build PID → colour index (first-appearance order) */
    int pid_seen[MAX_PROCESSES];
    int pid_count = 0;
    for (int i = 0; i < n; i++) {
        int found = 0;
        for (int j = 0; j < pid_count; j++)
            if (pid_seen[j] == slots[i].pid) { found = 1; break; }
        if (!found && pid_count < MAX_PROCESSES)
            pid_seen[pid_count++] = slots[i].pid;
    }

    cairo_select_font_face(cr, "Sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);

    for (int i = 0; i < n; i++) {
        double bx = GANTT_LEFT_PAD + slots[i].start * GANTT_PX_PER_UNIT;
        double bw = (slots[i].end - slots[i].start) * GANTT_PX_PER_UNIT
                    - GANTT_BAR_GAP;
        double by = (double)GANTT_BAR_Y;
        double bh = (double)GANTT_BAR_H;

        int ci = 0;
        for (int j = 0; j < pid_count; j++)
            if (pid_seen[j] == slots[i].pid) { ci = j % 8; break; }

        cairo_set_source_rgb(cr,
            GANTT_COLORS[ci][0], GANTT_COLORS[ci][1], GANTT_COLORS[ci][2]);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_fill(cr);

        cairo_set_source_rgb(cr,
            GANTT_COLORS[ci][0] * 0.65,
            GANTT_COLORS[ci][1] * 0.65,
            GANTT_COLORS[ci][2] * 0.65);
        cairo_set_line_width(cr, 1.2);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_stroke(cr);

        if (bw > 14) {
            char label[12];
            snprintf(label, sizeof(label), "P%d", slots[i].pid);
            cairo_text_extents_t te;
            cairo_text_extents(cr, label, &te);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_move_to(cr,
                bx + (bw - te.width)  / 2.0 - te.x_bearing,
                by + (bh - te.height) / 2.0 - te.y_bearing);
            cairo_show_text(cr, label);
        }
    }

    /* Time axis */
    double ax0 = GANTT_LEFT_PAD;
    double ax1 = GANTT_LEFT_PAD + max_time * GANTT_PX_PER_UNIT;
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, ax0, GANTT_TICK_Y);
    cairo_line_to(cr, ax1, GANTT_TICK_Y);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);

    for (int t = 0; t <= max_time; t++) {
        double tx = GANTT_LEFT_PAD + t * GANTT_PX_PER_UNIT;
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, tx, GANTT_TICK_Y);
        cairo_line_to(cr, tx, GANTT_TICK_Y + 6);
        cairo_stroke(cr);

        char tlabel[8];
        snprintf(tlabel, sizeof(tlabel), "%d", t);
        cairo_text_extents_t te;
        cairo_text_extents(cr, tlabel, &te);
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_move_to(cr,
            tx - te.width / 2.0 - te.x_bearing,
            (double)GANTT_LABEL_Y);
        cairo_show_text(cr, tlabel);
    }
}

/* ── Helpers ────────────────────────────────────────────── */

/* Resize + redraw the Gantt area to fit the current slots */
static void update_gantt_chart(AppState *s)
{
    int max_time = 0;
    const GanttSlot *slots = get_gantt_slots();
    for (int i = 0; i < get_gantt_count(); i++)
        if (slots[i].end > max_time) max_time = slots[i].end;

    int chart_w = GANTT_LEFT_PAD
                  + max_time * GANTT_PX_PER_UNIT
                  + GANTT_RIGHT_PAD;
    gtk_widget_set_size_request(s->gantt_area, chart_w, GANTT_CHART_H);
    gtk_widget_queue_draw(s->gantt_area);
}

/* Populate the Processes text pane */
static void refresh_process_view(AppState *s)
{
    GString *sb = g_string_new("PID    Arrival    Burst\n");
    g_string_append(sb,        "----------------------\n");
    for (int i = 0; i < s->n_procs; i++) {
        g_string_append_printf(sb, "%-7d%-11d%d\n",
            s->procs[i].pid,
            s->procs[i].arrival_time,
            s->procs[i].burst_time);
    }
    GtkTextBuffer *buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->process_view));
    gtk_text_buffer_set_text(buf, sb->str, -1);
    g_string_free(sb, TRUE);
}

/* Render the text results pane from the current sched output */
static void refresh_result_view(AppState *s)
{
    const char *algo = s->last_algo;
    GString *out = g_string_new(NULL);
    if (s->last_quantum > 0)
        g_string_append_printf(out, "=== %s (q=%d) ===\n\n",
                               algo, s->last_quantum);
    else
        g_string_append_printf(out, "=== %s ===\n\n", algo);
    g_string_append(out, "Gantt Chart:\n");
    g_string_append(out, "------------\n");
    g_string_append(out, get_sched_output());

    GtkTextBuffer *buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->result_view));
    gtk_text_buffer_set_text(buf, out->str, -1);
    g_string_free(out, TRUE);
}

/* ── File loading ───────────────────────────────────────── */
static gboolean load_file(AppState *s, const char *path)
{
    int n = read_processes(path, s->procs);
    if (n <= 0) return FALSE;

    s->n_procs    = n;
    s->has_result = 0;

    const char *fname = strrchr(path, '/');
    if (!fname) fname = strrchr(path, '\\');
    fname = fname ? fname + 1 : path;

    char label[512];
    snprintf(label, sizeof(label),
             "File: %s  (%d processes)", fname, n);
    gtk_label_set_text(GTK_LABEL(s->file_label), label);

    refresh_process_view(s);
    gtk_widget_set_sensitive(s->run_btn,  TRUE);
    gtk_widget_set_sensitive(s->save_btn, FALSE);

    GtkTextBuffer *rbuf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->result_view));
    gtk_text_buffer_set_text(rbuf, "", -1);
    gtk_widget_set_size_request(s->gantt_area, 400, GANTT_CHART_H);
    gtk_widget_queue_draw(s->gantt_area);

    return TRUE;
}

static void on_file_response(GtkDialog *dialog, int response,
                             gpointer user_data)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        char  *path = g_file_get_path(file);
        if (!load_file((AppState *)user_data, path))
            gtk_label_set_text(
                GTK_LABEL(((AppState *)user_data)->file_label),
                "Error: could not read file.");
        g_free(path);
        g_object_unref(file);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_open_clicked(GtkWidget *btn, gpointer user_data)
{
    AppState  *s   = (AppState *)user_data;
    GtkWidget *top = gtk_widget_get_ancestor(btn, GTK_TYPE_WINDOW);

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Process Schedule File",
        GTK_WINDOW(top),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Text files (*.txt)");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_file_response), s);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── Algorithm toggle ───────────────────────────────────── */
static void on_algo_toggled(GtkCheckButton *btn, gpointer user_data)
{
    (void)btn;
    AppState *s = (AppState *)user_data;
    gboolean rr = gtk_check_button_get_active(
                      GTK_CHECK_BUTTON(s->btn_rr));
    gtk_widget_set_sensitive(s->quantum_spin, rr);
}

/* ── Run simulation ─────────────────────────────────────── */
static void on_run_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    AppState *s = (AppState *)user_data;
    if (s->n_procs <= 0) return;

    reset_processes(s->procs, s->n_procs);
    clear_sched_output();

    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(s->btn_fcfs))) {
        strncpy(s->last_algo, "FCFS", sizeof(s->last_algo) - 1);
        s->last_quantum = 0;
        fcfs(s->procs, s->n_procs);

    } else if (gtk_check_button_get_active(GTK_CHECK_BUTTON(s->btn_sjf))) {
        strncpy(s->last_algo, "SJF", sizeof(s->last_algo) - 1);
        s->last_quantum = 0;
        sjf(s->procs, s->n_procs);

    } else {
        s->last_quantum = gtk_spin_button_get_value_as_int(
                              GTK_SPIN_BUTTON(s->quantum_spin));
        strncpy(s->last_algo, "Round Robin",
                sizeof(s->last_algo) - 1);
        round_robin(s->procs, s->n_procs, s->last_quantum);
    }

    print_metrics(s->procs, s->n_procs);
    s->has_result = 1;

    refresh_result_view(s);
    update_gantt_chart(s);

    gtk_widget_set_sensitive(s->save_btn, TRUE);
    gtk_label_set_text(GTK_LABEL(s->status_label),
                       "Simulation complete. Click \"Save\" to store the result.");
}

/* ── Save to database ───────────────────────────────────── */
static void on_save_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    AppState *s = (AppState *)user_data;
    if (!s->has_result) return;

    int id = db_save_simulation(
        s->last_algo,
        s->last_quantum,
        s->procs,    s->n_procs,
        get_gantt_slots(), get_gantt_count(),
        get_sched_output());

    if (id < 0) {
        gtk_label_set_text(GTK_LABEL(s->status_label),
            "Error: could not save to database.");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Saved to database — Simulation ID: %d", id);
        gtk_label_set_text(GTK_LABEL(s->status_label), msg);
        gtk_widget_set_sensitive(s->save_btn, FALSE);
    }
}

/* ── Load dialog ────────────────────────────────────────── */

/* Restore a full simulation from the database into the UI */
static void apply_loaded_simulation(AppState *s, int sim_id,
                                    const char *timestamp)
{
    char       algo[64];
    int        quantum    = 0;
    char      *text       = g_malloc(DB_TEXT_SIZE);
    Process    procs[MAX_PROCESSES];
    GanttSlot  slots[MAX_SLOTS];
    int        np = 0, ns = 0;

    if (db_load_simulation(sim_id, algo, &quantum, text,
                           procs, &np, slots, &ns) != 0) {
        gtk_label_set_text(GTK_LABEL(s->status_label),
                           "Error: failed to load simulation from database.");
        g_free(text);
        return;
    }

    /* Restore data into app state */
    s->n_procs = np;
    memcpy(s->procs, procs, np * sizeof(Process));
    strncpy(s->last_algo, algo, sizeof(s->last_algo) - 1);
    s->last_algo[sizeof(s->last_algo) - 1] = '\0';
    s->last_quantum = quantum;
    s->has_result   = 1;

    /* Restore scheduling output buffers */
    set_sched_output(text);
    set_gantt_slots(slots, ns);
    g_free(text);

    /* Update process list */
    refresh_process_view(s);

    /* Update results text */
    refresh_result_view(s);

    /* Update Gantt chart */
    update_gantt_chart(s);

    /* Restore algorithm radio selection */
    if (strstr(algo, "FCFS")) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->btn_fcfs), TRUE);
    } else if (strstr(algo, "SJF")) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->btn_sjf), TRUE);
    } else {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->btn_rr), TRUE);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->quantum_spin), quantum);
    }

    /* Update labels */
    char file_msg[128];
    snprintf(file_msg, sizeof(file_msg),
             "Loaded from DB: %s (ID %d)", algo, sim_id);
    gtk_label_set_text(GTK_LABEL(s->file_label), file_msg);

    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg),
             "Loaded simulation #%d  —  %s  —  saved %s",
             sim_id, algo, timestamp ? timestamp : "");
    gtk_label_set_text(GTK_LABEL(s->status_label), status_msg);

    /* Re-enable Run, disable Save (result is already in DB) */
    gtk_widget_set_sensitive(s->run_btn,  TRUE);
    gtk_widget_set_sensitive(s->save_btn, FALSE);
}

static void on_load_confirm_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn;
    LoadDialogData *ldd = (LoadDialogData *)data;

    GtkListBoxRow *row =
        gtk_list_box_get_selected_row(GTK_LIST_BOX(ldd->list_box));
    if (!row) {
        return;  /* nothing selected */
    }

    int sim_id = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(row), "sim-id"));

    /* Find the matching record for its timestamp */
    const char *ts = "";
    for (int i = 0; i < ldd->n_records; i++) {
        if (ldd->records[i].id == sim_id) {
            ts = ldd->records[i].timestamp;
            break;
        }
    }

    apply_loaded_simulation(ldd->app, sim_id, ts);
    gtk_window_destroy(GTK_WINDOW(ldd->window));
}

static void on_load_dialog_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    LoadDialogData *ldd = (LoadDialogData *)data;
    db_free_records(ldd->records);
    g_free(ldd);
}

static void on_cancel_load(GtkWidget *btn, gpointer win)
{
    (void)btn;
    gtk_window_destroy(GTK_WINDOW(win));
}

static void on_load_clicked(GtkWidget *btn, gpointer user_data)
{
    AppState  *s      = (AppState *)user_data;
    GtkWidget *parent =
        gtk_widget_get_ancestor(btn, GTK_TYPE_WINDOW);

    /* ── Build dialog window ── */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog),
                         "Load Saved Simulation");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 380);
    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    GtkWidget *heading = gtk_label_new("Select a saved simulation:");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), heading);

    /* Scrolled list */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box),
                                    GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                  list_box);

    /* Populate rows */
    SimRecord *records  = NULL;
    int        n_records = db_load_simulations(&records);

    if (n_records == 0) {
        GtkWidget *empty = gtk_label_new(
            "No saved simulations found.");
        gtk_widget_set_margin_top(empty, 20);
        gtk_list_box_append(GTK_LIST_BOX(list_box), empty);
    } else {
        for (int i = 0; i < n_records; i++) {
            char row_text[256];
            if (records[i].quantum > 0) {
                snprintf(row_text, sizeof(row_text),
                    "#%-4d  %-14s  q=%-3d  %-22s"
                    "  Avg Wait: %5.2f   Avg TAT: %5.2f",
                    records[i].id,
                    records[i].algorithm,
                    records[i].quantum,
                    records[i].timestamp,
                    records[i].avg_waiting,
                    records[i].avg_turnaround);
            } else {
                snprintf(row_text, sizeof(row_text),
                    "#%-4d  %-14s         %-22s"
                    "  Avg Wait: %5.2f   Avg TAT: %5.2f",
                    records[i].id,
                    records[i].algorithm,
                    records[i].timestamp,
                    records[i].avg_waiting,
                    records[i].avg_turnaround);
            }

            GtkWidget *row_label = gtk_label_new(row_text);
            gtk_label_set_xalign(GTK_LABEL(row_label), 0.0);
            gtk_widget_set_margin_start(row_label, 8);
            gtk_widget_set_margin_end(row_label, 8);
            gtk_widget_set_margin_top(row_label, 5);
            gtk_widget_set_margin_bottom(row_label, 5);
            gtk_widget_add_css_class(row_label, "monospace");

            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                       row_label);
            g_object_set_data(G_OBJECT(row), "sim-id",
                              GINT_TO_POINTER(records[i].id));
            gtk_list_box_append(GTK_LIST_BOX(list_box), row);
        }

        /* Pre-select the first (newest) row */
        GtkListBoxRow *first =
            gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_box), 0);
        if (first)
            gtk_list_box_select_row(GTK_LIST_BOX(list_box), first);
    }

    /* Button row */
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_row, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), btn_row);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *load_btn2  = gtk_button_new_with_label("Load Selected");
    gtk_box_append(GTK_BOX(btn_row), cancel_btn);
    gtk_box_append(GTK_BOX(btn_row), load_btn2);

    /* Wire up state struct */
    LoadDialogData *ldd  = g_new0(LoadDialogData, 1);
    ldd->app             = s;
    ldd->list_box        = list_box;
    ldd->window          = dialog;
    ldd->records         = records;
    ldd->n_records       = n_records;

    g_signal_connect(load_btn2,  "clicked",
                     G_CALLBACK(on_load_confirm_clicked), ldd);
    g_signal_connect(cancel_btn, "clicked",
                     G_CALLBACK(on_cancel_load), dialog);
    g_signal_connect(dialog, "destroy",
                     G_CALLBACK(on_load_dialog_destroy), ldd);

    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── Helper: labelled scrolled text view ────────────────── */
static GtkWidget *make_text_pane(const char *title, GtkWidget **tv_out)
{
    GtkWidget *frame  = gtk_frame_new(title);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_frame_set_child(GTK_FRAME(frame), scroll);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_NONE);
    gtk_widget_set_margin_start(tv, 4);
    gtk_widget_set_margin_top(tv, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);

    if (tv_out) *tv_out = tv;
    return frame;
}

/* ── Build the UI ───────────────────────────────────────── */
static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    if (db_open("scheduler.db") != 0)
        fprintf(stderr, "Warning: could not open scheduler.db\n");

    AppState *s = g_new0(AppState, 1);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window),
                         "Process Scheduler Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 860, 700);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 8);
    gtk_window_set_child(GTK_WINDOW(window), root);

    /* ── Row 1: file open + load from DB ── */
    GtkWidget *file_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root), file_row);

    GtkWidget *open_btn = gtk_button_new_with_label("Open File");
    gtk_box_append(GTK_BOX(file_row), open_btn);

    s->load_btn = gtk_button_new_with_label("Load from DB");
    gtk_box_append(GTK_BOX(file_row), s->load_btn);

    s->file_label = gtk_label_new("No file loaded.");
    gtk_widget_set_halign(s->file_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(s->file_label, TRUE);
    gtk_box_append(GTK_BOX(file_row), s->file_label);

    /* ── Row 2: algorithm frame ── */
    GtkWidget *algo_frame = gtk_frame_new("Scheduling Algorithm");
    gtk_box_append(GTK_BOX(root), algo_frame);

    GtkWidget *algo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(algo_box, 10);
    gtk_widget_set_margin_end(algo_box, 10);
    gtk_widget_set_margin_top(algo_box, 8);
    gtk_widget_set_margin_bottom(algo_box, 10);
    gtk_frame_set_child(GTK_FRAME(algo_frame), algo_box);

    GtkWidget *radio_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(algo_box), radio_row);

    s->btn_fcfs = gtk_check_button_new_with_label(
                      "First Come First Serve (FCFS)");
    s->btn_sjf  = gtk_check_button_new_with_label(
                      "Shortest Job First (SJF)");
    s->btn_rr   = gtk_check_button_new_with_label(
                      "Round Robin (RR)");

    gtk_check_button_set_group(GTK_CHECK_BUTTON(s->btn_sjf),
                               GTK_CHECK_BUTTON(s->btn_fcfs));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(s->btn_rr),
                               GTK_CHECK_BUTTON(s->btn_fcfs));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(s->btn_fcfs), TRUE);

    gtk_box_append(GTK_BOX(radio_row), s->btn_fcfs);
    gtk_box_append(GTK_BOX(radio_row), s->btn_sjf);
    gtk_box_append(GTK_BOX(radio_row), s->btn_rr);

    GtkWidget *q_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(algo_box), q_row);
    gtk_box_append(GTK_BOX(q_row), gtk_label_new("Time Quantum:"));

    s->quantum_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->quantum_spin), 2);
    gtk_widget_set_sensitive(s->quantum_spin, FALSE);
    gtk_box_append(GTK_BOX(q_row), s->quantum_spin);

    /* Run + Save buttons side by side */
    GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(algo_box), action_row);

    s->run_btn = gtk_button_new_with_label("Run Simulation");
    gtk_widget_set_sensitive(s->run_btn, FALSE);
    gtk_box_append(GTK_BOX(action_row), s->run_btn);

    s->save_btn = gtk_button_new_with_label("Save Result");
    gtk_widget_set_sensitive(s->save_btn, FALSE);
    gtk_box_append(GTK_BOX(action_row), s->save_btn);

    /* ── Row 3: processes | results pane ── */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    GtkWidget *proc_frame = make_text_pane("Processes",
                                           &s->process_view);
    gtk_paned_set_start_child(GTK_PANED(paned), proc_frame);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);

    GtkWidget *res_frame = make_text_pane("Results", &s->result_view);
    gtk_paned_set_end_child(GTK_PANED(paned), res_frame);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 240);

    /* ── Row 4: Gantt chart ── */
    GtkWidget *gantt_frame = gtk_frame_new("Gantt Chart");
    gtk_box_append(GTK_BOX(root), gantt_frame);

    s->gantt_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(s->gantt_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(s->gantt_scroll, -1,
                                GANTT_CHART_H + 16);
    gtk_frame_set_child(GTK_FRAME(gantt_frame), s->gantt_scroll);

    s->gantt_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s->gantt_area, 400, GANTT_CHART_H);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(s->gantt_area),
        draw_gantt_chart, s, NULL);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(s->gantt_scroll), s->gantt_area);

    /* ── Row 5: status bar ── */
    s->status_label = gtk_label_new("Ready.");
    gtk_widget_set_halign(s->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(s->status_label, 2);
    gtk_box_append(GTK_BOX(root), s->status_label);

    /* ── Signals ── */
    g_signal_connect(open_btn,    "clicked",
                     G_CALLBACK(on_open_clicked),  s);
    g_signal_connect(s->load_btn, "clicked",
                     G_CALLBACK(on_load_clicked),  s);
    g_signal_connect(s->run_btn,  "clicked",
                     G_CALLBACK(on_run_clicked),   s);
    g_signal_connect(s->save_btn, "clicked",
                     G_CALLBACK(on_save_clicked),  s);
    g_signal_connect(s->btn_fcfs, "toggled",
                     G_CALLBACK(on_algo_toggled),  s);
    g_signal_connect(s->btn_sjf,  "toggled",
                     G_CALLBACK(on_algo_toggled),  s);
    g_signal_connect(s->btn_rr,   "toggled",
                     G_CALLBACK(on_algo_toggled),  s);

    /* Auto-load processes.txt from the working directory */
    load_file(s, "processes.txt");

    gtk_window_present(GTK_WINDOW(window));
}

/* ── Entry point ────────────────────────────────────────── */
int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new(
        "cpts360.scheduler", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    db_close();
    return status;
}
