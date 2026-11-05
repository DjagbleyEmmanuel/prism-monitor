#include "core/common.h"
#include "core/export.h"
#include "core/history.h"
#include "core/sampler.h"
#include "ui/app.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_usage(const char *prog) {
    printf("Prism Monitor %s\n", PRISM_VERSION);
    printf("Fast native system monitor for Linux (GTK 4 / Cairo)\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --help                 Show this help\n");
    printf("  --version              Show version\n");
    printf("  --page NAME            Start on a page: overview, cpu, memory, gpu,\n");
    printf("                         storage, network, processes, sensors, system,\n");
    printf("                         services, startup, connections, apps, drivers,\n");
    printf("                         diskspace, benchmarks, power, recorder\n");
    printf("  --interval SECONDS     Sampling interval (0.25-10, default from config)\n");
    printf("  --paused               Start with sampling paused (freeze charts)\n");
    printf("  --duration SECONDS     How long to run headless modes (default 2)\n");
    printf("  --snapshot [FILE]      Export one JSON snapshot and exit.\n");
    printf("                         FILE \"-\" or omitted = stdout. Example:\n");
    printf("                         prism-monitor --snapshot /tmp/snap.json --duration 2\n");
    printf("  --record FILE          Record TSV telemetry for --duration seconds and exit\n");
    printf("\n");
    printf("Shortcuts in the GUI: Ctrl+K palette, Ctrl+P pause/resume,\n");
    printf("Ctrl+E export snapshot JSON, Ctrl+1..9 pages, Ctrl+B sidebar.\n");
}

static int run_headless_snapshot(const char *path, double interval,
                                 double duration) {
    if (interval <= 0) {
        interval = 1.0;
    }
    if (duration <= 0) {
        duration = 2.0;
    }
    Sampler *sampler = sampler_new(interval);
    sampler_start(sampler);
    Snapshot *snap = snapshot_new();
    History hist;
    history_clear(&hist);
    guint64 seq = 0;
    gint64 deadline = g_get_monotonic_time() + (gint64)(duration * G_USEC_PER_SEC);
    while (g_get_monotonic_time() < deadline) {
        sampler_copy(sampler, snap, &hist, &seq);
        g_usleep(100 * 1000);
    }
    sampler_copy(sampler, snap, &hist, NULL);
    gboolean ok = export_snapshot_json_file(path, snap, &hist);
    snapshot_free(snap);
    sampler_free(sampler);
    if (!ok) {
        fprintf(stderr, "prism-monitor: cannot write snapshot to %s\n",
                path != NULL ? path : "-");
        return 1;
    }
    return 0;
}

static int run_headless_record(const char *path, double interval,
                               double duration) {
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "prism-monitor: --record needs a file path\n");
        return 1;
    }
    if (interval <= 0) {
        interval = 1.0;
    }
    if (duration <= 0) {
        duration = 10.0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "prism-monitor: cannot create %s\n", path);
        return 1;
    }
    time_t now = time(NULL);
    fprintf(file, "# prism-monitor recording v1\n");
    fprintf(file, "# started=%.0f\n", (double)now);
    fprintf(file, "# interval=%.2f\n", interval);
    fprintf(file, "# timestamp\tcpu_pct\tmem_pct\tswap_pct\trx_bps\ttx_bps\t"
                  "disk_r_bps\tdisk_w_bps\tgpu_pct\tcpu_temp_c\tpower_w\tload1\n");
    Sampler *sampler = sampler_new(interval);
    sampler_start(sampler);
    Snapshot *snap = snapshot_new();
    History hist;
    history_clear(&hist);
    guint64 seq = 0;
    gint64 deadline = g_get_monotonic_time() + (gint64)(duration * G_USEC_PER_SEC);
    int rows = 0;
    while (g_get_monotonic_time() < deadline) {
        if (sampler_copy(sampler, snap, &hist, &seq)) {
            double gpu = snap->gpu_count > 0 ? snap->gpus[0].util_pct : 0;
            double dr = 0, dw = 0;
            for (int i = 0; i < snap->disk_count; i++) {
                dr += snap->disks[i].read_bps;
                dw += snap->disks[i].write_bps;
            }
            fprintf(file, "%.3f\t%.2f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.0f\t"
                          "%.2f\t%.1f\t%.2f\t%.2f\n",
                    (double)snap->timestamp_us / 1000000.0,
                    snap->cpu.usage_pct, snap->mem.used_pct,
                    snap->mem.swap_pct, snap->net_rx_bps, snap->net_tx_bps,
                    dr, dw, gpu, snap->cpu.temp_c, snap->cpu.package_power_w,
                    snap->cpu.load1);
            fflush(file);
            rows++;
        }
        g_usleep(100 * 1000);
    }
    fclose(file);
    snapshot_free(snap);
    sampler_free(sampler);
    fprintf(stderr, "prism-monitor: recorded %d samples to %s\n", rows, path);
    return 0;
}

