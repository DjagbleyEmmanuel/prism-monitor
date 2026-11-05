#include "core/common.h"
#include "sys/systeminfo.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    BENCH_CPU_SINGLE,
    BENCH_CPU_MULTI,
    BENCH_GPU,
    BENCH_DISK_WRITE,
    BENCH_DISK_READ,
    BENCH_INTERNET,
    BENCH_KIND_COUNT
} BenchKind;

typedef struct {
    int kind;
    double time;
    double value;
    char unit[16];
} BenchHistory;

typedef struct {
    int valid;
    double primary;
    double secondary;
    char unit[16];
    char detail[192];
} BenchOutput;

typedef struct {
    App *app;
    ManagePage *ui;
    int selected;
    int running;
    double results[BENCH_KIND_COUNT];
    char units[BENCH_KIND_COUNT][16];
    char details[BENCH_KIND_COUNT][192];
    int has_result[BENCH_KIND_COUNT];
    BenchHistory history[256];
    int history_count;
    GQueue queue;
    int queue_active;
    int current_kind;
} BenchPage;

typedef struct {
    BenchPage *page;
    int kind;
} BenchJob;

static const char *bench_names[BENCH_KIND_COUNT] = {
    "CPU single-core",
    "CPU multi-core",
    "GPU renderer",
    "Disk write",
    "Disk read",
    "Internet"
};

static const char *bench_method[BENCH_KIND_COUNT] = {
    "A fixed-work integer loop runs for 0.45 seconds on one thread. The score is "
    "millions of dependent operations per second.",
    "The same workload runs on every available hardware thread for 0.45 seconds. "
    "The score is aggregate throughput.",
    "Cairo fills an offscreen surface with gradients and geometry for 0.35 seconds. "
    "The score is millions of pixels rasterized per second.",
    "A 32 MiB file is written, flushed to stable storage and timed. The score is "
    "megabytes per second.",
    "The same 32 MiB file is read back with buffered reads and timed. The score is "
    "megabytes per second.",
    "A 1 MiB transfer is requested over HTTPS and timed. The score is megabits per "
    "second; it falls back to connection latency when curl is unavailable."
};

static void bench_history_path(char *buf, size_t len) {
    char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor", NULL);
    g_mkdir_with_parents(dir, 0700);
    snprintf(buf, len, "%s/benchmarks.tsv", dir);
    g_free(dir);
}

