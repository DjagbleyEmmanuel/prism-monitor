#include "core/common.h"
#include "core/export.h"
#include "core/i18n.h"
#include "core/history.h"
#include "core/sampler.h"
#include "ui/app.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
    printf("  --daemon               Fork to background, write history + pidfile, exit\n");
    printf("  --exporter PORT        Serve Prometheus metrics on localhost:PORT (--duration secs, 0=forever)\n");
    printf("  --tray                 Start in tray mode (close minimizes)\n");
    printf("  --minimized            Start minimized\n");
    printf("  --no-tray              Disable tray mode and restore window\n");
    printf("  --quit                 Quit the running instance (recovery)\n");
    printf("  --diagnostics          Write diagnostics bundle and exit\n");
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

static int run_daemon(double interval) {
    if (interval <= 0) {
        interval = 1.0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        return 1;
    }
    if (pid > 0) {
        fprintf(stderr, "prism-monitor: daemon pid %d\n", (int)pid);
        return 0;
    }
    if (setsid() < 0) {
        _exit(1);
    }
    /* Redirect stdio to log */
    char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *logpath = g_build_filename(dir, "daemon.log", NULL);
    FILE *log = freopen(logpath, "a", "w");
    (void)log;
    char *pidpath = g_build_filename(dir, "daemon.pid", NULL);
    FILE *pf = fopen(pidpath, "w");
    if (pf != NULL) {
        fprintf(pf, "%d\n", (int)getpid());
        fclose(pf);
    }
    g_free(pidpath);
    g_free(logpath);
    g_free(dir);
    Sampler *sampler = sampler_new(interval);
    sampler_load_history(sampler);
    sampler_start(sampler);
    Snapshot *snap = snapshot_new();
    History hist;
    history_clear(&hist);
    guint64 seq = 0;
    while (TRUE) {
        if (sampler_copy(sampler, snap, &hist, &seq)) {
            history_append(snap);
        }
        g_usleep(1000 * 1000);
    }
    return 0;
}

static int run_exporter(int port, double interval, double duration) {
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "prism-monitor: --exporter needs port 1-65535\n");
        return 1;
    }
    if (interval <= 0) {
        interval = 5.0;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        return 1;
    }
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }
    if (listen(listener, 5) < 0) {
        close(listener);
        return 1;
    }
    fprintf(stderr, "prism-monitor: Prometheus exporter on 127.0.0.1:%d\n", port);
    Sampler *sampler = sampler_new(interval);
    sampler_start(sampler);
    Snapshot *snap = snapshot_new();
    History hist;
    history_clear(&hist);
    gint64 deadline = duration > 0
        ? g_get_monotonic_time() + (gint64)(duration * G_USEC_PER_SEC) : 0;
    while (TRUE) {
        if (deadline > 0 && g_get_monotonic_time() > deadline) {
            break;
        }
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(listener, (struct sockaddr *)&cli, &clen);
        if (fd < 0) {
            continue;
        }
        sampler_copy(sampler, snap, &hist, NULL);
        char *metrics = export_snapshot_prometheus(snap);
        char header[256];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\n"
                 "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                 strlen(metrics));
        send(fd, header, strlen(header), 0);
        send(fd, metrics, strlen(metrics), 0);
        g_free(metrics);
        close(fd);
    }
    close(listener);
    snapshot_free(snap);
    sampler_free(sampler);
    return 0;
}

static int run_diagnostics(double interval) {
    if (interval <= 0) {
        interval = 1.0;
    }
    Sampler *sampler = sampler_new(interval);
    sampler_start(sampler);
    Snapshot *snap = snapshot_new();
    History hist;
    history_clear(&hist);
    guint64 seq = 0;
    gint64 deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < deadline) {
        sampler_copy(sampler, snap, &hist, &seq);
        g_usleep(100 * 1000);
    }
    sampler_copy(sampler, snap, &hist, NULL);
    /* config path via a dummy load */
    char *bundle = export_diagnostics_bundle(snap, &hist, NULL);
    if (bundle != NULL) {
        printf("%s\n", bundle);
        g_free(bundle);
        snapshot_free(snap);
        sampler_free(sampler);
        return 0;
    }
    snapshot_free(snap);
    sampler_free(sampler);
    return 1;
}

static gboolean startup_quit = FALSE;
static gboolean startup_no_tray = FALSE;

