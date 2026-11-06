#include "core/export.h"

#include "core/history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *export_json_escape(const char *text) {
    if (text == NULL) {
        return g_strdup("");
    }
    GString *out = g_string_new("");
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
            case '"': g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\b': g_string_append(out, "\\b"); break;
            case '\f': g_string_append(out, "\\f"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if (*p < 0x20) {
                    g_string_append_printf(out, "\\u%04x", *p);
                } else {
                    g_string_append_c(out, (char)*p);
                }
                break;
        }
    }
    return g_string_free(out, FALSE);
}

static char *jstr(const char *text) {
    char *e = export_json_escape(text != NULL ? text : "");
    char *r = g_strdup_printf("\"%s\"", e);
    g_free(e);
    return r;
}

char *export_snapshot_json(const Snapshot *snap, const History *hist) {
    if (snap == NULL) {
        return g_strdup("{\"error\":\"no snapshot\"}\n");
    }
    GString *s = g_string_new("");
    char *v;
    g_string_append(s, "{\n");
    g_string_append_printf(s, "  \"timestamp_us\": %lld,\n",
                           (long long)snap->timestamp_us);
    g_string_append_printf(s, "  \"uptime_s\": %.3f,\n", snap->uptime_s);
    v = jstr(snap->sys.hostname);
    g_string_append_printf(s, "  \"hostname\": %s,\n", v);
    g_free(v);
    v = jstr(snap->sys.kernel);
    g_string_append_printf(s, "  \"kernel\": %s,\n", v);
    g_free(v);
    v = jstr(snap->sys.distro);
    g_string_append_printf(s, "  \"distro\": %s,\n", v);
    g_free(v);
    v = jstr(snap->sys.cpu_model);
    g_string_append_printf(s, "  \"cpu_model\": %s,\n", v);
    g_free(v);
    g_string_append_printf(s,
                           "  \"cpu\": {\"usage_pct\": %.2f, \"user_pct\": %.2f, "
                           "\"system_pct\": %.2f, \"iowait_pct\": %.2f, "
                           "\"freq_mhz\": %.1f, \"temp_c\": %.1f, "
                           "\"power_w\": %.2f, \"load1\": %.2f, \"load5\": %.2f, "
                           "\"load15\": %.2f, \"cores\": %d},\n",
                           snap->cpu.usage_pct, snap->cpu.user_pct,
                           snap->cpu.system_pct, snap->cpu.iowait_pct,
                           snap->cpu.freq_mhz, snap->cpu.temp_c,
                           snap->cpu.package_power_w, snap->cpu.load1,
                           snap->cpu.load5, snap->cpu.load15,
                           snap->cpu.core_count);
    g_string_append_printf(s,
                           "  \"memory\": {\"total_mb\": %.1f, \"used_mb\": %.1f, "
                           "\"used_pct\": %.2f, \"swap_pct\": %.2f, "
                           "\"psi_some10\": %.2f, \"psi_full10\": %.2f},\n",
                           snap->mem.total_mb, snap->mem.used_mb,
                           snap->mem.used_pct, snap->mem.swap_pct,
                           snap->mem.psi_some10, snap->mem.psi_full10);
    g_string_append_printf(s,
                           "  \"network\": {\"rx_bps\": %.1f, \"tx_bps\": %.1f, "
                           "\"interfaces\": %d},\n",
                           snap->net_rx_bps, snap->net_tx_bps,
                           snap->nic_count);
    g_string_append_printf(s, "  \"processes\": %d,\n", snap->proc_count);
    g_string_append_printf(s, "  \"sensors\": %d,\n", snap->sensor_count);
    g_string_append_printf(s, "  \"battery\": {\"present\": %s, \"capacity_pct\": %.1f, "
                           "\"power_w\": %.2f},\n",
                           snap->battery.present ? "true" : "false",
                           snap->battery.capacity_pct, snap->battery.power_w);
    g_string_append(s, "  \"gpus\": [");
    for (int i = 0; i < snap->gpu_count; i++) {
        char *name = jstr(snap->gpus[i].name);
        g_string_append_printf(s, "%s{\"name\": %s, \"util_pct\": %.2f, "
                               "\"temp_c\": %.1f, \"vram_used_mb\": %.1f}",
                               i ? ", " : "", name, snap->gpus[i].util_pct,
                               snap->gpus[i].temp_c,
                               snap->gpus[i].vram_used_mb);
        g_free(name);
    }
    g_string_append(s, "],\n");
    g_string_append(s, "  \"mounts\": [");
    for (int i = 0; i < snap->mount_count; i++) {
        char *m = jstr(snap->mounts[i].mount);
        char *d = jstr(snap->mounts[i].device);
        g_string_append_printf(s, "%s{\"mount\": %s, \"device\": %s, "
                               "\"used_pct\": %.2f}",
                               i ? ", " : "", m, d,
                               snap->mounts[i].used_pct);
        g_free(m);
        g_free(d);
    }
    g_string_append(s, "],\n");
    if (hist != NULL && hist->count > 0) {
        g_string_append_printf(s, "  \"history_samples\": %d,\n",
                               hist->count);
        g_string_append_printf(s,
                               "  \"history_latest\": {\"cpu_pct\": %.2f, "
                               "\"mem_pct\": %.2f, \"net_rx_bps\": %.1f, "
                               "\"net_tx_bps\": %.1f}\n",
                               (double)history_get(hist->cpu_total, hist, 0),
                               (double)history_get(hist->mem_used, hist, 0),
                               (double)history_get(hist->net_rx, hist, 0),
                               (double)history_get(hist->net_tx, hist, 0));
    } else {
        g_string_append(s, "  \"history_samples\": 0\n");
    }
    g_string_append(s, "}\n");
    return g_string_free(s, FALSE);
}

