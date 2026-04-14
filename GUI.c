#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduling.h"

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
    /* Data */
    Process procs[MAX_PROCESSES];
    int     n_procs;
} AppState;

/* ── Populate the Processes pane ────────────────────────── */
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

/* ── Load a processes file ──────────────────────────────── */
static gboolean load_file(AppState *s, const char *path)
{
    int n = read_processes(path, s->procs);
    if (n <= 0) return FALSE;

    s->n_procs = n;

    /* Show filename in label */
    const char *fname = strrchr(path, '/');
    if (!fname) fname = strrchr(path, '\\');
    fname = fname ? fname + 1 : path;

    char label[512];
    snprintf(label, sizeof(label), "File: %s  (%d processes)", fname, n);
    gtk_label_set_text(GTK_LABEL(s->file_label), label);

    refresh_process_view(s);
    gtk_widget_set_sensitive(s->run_btn, TRUE);

    /* Clear any previous results */
    GtkTextBuffer *rbuf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->result_view));
    gtk_text_buffer_set_text(rbuf, "", -1);

    return TRUE;
}

/* ── File-chooser response ──────────────────────────────── */
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

/* ── "Open File" button ─────────────────────────────────── */
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

/* ── Enable quantum spinner only when Round Robin is active  */
static void on_algo_toggled(GtkCheckButton *btn, gpointer user_data)
{
    (void)btn;
    AppState *s = (AppState *)user_data;
    gboolean rr = gtk_check_button_get_active(
                      GTK_CHECK_BUTTON(s->btn_rr));
    gtk_widget_set_sensitive(s->quantum_spin, rr);
}

/* ── "Run Simulation" button ────────────────────────────── */
static void on_run_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    AppState *s = (AppState *)user_data;
    if (s->n_procs <= 0) return;

    reset_processes(s->procs, s->n_procs);
    clear_sched_output();

    const char *algo_name;

    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(s->btn_fcfs))) {
        algo_name = "First Come First Serve (FCFS)";
        fcfs(s->procs, s->n_procs);

    } else if (gtk_check_button_get_active(GTK_CHECK_BUTTON(s->btn_sjf))) {
        algo_name = "Shortest Job First (SJF)";
        sjf(s->procs, s->n_procs);

    } else {
        int q = gtk_spin_button_get_value_as_int(
                    GTK_SPIN_BUTTON(s->quantum_spin));
        /* Build label with quantum value */
        static char rr_label[64];
        snprintf(rr_label, sizeof(rr_label),
                 "Round Robin (RR) — Quantum = %d", q);
        algo_name = rr_label;
        round_robin(s->procs, s->n_procs, q);
    }

    print_metrics(s->procs, s->n_procs);

    /* Build display string: header + Gantt + metrics */
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "=== %s ===\n\n", algo_name);
    g_string_append(out, "Gantt Chart:\n");
    g_string_append(out, "------------\n");
    g_string_append(out, get_sched_output());

    GtkTextBuffer *buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->result_view));
    gtk_text_buffer_set_text(buf, out->str, -1);
    g_string_free(out, TRUE);
}

/* ── Helper: make a labelled scrolled text view ─────────── */
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
    AppState *s = g_new0(AppState, 1);

    /* Window */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window),
                         "Process Scheduler Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 820, 600);

    /* Root vbox */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_window_set_child(GTK_WINDOW(window), root);

    /* ── Row 1: file open ── */
    GtkWidget *file_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root), file_row);

    GtkWidget *open_btn = gtk_button_new_with_label("Open File");
    gtk_box_append(GTK_BOX(file_row), open_btn);

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

    /* Radio buttons */
    GtkWidget *radio_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(algo_box), radio_row);

    s->btn_fcfs = gtk_check_button_new_with_label(
                      "First Come First Serve (FCFS)");
    s->btn_sjf  = gtk_check_button_new_with_label(
                      "Shortest Job First (SJF)");
    s->btn_rr   = gtk_check_button_new_with_label(
                      "Round Robin (RR)");

    /* Group them as mutually exclusive radio buttons */
    gtk_check_button_set_group(GTK_CHECK_BUTTON(s->btn_sjf),
                               GTK_CHECK_BUTTON(s->btn_fcfs));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(s->btn_rr),
                               GTK_CHECK_BUTTON(s->btn_fcfs));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(s->btn_fcfs), TRUE);

    gtk_box_append(GTK_BOX(radio_row), s->btn_fcfs);
    gtk_box_append(GTK_BOX(radio_row), s->btn_sjf);
    gtk_box_append(GTK_BOX(radio_row), s->btn_rr);

    /* Quantum row (only active when RR is selected) */
    GtkWidget *q_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(algo_box), q_row);

    GtkWidget *q_label = gtk_label_new("Time Quantum:");
    gtk_box_append(GTK_BOX(q_row), q_label);

    s->quantum_spin = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->quantum_spin), 2);
    gtk_widget_set_sensitive(s->quantum_spin, FALSE);
    gtk_box_append(GTK_BOX(q_row), s->quantum_spin);

    /* Run button */
    s->run_btn = gtk_button_new_with_label("Run Simulation");
    gtk_widget_set_sensitive(s->run_btn, FALSE);
    gtk_widget_set_halign(s->run_btn, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(algo_box), s->run_btn);

    /* ── Row 3: split pane (processes | results) ── */
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

    /* ── Signals ── */
    g_signal_connect(open_btn,    "clicked", G_CALLBACK(on_open_clicked), s);
    g_signal_connect(s->run_btn,  "clicked", G_CALLBACK(on_run_clicked),  s);
    g_signal_connect(s->btn_fcfs, "toggled", G_CALLBACK(on_algo_toggled), s);
    g_signal_connect(s->btn_sjf,  "toggled", G_CALLBACK(on_algo_toggled), s);
    g_signal_connect(s->btn_rr,   "toggled", G_CALLBACK(on_algo_toggled), s);

    /* Auto-load processes.txt from the working directory if present */
    load_file(s, "processes.txt");

    gtk_window_present(GTK_WINDOW(window));
}

/* ── Entry point ────────────────────────────────────────── */
int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new(
        "edu.wsu.cpts360.scheduler", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
