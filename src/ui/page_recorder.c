#include "core/common.h"
#include "core/export.h"
#include "core/format.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    char name[128];
    char path[512];
    double started;
    double interval;
    int samples;
    guint64 bytes;
} RecordingEntry;

typedef struct {
    App *app;
    ManagePage *ui;
    GtkWidget *toggle_button;
    GtkWidget *count_label;
    char filter[128];
    int selected;
    int recording;
    FILE *file;
    char path[512];
    double started;
    int samples;
    char status[256];
    GtkLabel *live_elapsed;
    GtkLabel *live_samples;
    RecordingEntry items[PRISM_MAX_RECORDINGS];
    int count;
} RecorderPage;

static void recorder_rebuild(RecorderPage *page, int keep);
static void recorder_open_folder(GtkButton *button, gpointer data);
static void recorder_delete(GtkButton *button, gpointer data);
static void recorder_start(RecorderPage *page);

static void recorder_dir(char *buf, size_t len) {
    char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                 "recordings", NULL);
    g_mkdir_with_parents(dir, 0700);
    g_strlcpy(buf, dir, len);
    g_free(dir);
}

static void recorder_stamp(double epoch, char *buf, size_t len) {
    time_t stamp = (time_t)epoch;
    struct tm local;
    localtime_r(&stamp, &local);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &local);
}

static void recorder_scan(RecorderPage *page) {
    page->count = 0;
    char dir_path[512];
    recorder_dir(dir_path, sizeof(dir_path));
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (dir == NULL) {
        return;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL && page->count < PRISM_MAX_RECORDINGS) {
        if (!g_str_has_suffix(name, ".tsv")) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
        struct stat info;
        if (stat(path, &info) != 0) {
            continue;
        }
        RecordingEntry *entry = &page->items[page->count];
        memset(entry, 0, sizeof(*entry));
        g_strlcpy(entry->name, name, sizeof(entry->name));
        g_strlcpy(entry->path, path, sizeof(entry->path));
        entry->bytes = (guint64)info.st_size;
        entry->started = (double)info.st_mtime;
        entry->interval = 1.0;
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            char line[256];
            int lines = 0;
            while (fgets(line, sizeof(line), file) != NULL && lines < 2000000) {
                if (strncmp(line, "# started=", 10) == 0) {
                    entry->started = strtod(line + 10, NULL);
                } else if (strncmp(line, "# interval=", 11) == 0) {
                    entry->interval = strtod(line + 11, NULL);
                }
                if (line[0] != '#') {
                    lines++;
                }
            }
            entry->samples = lines;
            fclose(file);
        }
        page->count++;
    }
    g_dir_close(dir);
}

static void recorder_rebuild(RecorderPage *page, int keep) {
    ManagePage *ui = page->ui;
    int previous = page->selected;
    recorder_scan(page);
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < page->count; i++) {
        RecordingEntry *entry = &page->items[i];
        if (page->filter[0] != '\0' && g_strrstr(entry->name, page->filter) == NULL) {
            continue;
        }
        char stamp[64];
        recorder_stamp(entry->started, stamp, sizeof(stamp));
        char duration[32];
        fmt_duration(entry->samples * entry->interval, duration, sizeof(duration));
        char subtitle[256];
        char size[32];
        fmt_bytes((double)entry->bytes, size, sizeof(size));
        snprintf(subtitle, sizeof(subtitle), "%s  \xc2\xb7  %s  \xc2\xb7  %s",
                 stamp, duration, size);
        char badge[48];
        snprintf(badge, sizeof(badge), "%d rows", entry->samples);
        manage_add_row(ui, i, entry->name, subtitle, badge, 4, 0);
        if (i == previous && keep) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d recordings%s%s", shown,
             page->filter[0] ? "  \xc2\xb7  filter " : "",
             page->filter[0] ? page->filter : "");
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0 && !page->recording) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else if (shown == 0) {
        page->selected = -1;
        manage_detail_clear(ui);
        manage_detail_heading(ui, page->recording ? "Recording in progress" : "No recordings yet");
        manage_detail_note(ui, page->recording
            ? "Live capture is running. Rows are flushed as they are sampled."
            : "Start a capture to record CPU, memory, disk, network, GPU, "
              "temperature and power telemetry into a timestamped TSV file.");
    }
}

