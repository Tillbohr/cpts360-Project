#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── State ─────────────────────────────────────────────── */
typedef struct {
    GtkWidget *text_view;
    GtkWidget *file_label;
    GtkWidget *analyze_btn;
} AppWidgets;

/* ── Load file into the text view ──────────────────────── */
static void load_file(AppWidgets *w, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->text_view));
        gtk_text_buffer_set_text(buf, "Error: could not open file.", -1);
        return;
    }

    /* Read entire file into a string */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *contents = malloc(size + 1);
    if (!contents) { fclose(fp); return; }
    fread(contents, 1, size, fp);
    contents[size] = '\0';
    fclose(fp);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->text_view));
    gtk_text_buffer_set_text(buf, contents, -1);
    free(contents);

    /* Show just the filename in the label */
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    char label[512];
    snprintf(label, sizeof(label), "Loaded: %s", filename);
    gtk_label_set_text(GTK_LABEL(w->file_label), label);

    gtk_widget_set_sensitive(w->analyze_btn, TRUE);
}

/* ── File-chooser response callback ────────────────────── */
static void on_file_response(GtkDialog *dialog, int response, gpointer user_data)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        char *path  = g_file_get_path(file);

        load_file((AppWidgets *)user_data, path);

        g_free(path);
        g_object_unref(file);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/* ── "Open File" button clicked ────────────────────────── */
static void on_open_clicked(GtkWidget *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;

    GtkWidget *top = gtk_widget_get_ancestor(btn, GTK_TYPE_WINDOW);
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Process Schedule File",
        GTK_WINDOW(top),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        NULL);

    /* Filter to .txt files */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Text files (*.txt)");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    g_signal_connect(dialog, "response", G_CALLBACK(on_file_response), w);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── "Analyze" button clicked (stub — wire in scheduling.c) */
static void on_analyze_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    AppWidgets *w = (AppWidgets *)user_data;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->text_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    /* TODO: pass `text` to your scheduling algorithms in scheduling.c */
    g_print("--- File contents passed to scheduler ---\n%s\n", text);
    g_print("-----------------------------------------\n");

    gtk_label_set_text(GTK_LABEL(w->file_label),
                       "Analysis triggered — see terminal output.");
    g_free(text);
}

/* ── Build UI ───────────────────────────────────────────── */
static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    AppWidgets *w = g_new0(AppWidgets, 1);

    /* Main window */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Process Scheduler Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);

    /* Root vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* Top button row */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget *open_btn = gtk_button_new_with_label("Open .txt File");
    gtk_box_append(GTK_BOX(hbox), open_btn);

    w->analyze_btn = gtk_button_new_with_label("Analyze");
    gtk_widget_set_sensitive(w->analyze_btn, FALSE); /* enabled after load */
    gtk_box_append(GTK_BOX(hbox), w->analyze_btn);

    /* File status label */
    w->file_label = gtk_label_new("No file loaded.");
    gtk_widget_set_halign(w->file_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), w->file_label);

    /* Separator */
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Scrollable text view */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);

    w->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(w->text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->text_view), GTK_WRAP_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), w->text_view);

    /* Connect signals */
    g_signal_connect(open_btn,      "clicked", G_CALLBACK(on_open_clicked),   w);
    g_signal_connect(w->analyze_btn,"clicked", G_CALLBACK(on_analyze_clicked), w);

    gtk_window_present(GTK_WINDOW(window));
}

/* ── Entry point ─────────────────────────────────────────── */
int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new(
        "edu.wsu.cpts360.scheduler", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