static void bench_history_load(BenchPage *page) {
    char path[512];
    bench_history_path(path, sizeof(path));
    char *text = prism_read_file(path);
    if (text == NULL) {
        return;
    }
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line != NULL && page->history_count < 256) {
        int kind = 0;
        double stamp = 0;
        double value = 0;
        char unit[16] = "";
        if (sscanf(line, "%d\t%lf\t%lf\t%15s", &kind, &stamp, &value, unit) == 4 &&
            kind >= 0 && kind < BENCH_KIND_COUNT) {
            BenchHistory *entry = &page->history[page->history_count++];
            entry->kind = kind;
            entry->time = stamp;
            entry->value = value;
            g_strlcpy(entry->unit, unit, sizeof(entry->unit));
            if (stamp > page->results[kind] || !page->has_result[kind]) {
                page->results[kind] = value;
                g_strlcpy(page->units[kind], unit, sizeof(page->units[kind]));
                page->has_result[kind] = 1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(text);
}

static void bench_history_append(BenchPage *page, int kind, double value,
                                 const char *unit) {
    if (page->history_count < 256) {
        BenchHistory *entry = &page->history[page->history_count++];
        entry->kind = kind;
        entry->time = (double)g_get_real_time() / 1000000.0;
        entry->value = value;
        g_strlcpy(entry->unit, unit, sizeof(entry->unit));
    }
    char path[512];
    bench_history_path(path, sizeof(path));
    FILE *file = fopen(path, "a");
    if (file != NULL) {
        fprintf(file, "%d\t%.0f\t%.3f\t%s\n", kind, (double)g_get_real_time() / 1000000.0,
                value, unit);
        fclose(file);
    }
}

static double bench_cpu_worker(double seconds) {
    gint64 start = g_get_monotonic_time();
    guint64 ops = 0;
    guint64 value = 0x9e3779b97f4a7c15ULL;
    double elapsed = 0;
    do {
        for (int i = 0; i < 20000; i++) {
            value = value * 1664525ULL + 1013904223ULL;
            value ^= value >> 17;
            ops++;
        }
        elapsed = (double)(g_get_monotonic_time() - start) / G_USEC_PER_SEC;
    } while (elapsed < seconds);
    volatile guint64 sink = value;
    (void)sink;
    return (double)ops / 1000000.0 / (elapsed > 0 ? elapsed : 1.0);
}

typedef struct {
    double seconds;
    double result;
} CpuThreadArgs;

static gpointer cpu_thread(gpointer data) {
    CpuThreadArgs *args = data;
    args->result = bench_cpu_worker(args->seconds);
    return NULL;
}

static BenchOutput bench_cpu_multi(void) {
    BenchOutput out = {0};
    int threads = (int)g_get_num_processors();
    if (threads > 16) {
        threads = 16;
    }
    if (threads < 1) {
        threads = 1;
    }
    GThread *workers[16];
    CpuThreadArgs args[16];
    for (int i = 0; i < threads; i++) {
        args[i].seconds = 0.45;
        args[i].result = 0;
        workers[i] = g_thread_new("prism-bench", cpu_thread, &args[i]);
    }
    double total = 0;
    for (int i = 0; i < threads; i++) {
        g_thread_join(workers[i]);
        total += args[i].result;
    }
    out.valid = 1;
    out.primary = total;
    g_strlcpy(out.unit, "Mops/s", sizeof(out.unit));
    snprintf(out.detail, sizeof(out.detail), "%d threads", threads);
    return out;
}

static BenchOutput bench_gpu(void) {
    BenchOutput out = {0};
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 900, 900);
    cairo_t *cr = cairo_create(surface);
    gint64 start = g_get_monotonic_time();
    gint64 deadline = start + 350000;
    int frames = 0;
    while (g_get_monotonic_time() < deadline) {
        cairo_set_source_rgba(cr, 0.05, 0.08, 0.14, 1);
        cairo_paint(cr);
        for (int i = 0; i < 24; i++) {
            double x = (i * 73 + frames * 11) % 900;
            double y = (i * 41 + frames * 17) % 900;
            cairo_pattern_t *grad = cairo_pattern_create_radial(
                x, y, 2, x, y, 130);
            cairo_pattern_add_color_stop_rgba(grad, 0, 0.2, 0.8, 1.0, 0.8);
            cairo_pattern_add_color_stop_rgba(grad, 1, 0.6, 0.3, 1.0, 0.0);
            cairo_set_source(cr, grad);
            cairo_new_path(cr);
            cairo_arc(cr, x, y, 130, 0, 2 * 3.141592653589793);
            cairo_fill(cr);
            cairo_pattern_destroy(grad);
        }
        cairo_surface_flush(surface);
        frames++;
    }
    double elapsed = (double)(g_get_monotonic_time() - start) / G_USEC_PER_SEC;
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    out.valid = 1;
    out.primary = frames * 900.0 * 900.0 / 1000000.0 / (elapsed > 0 ? elapsed : 1);
    g_strlcpy(out.unit, "Mpix/s", sizeof(out.unit));
    snprintf(out.detail, sizeof(out.detail), "%d frames rasterized", frames);
    return out;
}

static BenchOutput bench_disk(int write_test) {
    BenchOutput out = {0};
    char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor", "bench", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "prism-bench.tmp", NULL);
    const size_t chunk = 1 << 20;
    const int chunks = 32;
    char *buffer = g_malloc(chunk);
    memset(buffer, 0x5a, chunk);
    if (write_test) {
        FILE *file = fopen(path, "wb");
        if (file == NULL) {
            g_free(buffer);
            g_free(path);
            g_free(dir);
            return out;
        }
        gint64 start = g_get_monotonic_time();
        for (int i = 0; i < chunks; i++) {
            fwrite(buffer, 1, chunk, file);
        }
        fflush(file);
        fsync(fileno(file));
        double elapsed = (double)(g_get_monotonic_time() - start) / G_USEC_PER_SEC;
        fclose(file);
        out.valid = elapsed > 0.001;
        out.primary = (double)chunks / (elapsed > 0 ? elapsed : 1);
        g_strlcpy(out.unit, "MB/s", sizeof(out.unit));
        snprintf(out.detail, sizeof(out.detail), "%d MiB written and flushed", chunks);
    } else {
        FILE *file = fopen(path, "rb");
        if (file == NULL) {
            g_free(buffer);
            g_free(path);
            g_free(dir);
            return out;
        }
        gint64 start = g_get_monotonic_time();
        size_t read_total = 0;
        size_t got = 0;
        while ((got = fread(buffer, 1, chunk, file)) > 0) {
            read_total += got;
        }
        double elapsed = (double)(g_get_monotonic_time() - start) / G_USEC_PER_SEC;
        fclose(file);
        out.valid = elapsed > 0.001;
        out.primary = (double)read_total / 1048576.0 / (elapsed > 0 ? elapsed : 1);
        g_strlcpy(out.unit, "MB/s", sizeof(out.unit));
        snprintf(out.detail, sizeof(out.detail), "%.0f MiB read",
                 read_total / 1048576.0);
    }
    unlink(path);
    g_free(buffer);
    g_free(path);
    g_free(dir);
    return out;
}

static BenchOutput bench_internet(void) {
    BenchOutput out = {0};
    char *curl = g_find_program_in_path("curl");
    if (curl != NULL) {
        const char *command =
            "curl -L --max-time 12 -s -o /dev/null -w '%{speed_download}' "
            "https://speed.cloudflare.com/__down?bytes=2000000";
        gchar *stdout_text = NULL;
        gint status = 0;
        if (g_spawn_command_line_sync(command, &stdout_text, NULL, &status, NULL) &&
            status == 0 && stdout_text != NULL) {
            double bytes = strtod(stdout_text, NULL);
            if (bytes > 0) {
                out.valid = 1;
                out.primary = bytes * 8.0 / 1000000.0;
                g_strlcpy(out.unit, "Mbit/s", sizeof(out.unit));
                snprintf(out.detail, sizeof(out.detail), "2 MB HTTPS transfer");
            }
        }
        g_free(stdout_text);
    }
    g_free(curl);
    if (!out.valid) {
        gint64 start = g_get_monotonic_time();
        GResolver *resolver = g_resolver_get_default();
        GError *error = NULL;
        GList *addresses = g_resolver_lookup_by_name(resolver, "one.one.one.one",
                                                     NULL, &error);
        double elapsed = (double)(g_get_monotonic_time() - start) / 1000.0;
        if (addresses != NULL) {
            out.valid = 1;
            out.primary = elapsed;
            g_strlcpy(out.unit, "ms", sizeof(out.unit));
            snprintf(out.detail, sizeof(out.detail), "DNS lookup latency");
            g_resolver_free_addresses(addresses);
        } else {
            snprintf(out.detail, sizeof(out.detail), "network unavailable");
            g_clear_error(&error);
        }
        g_object_unref(resolver);
    }
    return out;
}

static void bench_thread(GTask *task, gpointer source, gpointer data,
                         GCancellable *cancellable) {
    (void)source;
    (void)data;
    (void)cancellable;
    int kind = GPOINTER_TO_INT(g_task_get_task_data(task));
    BenchOutput *out = g_new0(BenchOutput, 1);
    switch (kind) {
        case BENCH_CPU_SINGLE:
            out->valid = 1;
            out->primary = bench_cpu_worker(0.45);
            g_strlcpy(out->unit, "Mops/s", sizeof(out->unit));
            snprintf(out->detail, sizeof(out->detail), "single thread");
            break;
        case BENCH_CPU_MULTI:
            *out = bench_cpu_multi();
            break;
        case BENCH_GPU:
            *out = bench_gpu();
            break;
        case BENCH_DISK_WRITE:
            *out = bench_disk(1);
            break;
        case BENCH_DISK_READ:
            *out = bench_disk(0);
            break;
        case BENCH_INTERNET:
            *out = bench_internet();
            break;
        default:
            break;
    }
    g_task_return_pointer(task, out, g_free);
}

static void bench_start_next(BenchPage *page);
static void bench_run_selected(GtkButton *button, gpointer data);
static void bench_run_all(GtkButton *button, gpointer data);

static void bench_row_refresh(BenchPage *page, int keep) {
    ManagePage *ui = page->ui;
    int previous = page->selected;
    manage_list_clear(ui);
    int selected_row = -1;
    for (int kind = 0; kind < BENCH_KIND_COUNT; kind++) {
        char badge[48];
        if (page->has_result[kind]) {
            snprintf(badge, sizeof(badge), "%.1f %s", page->results[kind],
                     page->units[kind]);
        } else {
            snprintf(badge, sizeof(badge), "not run");
        }
        manage_add_row(ui, kind, bench_names[kind],
                       page->has_result[kind] && page->details[kind][0]
                           ? page->details[kind] : "Run to record a baseline",
                       badge, page->has_result[kind] ? 4 : 0, 0);
        if (kind == previous && keep) {
            selected_row = kind;
        }
    }
    char text[128];
    snprintf(text, sizeof(text), "%d tests  \xc2\xb7  %d history samples",
             BENCH_KIND_COUNT, page->history_count);
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (page->selected < 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    }
}

static void bench_detail(BenchPage *page, int kind) {
    ManagePage *ui = page->ui;
    manage_detail_clear(ui);
    if (kind < 0 || kind >= BENCH_KIND_COUNT) {
        manage_detail_heading(ui, "Select a benchmark");
        manage_detail_note(ui, "Benchmarks run in a background worker so the monitor "
                               "stays responsive.");
        return;
    }
    manage_detail_heading(ui, bench_names[kind]);
    if (page->has_result[kind]) {
        char value[64];
        snprintf(value, sizeof(value), "%.2f %s", page->results[kind],
                 page->units[kind]);
        manage_detail_row(ui, "Last result", value);
        manage_detail_row(ui, "Details", page->details[kind]);
    } else {
        manage_detail_row(ui, "Last result", "not run");
    }
    manage_detail_note(ui, bench_method[kind]);

    manage_detail_heading(ui, "Recent history");
    int shown = 0;
    for (int i = page->history_count - 1; i >= 0 && shown < 10; i--) {
        if (page->history[i].kind != kind) {
            continue;
        }
        char label[48];
        time_t stamp = (time_t)page->history[i].time;
        struct tm local;
        localtime_r(&stamp, &local);
        strftime(label, sizeof(label), "%Y-%m-%d %H:%M", &local);
        char value[64];
        snprintf(value, sizeof(value), "%.2f %s", page->history[i].value,
                 page->history[i].unit);
        manage_detail_row(ui, label, value);
        shown++;
    }
    if (shown == 0) {
        manage_detail_note(ui, "No recorded runs yet.");
    }

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *run = prism_button(page->running ? "Running..." : "Run benchmark", NULL);
    GtkWidget *all = prism_button("Run all", NULL);
    gtk_widget_set_sensitive(run, !page->running);
    gtk_widget_set_sensitive(all, !page->running);
    gtk_box_append(GTK_BOX(actions), run);
    gtk_box_append(GTK_BOX(actions), all);
    g_signal_connect(run, "clicked", G_CALLBACK(bench_run_selected), page);
    g_signal_connect(all, "clicked", G_CALLBACK(bench_run_all), page);
}

static void bench_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    BenchPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    bench_detail(page, page->selected);
}