static void recorder_update_toggle(RecorderPage *page) {
    if (page->toggle_button != NULL) {
        gtk_button_set_label(GTK_BUTTON(page->toggle_button),
                             page->recording ? "Stop recording" : "Start recording");
        if (page->recording) {
            gtk_widget_add_css_class(page->toggle_button, "destructive-action");
        } else {
            gtk_widget_remove_css_class(page->toggle_button, "destructive-action");
        }
    }
    if (page->count_label != NULL) {
        char text[160];
        snprintf(text, sizeof(text), "%s  \xc2\xb7  %d recordings",
                 page->recording ? "recording" : "idle", page->count);
        gtk_label_set_text(GTK_LABEL(page->count_label), text);
    }
}

static void recorder_live_detail(RecorderPage *page) {
    ManagePage *ui = page->ui;
    manage_detail_clear(ui);
    manage_detail_heading(ui, "Live recording");
    manage_detail_row(ui, "File", page->path);
    GtkWidget *row = manage_detail_row(ui, "Elapsed", "0s");
    page->live_elapsed = GTK_LABEL(gtk_widget_get_last_child(row));
    row = manage_detail_row(ui, "Samples", "0");
    page->live_samples = GTK_LABEL(gtk_widget_get_last_child(row));
    char interval[32];
    snprintf(interval, sizeof(interval), "%.2f s", app_interval(page->app));
    manage_detail_row(ui, "Interval", interval);
    manage_detail_note(ui, "The flight recorder writes one row per sample. "
                           "Stop the capture to inspect it from the recording "
                           "list. Files are plain TSV and stay on this machine.");
    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *stop = prism_button("Stop recording", "danger");
    gtk_box_append(GTK_BOX(actions), stop);
    g_signal_connect_swapped(stop, "clicked", G_CALLBACK(gtk_widget_activate),
                             page->toggle_button);
}

typedef struct {
    RecorderPage *page;
    int format; /* 0=csv, 1=json */
} RecorderExport;

static void recorder_export_done(GObject *source, GAsyncResult *result,
                                 gpointer data) {
    RecorderExport *exp = data;
    RecorderPage *page = exp->page;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        g_clear_error(&error);
        g_free(exp);
        return;
    }
    char *path = g_file_get_path(file);
    if (path != NULL && page->selected >= 0 &&
        page->selected < page->count) {
        char *text = prism_read_file(page->items[page->selected].path);
        if (text != NULL) {
            char *converted = exp->format == 0 ? export_tsv_to_csv(text)
                                               : export_tsv_to_json(text);
            FILE *f = fopen(path, "w");
            if (f != NULL) {
                fputs(converted, f);
                fclose(f);
                app_notify(page->app, "Exported to %s", path);
            } else {
                app_notify(page->app, "Cannot write %s", path);
            }
            g_free(converted);
            g_free(text);
        } else {
            app_notify(page->app, "Cannot read recording");
        }
        g_free(path);
    }
    g_object_unref(file);
    g_free(exp);
}

static void recorder_export(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        app_notify(page->app, "Select a recording first");
        return;
    }
    int format = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "format"));
    RecorderExport *exp = g_new0(RecorderExport, 1);
    exp->page = page;
    exp->format = format;
    char base[160];
    g_strlcpy(base, page->items[page->selected].name, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    char filename[192];
    snprintf(filename, sizeof(filename), "%s.%s", base,
             format == 0 ? "csv" : "json");
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, format == 0 ? "Export CSV" : "Export JSON");
    gtk_file_dialog_set_initial_name(dialog, filename);
    gtk_file_dialog_save(dialog, GTK_WINDOW(page->app->window), NULL,
                         recorder_export_done, exp);
    g_object_unref(dialog);
}