gboolean export_snapshot_json_file(const char *path, const Snapshot *snap,
                                   const History *hist) {
    char *json = export_snapshot_json(snap, hist);
    gboolean ok = FALSE;
    if (path == NULL || strcmp(path, "-") == 0) {
        fputs(json, stdout);
        fflush(stdout);
        ok = TRUE;
    } else {
        FILE *f = fopen(path, "w");
        if (f != NULL) {
            fputs(json, f);
            ok = fclose(f) == 0;
        }
    }
    g_free(json);
    return ok;
}

/* TSV columns (page_recorder.c):
 * timestamp cpu_pct mem_pct swap_pct rx_bps tx_bps disk_r disk_w gpu_pct
 * cpu_temp_c power_w load1 */
static const char *const tsv_header =
    "timestamp\tcpu_pct\tmem_pct\tswap_pct\trx_bps\ttx_bps\tdisk_r_bps\t"
    "disk_w_bps\tgpu_pct\tcpu_temp_c\tpower_w\tload1";
static const char *const csv_header =
    "timestamp,cpu_pct,mem_pct,swap_pct,rx_bps,tx_bps,disk_r_bps,"
    "disk_w_bps,gpu_pct,cpu_temp_c,power_w,load1";
static const char *const json_keys[] = {
    "timestamp", "cpu_pct", "mem_pct", "swap_pct", "rx_bps", "tx_bps",
    "disk_r_bps", "disk_w_bps", "gpu_pct", "cpu_temp_c", "power_w", "load1"
};

static gboolean parse_row(const char *line, double vals[12]) {
    if (line == NULL || line[0] == '\0' || line[0] == '#') {
        return FALSE;
    }
    char *copy = g_strdup(line);
    char *save = NULL;
    char *tok = strtok_r(copy, "\t\r\n", &save);
    int n = 0;
    while (tok != NULL && n < 12) {
        vals[n++] = strtod(tok, NULL);
        tok = strtok_r(NULL, "\t\r\n", &save);
    }
    g_free(copy);
    return n == 12;
}