static void bench_finished(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    BenchPage *page = data;
    BenchOutput *out = g_task_propagate_pointer(G_TASK(result), NULL);
    int kind = page->current_kind;
    if (out != NULL && out->valid && kind >= 0 && kind < BENCH_KIND_COUNT) {
        page->results[kind] = out->primary;
        g_strlcpy(page->units[kind], out->unit, sizeof(page->units[kind]));
        g_strlcpy(page->details[kind], out->detail, sizeof(page->details[kind]));
        page->has_result[kind] = 1;
        bench_history_append(page, kind, out->primary, out->unit);
        app_notify(page->app, "%s: %.2f %s", bench_names[kind], out->primary, out->unit);
    } else if (out != NULL) {
        app_notify(page->app, "%s could not complete", bench_names[kind]);
    }
    g_free(out);
    page->running = 0;
    bench_row_refresh(page, 1);
    if (page->selected >= 0) {
        bench_detail(page, page->selected);
    }
    bench_start_next(page);
}

static void bench_start_kind(BenchPage *page, int kind) {
    page->running = 1;
    page->current_kind = kind;
    bench_row_refresh(page, 1);
    if (page->selected >= 0) {
        bench_detail(page, page->selected);
    }
    GTask *task = g_task_new(NULL, NULL, bench_finished, page);
    g_task_set_task_data(task, GINT_TO_POINTER(kind), NULL);
    g_object_set_data(G_OBJECT(task), "bench-kind", GINT_TO_POINTER(kind));
    g_task_run_in_thread(task, bench_thread);
    g_object_unref(task);
}