static void recorder_detail(RecorderPage *page, int index) {
    ManagePage *ui = page->ui;
    page->live_elapsed = NULL;
    page->live_samples = NULL;
    if (index < 0 || index >= page->count) {
        manage_detail_clear(ui);
        if (page->recording) {
            recorder_live_detail(page);
        } else {
            manage_detail_heading(ui, "Select a recording");
            manage_detail_note(ui, "Recordings are stored under the Prism data "
                                   "directory and can be opened in any "
                                   "spreadsheet or analysis tool.");
        }
        return;
    }
    const RecordingEntry *entry = &page->items[index];
    manage_detail_clear(ui);
    manage_detail_heading(ui, entry->name);
    char stamp[64];
    recorder_stamp(entry->started, stamp, sizeof(stamp));
    char duration[32];
    fmt_duration(entry->samples * entry->interval, duration, sizeof(duration));
    char size[32];
    fmt_bytes((double)entry->bytes, size, sizeof(size));
    char samples[32];
    snprintf(samples, sizeof(samples), "%d", entry->samples);
    char interval[32];
    snprintf(interval, sizeof(interval), "%.2f s", entry->interval);
    manage_detail_row(ui, "Started", stamp);
    manage_detail_row(ui, "Duration", duration);
    manage_detail_row(ui, "Samples", samples);
    manage_detail_row(ui, "Interval", interval);
    manage_detail_row(ui, "File size", size);
    manage_detail_row(ui, "Path", entry->path);
    {
        char *text = prism_read_file(entry->path);
        double cp = 0, ca = 0, mp = 0, ma = 0;
        int rows = 0;
        if (text != NULL && export_tsv_stats(text, &cp, &ca, &mp, &ma, &rows)) {
            char row[96];
            snprintf(row, sizeof(row), "CPU peak %.1f%% · avg %.1f%%", cp, ca);
            manage_detail_row(ui, "CPU", row);
            snprintf(row, sizeof(row), "Mem peak %.1f%% · avg %.1f%%", mp, ma);
            manage_detail_row(ui, "Memory", row);
        }
        g_free(text);
    }
    manage_detail_note(ui, "Columns: timestamp, CPU %, memory %, swap %, network "
                           "receive and transmit rates, disk read and write "
                           "rates, GPU %, CPU temperature, package power and "
                           "one-minute load.");

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *folder = prism_button("Show file", NULL);
    GtkWidget *remove = prism_button("Delete", "danger");
    GtkWidget *csv = prism_button("Export CSV", NULL);
    GtkWidget *json = prism_button("Export JSON", NULL);
    g_object_set_data(G_OBJECT(csv), "format", GINT_TO_POINTER(0));
    g_object_set_data(G_OBJECT(json), "format", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(actions), folder);
    gtk_box_append(GTK_BOX(actions), csv);
    gtk_box_append(GTK_BOX(actions), json);
    gtk_box_append(GTK_BOX(actions), remove);
    g_signal_connect(folder, "clicked", G_CALLBACK(recorder_open_folder), page);
    g_signal_connect(remove, "clicked", G_CALLBACK(recorder_delete), page);
    g_signal_connect(csv, "clicked", G_CALLBACK(recorder_export), page);
    g_signal_connect(json, "clicked", G_CALLBACK(recorder_export), page);
}

static void recorder_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    RecorderPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    recorder_detail(page, page->selected);
}

static void recorder_start(RecorderPage *page) {
    if (page->recording) {
        return;
    }
    char dir_path[512];
    recorder_dir(dir_path, sizeof(dir_path));
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char name[128];
    strftime(name, sizeof(name), "prism-%Y%m%d-%H%M%S.tsv", &local);
    snprintf(page->path, sizeof(page->path), "%s/%s", dir_path, name);
    page->file = fopen(page->path, "w");
    if (page->file == NULL) {
        app_notify(page->app, "Could not create a recording file");
        return;
    }
    fprintf(page->file, "# prism-monitor recording v1\n");
    fprintf(page->file, "# started=%.0f\n", (double)now);
    fprintf(page->file, "# interval=%.2f\n", app_interval(page->app));
    fprintf(page->file, "# timestamp\tcpu_pct\tmem_pct\tswap_pct\trx_bps\ttx_bps\t"
                        "disk_r_bps\tdisk_w_bps\tgpu_pct\tcpu_temp_c\tpower_w\tload1\n");
    fflush(page->file);
    page->recording = 1;
    page->started = (double)g_get_real_time() / 1000000.0;
    page->samples = 0;
    page->selected = -1;
    recorder_update_toggle(page);
    recorder_live_detail(page);
    app_notify(page->app, "Flight recorder started");
}

static void recorder_stop(RecorderPage *page) {
    if (!page->recording) {
        return;
    }
    if (page->file != NULL) {
        fflush(page->file);
        fclose(page->file);
        page->file = NULL;
    }
    page->recording = 0;
    page->live_elapsed = NULL;
    page->live_samples = NULL;
    recorder_update_toggle(page);
    recorder_rebuild(page, 0);
    app_notify(page->app, "Recording saved (%d samples)", page->samples);
}