static void prism_activate(GtkApplication *application, gpointer data) {
    (void)data;
    App *existing = g_object_get_data(G_OBJECT(application), "prism-app");
    if (existing != NULL) {
        /* Second launch while first is hidden in tray: restore it.
         * This is the recovery path when the tray icon is not visible. */
        if (startup_quit) {
            g_application_quit(G_APPLICATION(application));
            return;
        }
        if (startup_no_tray) {
            existing->config.tray_enabled = 0;
            app_save_config(existing);
        }
        gtk_widget_set_visible(GTK_WIDGET(existing->window), TRUE);
        gtk_window_present(existing->window);
        return;
    }
    App *app = app_create(application);
    g_object_set_data_full(G_OBJECT(application), "prism-app", app,
                           (GDestroyNotify)app_destroy);
    if (startup_no_tray) {
        app->config.tray_enabled = 0;
        app_save_config(app);
    }
    if (startup_quit) {
        g_application_quit(G_APPLICATION(application));
    }
}

static void prism_shutdown(GtkApplication *application, gpointer data) {
    (void)application;
    (void)data;
}

int main(int argc, char **argv) {
    prism_i18n_init();
    g_set_application_name(PRISM_APP_NAME);

    const char *prog = argc > 0 ? argv[0] : "prism-monitor";
    char *opt_page = NULL;
    double opt_interval = 0;
    double opt_duration = 0;
    char *opt_snapshot = NULL;
    char *opt_record = NULL;
    gboolean opt_paused = FALSE;
    gboolean opt_snapshot_set = FALSE;
    gboolean opt_daemon = FALSE;
    gboolean opt_diagnostics = FALSE;
    gboolean opt_tray = FALSE;
    gboolean opt_minimized = FALSE;
    int opt_exporter = 0;

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
        } else if (strcmp(arg, "--daemon") == 0) {
            opt_daemon = TRUE;
        } else if (strcmp(arg, "--diagnostics") == 0) {
            opt_diagnostics = TRUE;
        } else if (strcmp(arg, "--tray") == 0) {
            opt_tray = TRUE;
        } else if (strcmp(arg, "--minimized") == 0 ||
                   strcmp(arg, "--start-minimized") == 0) {
            opt_minimized = TRUE;
        } else if (strcmp(arg, "--no-tray") == 0) {
            startup_no_tray = TRUE;
        } else if (strcmp(arg, "--quit") == 0 || strcmp(arg, "--exit") == 0) {
            startup_quit = TRUE;
        } else if (strncmp(arg, "--exporter=", 11) == 0) {
            opt_exporter = atoi(arg + 11);
        } else if (strcmp(arg, "--exporter") == 0 && i + 1 < argc) {
            opt_exporter = atoi(argv[++i]);
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
    if (opt_daemon) {
        int status = run_daemon(opt_interval);
        g_free(opt_page);
        g_free(opt_snapshot);
        g_free(opt_record);
        g_free(gui_argv);
        return status;
    }
    if (opt_exporter > 0) {
        int status = run_exporter(opt_exporter, opt_interval, opt_duration);
        g_free(opt_page);
        g_free(opt_snapshot);
        g_free(opt_record);
        g_free(gui_argv);
        return status;
    }
    if (opt_diagnostics) {
        int status = run_diagnostics(opt_interval);
        g_free(opt_page);
        g_free(opt_snapshot);
        g_free(opt_record);
        g_free(gui_argv);
        return status;
    }

    if (startup_quit) {
        /* Ask the running primary instance to quit via its GtkApplication
         * D-Bus} name. If no instance is running, just exit. */
        GError *err = NULL;
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (bus != NULL) {
            GVariantBuilder empty_av, empty_dict, params_b;
            g_variant_builder_init(&empty_av, G_VARIANT_TYPE("av"));
            g_variant_builder_init(&empty_dict, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_init(&params_b, G_VARIANT_TYPE("(sava{sv})"));
            g_variant_builder_add(&params_b, "s", "quit");
            g_variant_builder_add_value(&params_b, g_variant_builder_end(&empty_av));
            g_variant_builder_add_value(&params_b, g_variant_builder_end(&empty_dict));
            GVariant *params = g_variant_builder_end(&params_b);
            GVariant *res = g_dbus_connection_call_sync(
                bus, "io.prism.Monitor", "/io/prism/Monitor",
                "org.gtk.Actions", "Activate", params, NULL,
                G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);
            g_variant_unref(params);
            if (res != NULL) {
                g_variant_unref(res);
                g_object_unref(bus);
                g_free(opt_page);
                g_free(opt_snapshot);
                g_free(opt_record);
                g_free(gui_argv);
                return 0;
            }
            g_clear_error(&err);
            g_object_unref(bus);
        } else {
            g_clear_error(&err);
        }
        /* Launch normally; prism_activate will quit immediately */
    }

    app_configure_startup_full(opt_page, opt_interval, opt_paused,
                               opt_minimized, opt_tray);

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