char *export_tsv_to_csv(const char *tsv_text) {
    GString *out = g_string_new("");
    g_string_append(out, csv_header);
    g_string_append_c(out, '\n');
    if (tsv_text == NULL) {
        return g_string_free(out, FALSE);
    }
    char *copy = g_strdup(tsv_text);
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    while (line != NULL) {
        double v[12];
        if (parse_row(line, v)) {
            g_string_append_printf(out,
                                   "%.3f,%.2f,%.2f,%.2f,%.0f,%.0f,%.0f,%.0f,"
                                   "%.2f,%.1f,%.2f,%.2f\n",
                                   v[0], v[1], v[2], v[3], v[4], v[5], v[6],
                                   v[7], v[8], v[9], v[10], v[11]);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(copy);
    (void)tsv_header;
    return g_string_free(out, FALSE);
}

char *export_tsv_to_json(const char *tsv_text) {
    GString *out = g_string_new("[\n");
    int first = 1;
    if (tsv_text == NULL) {
        g_string_append(out, "]\n");
        return g_string_free(out, FALSE);
    }
    char *copy = g_strdup(tsv_text);
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    while (line != NULL) {
        double v[12];
        if (parse_row(line, v)) {
            g_string_append(out, first ? "  {" : ",\n  {");
            first = 0;
            for (int i = 0; i < 12; i++) {
                g_string_append_printf(out, "%s\"%s\": %.3f", i ? ", " : "",
                                       json_keys[i], v[i]);
            }
            g_string_append(out, "}");
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(copy);
    g_string_append(out, first ? "]\n" : "\n]\n");
    return g_string_free(out, FALSE);
}

gboolean export_tsv_stats(const char *tsv_text, double *cpu_peak,
                          double *cpu_avg, double *mem_peak, double *mem_avg,
                          int *rows) {
    double cp = 0, mp = 0, cs = 0, ms = 0;
    int n = 0;
    if (tsv_text == NULL) {
        return FALSE;
    }
    char *copy = g_strdup(tsv_text);
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    while (line != NULL) {
        double v[12];
        if (parse_row(line, v)) {
            if (v[1] > cp) cp = v[1];
            if (v[2] > mp) mp = v[2];
            cs += v[1];
            ms += v[2];
            n++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(copy);
    if (n == 0) {
        return FALSE;
    }
    if (cpu_peak) *cpu_peak = cp;
    if (mem_peak) *mem_peak = mp;
    if (cpu_avg) *cpu_avg = cs / n;
    if (mem_avg) *mem_avg = ms / n;
    if (rows) *rows = n;
    return TRUE;
}

char *export_snapshot_prometheus(const Snapshot *snap) {
    if (snap == NULL) {
        return g_strdup("# no snapshot\n");
    }
    GString *s = g_string_new("");
    g_string_append_printf(s, "# HELP prism_cpu_usage_percent CPU usage\n");
    g_string_append_printf(s, "# TYPE prism_cpu_usage_percent gauge\n");
    g_string_append_printf(s, "prism_cpu_usage_percent %.2f\n", snap->cpu.usage_pct);
    g_string_append_printf(s, "prism_cpu_temp_celsius %.1f\n", snap->cpu.temp_c);
    g_string_append_printf(s, "prism_cpu_freq_mhz %.1f\n", snap->cpu.freq_mhz);
    g_string_append_printf(s, "prism_cpu_load1 %.2f\n", snap->cpu.load1);
    g_string_append_printf(s, "prism_mem_used_percent %.2f\n", snap->mem.used_pct);
    g_string_append_printf(s, "prism_mem_swap_percent %.2f\n", snap->mem.swap_pct);
    g_string_append_printf(s, "prism_net_rx_bps %.1f\n", snap->net_rx_bps);
    g_string_append_printf(s, "prism_net_tx_bps %.1f\n", snap->net_tx_bps);
    g_string_append_printf(s, "prism_procs_total %d\n", snap->proc_count);
    g_string_append_printf(s, "prism_sensors_total %d\n", snap->sensor_count);
    g_string_append_printf(s, "prism_ping_ms %.1f\n", snap->ping_ms);
    for (int i = 0; i < snap->gpu_count; i++) {
        g_string_append_printf(s, "prism_gpu_util_percent{gpu=\"%s\"} %.2f\n",
                               snap->gpus[i].name, snap->gpus[i].util_pct);
        g_string_append_printf(s, "prism_gpu_temp_celsius{gpu=\"%s\"} %.1f\n",
                               snap->gpus[i].name, snap->gpus[i].temp_c);
    }
    for (int i = 0; i < snap->disk_count; i++) {
        g_string_append_printf(s, "prism_disk_read_bps{disk=\"%s\"} %.1f\n",
                               snap->disks[i].device, snap->disks[i].read_bps);
        g_string_append_printf(s, "prism_disk_write_bps{disk=\"%s\"} %.1f\n",
                               snap->disks[i].device, snap->disks[i].write_bps);
    }
    return g_string_free(s, FALSE);
}

char *export_diagnostics_bundle(const Snapshot *snap, const History *hist,
                                const char *config_path) {
    char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                 "diagnostics", NULL);
    g_mkdir_with_parents(dir, 0700);
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    char *bundle_dir = g_build_filename(dir, stamp, NULL);
    g_mkdir_with_parents(bundle_dir, 0700);
    /* snapshot.json */
    {
        char *p = g_build_filename(bundle_dir, "snapshot.json", NULL);
        export_snapshot_json_file(p, snap, hist);
        g_free(p);
    }
    /* config.ini copy */
    if (config_path != NULL) {
        char *p = g_build_filename(bundle_dir, "config.ini", NULL);
        gchar *contents = NULL;
        gsize len = 0;
        if (g_file_get_contents(config_path, &contents, &len, NULL)) {
            g_file_set_contents(p, contents, len, NULL);
            g_free(contents);
        }
        g_free(p);
    }
    /* system info */
    {
        char *p = g_build_filename(bundle_dir, "system.txt", NULL);
        FILE *f = fopen(p, "w");
        if (f != NULL) {
            fprintf(f, "Prism Monitor diagnostics %s\n", stamp);
            if (snap != NULL) {
                fprintf(f, "host=%s kernel=%s distro=%s cpu=%s\n",
                        snap->sys.hostname, snap->sys.kernel,
                        snap->sys.distro, snap->sys.cpu_model);
                fprintf(f, "uptime=%.0f procs=%d sensors=%d disks=%d gpus=%d\n",
                        snap->uptime_s, snap->proc_count, snap->sensor_count,
                        snap->disk_count, snap->gpu_count);
            }
            fclose(f);
        }
        g_free(p);
    }
    /* alerts.log + history tail */
    {
        const char *paths[] = {NULL, NULL};
        char *alog = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                      "alerts.log", NULL);
        char *histp = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                       "history.tsv", NULL);
        paths[0] = alog;
        paths[1] = histp;
        const char *names[] = {"alerts.log", "history.tsv"};
        for (int i = 0; i < 2; i++) {
            char *p = g_build_filename(bundle_dir, names[i], NULL);
            gchar *contents = NULL;
            gsize len = 0;
            if (g_file_get_contents(paths[i], &contents, &len, NULL)) {
                /* truncate history to last ~200KB for bundle */
                if (len > 200 * 1024) {
                    contents += len - 200 * 1024;
                    len = 200 * 1024;
                    g_file_set_contents(p, contents, len, NULL);
                    contents -= len;
                } else {
                    g_file_set_contents(p, contents, len, NULL);
                }
                g_free(contents);
            }
            g_free(p);
        }
        g_free(alog);
        g_free(histp);
    }
    /* tar.gz via argv */
    char *tar_path = g_strconcat(bundle_dir, ".tar.gz", NULL);
    gchar *argv[] = {"tar", "-czf", tar_path, "-C", dir, stamp, NULL};
    GError *err = NULL;
    if (!g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH,
                      NULL, NULL, NULL, NULL, NULL, &err)) {
        g_clear_error(&err);
        g_free(tar_path);
        g_free(bundle_dir);
        g_free(dir);
        return NULL;
    }
    g_free(bundle_dir);
    g_free(dir);
    return tar_path;
}