static void recorder_toggle(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    if (page->recording) {
        recorder_stop(page);
    } else {
        recorder_start(page);
    }
}

static void recorder_open_folder(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    char dir_path[512];
    recorder_dir(dir_path, sizeof(dir_path));
    char *quoted = g_shell_quote(dir_path);
    char *command = g_strdup_printf("xdg-open %s", quoted);
    g_spawn_command_line_async(command, NULL);
    g_free(command);
    g_free(quoted);
}

static void recorder_delete_confirmed(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        return;
    }
    unlink(page->items[page->selected].path);
    app_notify(page->app, "Recording deleted");
    page->selected = -1;
    recorder_rebuild(page, 0);
}

static void recorder_delete(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        return;
    }
    char message[512];
    snprintf(message, sizeof(message),
             "Delete %s? The recording is removed from disk immediately.",
             page->items[page->selected].name);
    manage_confirm_dialog(page->app, "Delete recording", message, "Delete",
                          G_CALLBACK(recorder_delete_confirmed), page);
}

static void recorder_filter(GtkSearchEntry *entry, gpointer data) {
    RecorderPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    recorder_rebuild(page, 0);
}

static void recorder_refresh(GtkButton *button, gpointer data) {
    (void)button;
    RecorderPage *page = data;
    recorder_rebuild(page, 1);
    app_notify(page->app, "Recording library refreshed");
}

void page_recorder_capture(App *app) {
    RecorderPage *page = g_object_get_data(G_OBJECT(app->pages[PAGE_RECORDER].root),
                                           "recorder-page");
    if (page == NULL || !page->recording || page->file == NULL) {
        return;
    }
    const Snapshot *snap = app->view;
    double gpu = 0;
    if (snap->gpu_count > 0) {
        gpu = snap->gpus[0].util_pct;
    }
    double rx = 0;
    double tx = 0;
    double dr = 0;
    double dw = 0;
    for (int i = 0; i < snap->disk_count; i++) {
        dr += snap->disks[i].read_bps;
        dw += snap->disks[i].write_bps;
    }
    rx = snap->net_rx_bps;
    tx = snap->net_tx_bps;
    fprintf(page->file,
            "%.3f\t%.2f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.0f\t%.2f\t%.1f\t%.2f\t%.2f\n",
            (double)snap->timestamp_us / 1000000.0,
            snap->cpu.usage_pct, snap->mem.used_pct, snap->mem.swap_pct,
            rx, tx, dr, dw, gpu, snap->cpu.temp_c,
            snap->cpu.package_power_w, snap->cpu.load1);
    fflush(page->file);
    page->samples++;

    if (page->live_elapsed != NULL) {
        char text[64];
        fmt_duration((double)g_get_real_time() / 1000000.0 - page->started,
                     text, sizeof(text));
        gtk_label_set_text(page->live_elapsed, text);
    }
    if (page->live_samples != NULL) {
        char text[32];
        snprintf(text, sizeof(text), "%d", page->samples);
        gtk_label_set_text(page->live_samples, text);
    }
}

GtkWidget *page_recorder_create(App *app) {
    RecorderPage *page = g_new0(RecorderPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter recordings...", 410);
    page->toggle_button = manage_toolbar_button(page->ui, "Start recording", NULL,
                                                G_CALLBACK(recorder_toggle), page);
    gtk_widget_add_css_class(page->toggle_button, "suggested-action");
    manage_toolbar_button(page->ui, "Show folder", NULL,
                          G_CALLBACK(recorder_open_folder), page);
    manage_toolbar_button(page->ui, "Refresh", NULL,
                          G_CALLBACK(recorder_refresh), page);
    page->count_label = page->ui->count_label;
    g_signal_connect(page->ui->search, "search-changed",
                     G_CALLBACK(recorder_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected",
                     G_CALLBACK(recorder_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "recorder-page", page, g_free);
    app->pages[PAGE_RECORDER].root = page->ui->root;
    recorder_rebuild(page, 0);
    recorder_update_toggle(page);
    if (app->config.auto_record) {
        recorder_start(page);
    }
    return page->ui->root;
}

void page_recorder_update(App *app) {
    RecorderPage *page = g_object_get_data(G_OBJECT(app->pages[PAGE_RECORDER].root),
                                           "recorder-page");
    if (page == NULL) {
        return;
    }
    if (page->recording) {
        recorder_update_toggle(page);
    }
}