static void bench_start_next(BenchPage *page) {
    if (page->running) {
        return;
    }
    gpointer item = g_queue_pop_head(&page->queue);
    if (item == NULL) {
        page->queue_active = 0;
        return;
    }
    page->queue_active = 1;
    bench_start_kind(page, GPOINTER_TO_INT(item) - 1);
}

static void bench_run_selected(GtkButton *button, gpointer data) {
    BenchPage *page = data;
    if (page->running || page->selected < 0) {
        return;
    }
    bench_start_kind(page, page->selected);
}

static void bench_run_all(GtkButton *button, gpointer data) {
    (void)button;
    BenchPage *page = data;
    if (page->running) {
        return;
    }
    g_queue_clear(&page->queue);
    for (int kind = 0; kind < BENCH_KIND_COUNT; kind++) {
        g_queue_push_tail(&page->queue, GINT_TO_POINTER(kind + 1));
    }
    page->queue_active = 1;
    bench_start_next(page);
}

GtkWidget *page_benchmarks_create(App *app) {
    BenchPage *page = g_new0(BenchPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter benchmarks...", 390);
    g_queue_init(&page->queue);
    bench_history_load(page);

    manage_toolbar_button(page->ui, "Run all", NULL, G_CALLBACK(bench_run_all), page);
    g_signal_connect(page->ui->listbox, "row-selected", G_CALLBACK(bench_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "bench-page", page, g_free);
    bench_row_refresh(page, 0);
    app->pages[PAGE_BENCHMARKS].root = page->ui->root;
    return page->ui->root;
}

void page_benchmarks_update(App *app) {
    (void)app;
}