static void prism_activate(GtkApplication *application, gpointer data) {
    (void)data;
    if (g_object_get_data(G_OBJECT(application), "prism-app") != NULL) {
        return;
    }
    App *app = app_create(application);
    g_object_set_data_full(G_OBJECT(application), "prism-app", app,
                           (GDestroyNotify)app_destroy);
}

static void prism_shutdown(GtkApplication *application, gpointer data) {
    (void)application;
    (void)data;
}

int main(int argc, char **argv) {
    g_set_application_name(PRISM_APP_NAME);

    const char *prog = argc > 0 ? argv[0] : "prism-monitor";
    char *opt_page = NULL;
    double opt_interval = 0;
    double opt_duration = 0;
    char *opt_snapshot = NULL;
    char *opt_record = NULL;
    gboolean opt_paused = FALSE;
    gboolean opt_snapshot_set = FALSE;

    char **gui_argv = g_new0(char *, argc + 1);
    int gui_argc = 0;
    gui_argv[gui_argc++] = argv[0];

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(prog);
            return 0;
        } else if (strcmp(arg, "--version") == 0 ||
                   strcmp(arg, "-V") == 0) {
            printf("Prism Monitor %s\n", PRISM_VERSION);
            return 0;
        } else if (strncmp(arg, "--page=", 7) == 0) {
            opt_page = g_strdup(arg + 7);
        } else if (strcmp(arg, "--page") == 0 && i + 1 < argc) {
            opt_page = g_strdup(argv[++i]);
        } else if (strncmp(arg, "--interval=", 11) == 0) {
            opt_interval = strtod(arg + 11, NULL);
        } else if (strcmp(arg, "--interval") == 0 && i + 1 < argc) {
            opt_interval = strtod(argv[++i], NULL);
        } else if (strncmp(arg, "--duration=", 11) == 0) {
            opt_duration = strtod(arg + 11, NULL);
        } else if (strcmp(arg, "--duration") == 0 && i + 1 < argc) {
            opt_duration = strtod(argv[++i], NULL);
        } else if (strncmp(arg, "--snapshot=", 11) == 0) {
            opt_snapshot = g_strdup(arg + 11);
            opt_snapshot_set = TRUE;
        } else if (strcmp(arg, "--snapshot") == 0) {
            opt_snapshot_set = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opt_snapshot = g_strdup(argv[++i]);
            } else {
                opt_snapshot = g_strdup("-");
            }
        } else if (strncmp(arg, "--snapshot-file=", 16) == 0) {
            opt_snapshot = g_strdup(arg + 16);
            opt_snapshot_set = TRUE;
        } else if (strncmp(arg, "--record=", 9) == 0) {
            opt_record = g_strdup(arg + 9);
        } else if (strcmp(arg, "--record") == 0 && i + 1 < argc) {
            opt_record = g_strdup(argv[++i]);
        } else if (strcmp(arg, "--paused") == 0 ||
                   strcmp(arg, "--start-paused") == 0) {
            opt_paused = TRUE;
        } else {
            gui_argv[gui_argc++] = argv[i];
        }
    }

    if (opt_snapshot_set) {
        int status = run_headless_snapshot(
            opt_snapshot != NULL ? opt_snapshot : "-",
            opt_interval, opt_duration > 0 ? opt_duration : 2.0);
        g_free(opt_page);
        g_free(opt_snapshot);
        g_free(opt_record);
        g_free(gui_argv);
        return status;
    }
    if (opt_record != NULL) {
        int status = run_headless_record(opt_record, opt_interval,
                                         opt_duration > 0 ? opt_duration : 10.0);
        g_free(opt_page);
        g_free(opt_snapshot);
        g_free(opt_record);
        g_free(gui_argv);
        return status;
    }

    app_configure_startup(opt_page, opt_interval, opt_paused);

    GtkApplication *application = gtk_application_new(PRISM_APP_ID,
                                                      G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(prism_activate), NULL);
    g_signal_connect(application, "shutdown", G_CALLBACK(prism_shutdown), NULL);
    int status = g_application_run(G_APPLICATION(application), gui_argc, gui_argv);
    g_object_unref(application);
    g_free(opt_page);
    g_free(opt_snapshot);
    g_free(opt_record);
    g_free(gui_argv);
    return status;
}
