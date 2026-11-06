#include "history.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

Snapshot *snapshot_new(void) {
    return calloc(1, sizeof(Snapshot));
}

void snapshot_free(Snapshot *snap) {
    free(snap);
}

void history_clear(History *hist) {
    if (hist != NULL) {
        memset(hist, 0, sizeof(*hist));
    }
}

static void hist_put(float *series, History *hist, float value) {
    series[hist->head] = value;
}

int history_count(const History *hist) {
    return hist == NULL ? 0 : hist->count;
}

void history_push(History *hist, const Snapshot *snap) {
    if (hist == NULL || snap == NULL) {
        return;
    }
    hist_put(hist->cpu_total, hist, (float)snap->cpu.usage_pct);
    hist_put(hist->cpu_user, hist, (float)snap->cpu.user_pct);
    hist_put(hist->cpu_sys, hist, (float)snap->cpu.system_pct);
    hist_put(hist->cpu_io, hist, (float)snap->cpu.iowait_pct);
    hist_put(hist->cpu_irq, hist, (float)(snap->cpu.irq_pct));
    hist_put(hist->cpu_nice, hist, (float)snap->cpu.nice_pct);
    hist_put(hist->cpu_steal, hist, (float)snap->cpu.steal_pct);
    for (int i = 0; i < snap->cpu.core_count && i < PRISM_MAX_CORES; i++) {
        hist_put(hist->cpu_core[i], hist, (float)snap->cpu.per_core[i]);
    }
    hist_put(hist->mem_used, hist, (float)snap->mem.used_pct);
    float cache_pct = 0;
    if (snap->mem.total_mb > 0) {
        cache_pct = (float)((snap->mem.cached_mb + snap->mem.buffers_mb) /
                            snap->mem.total_mb * 100.0);
    }
    hist_put(hist->mem_cache, hist, cache_pct);
    hist_put(hist->swap, hist, (float)snap->mem.swap_pct);
    hist_put(hist->net_rx, hist, (float)snap->net_rx_bps);
    hist_put(hist->net_tx, hist, (float)snap->net_tx_bps);
    float disk_read = 0;
    float disk_write = 0;
    for (int i = 0; i < snap->disk_count; i++) {
        disk_read += (float)snap->disks[i].read_bps;
        disk_write += (float)snap->disks[i].write_bps;
    }
    hist_put(hist->disk_r, hist, disk_read);
    hist_put(hist->disk_w, hist, disk_write);
    float gpu_util = 0;
    float gpu_temp = 0;
    for (int i = 0; i < snap->gpu_count; i++) {
        if (snap->gpus[i].present) {
            gpu_util = (float)snap->gpus[i].util_pct;
            gpu_temp = (float)snap->gpus[i].temp_c;
            break;
        }
    }
    hist_put(hist->gpu_util, hist, gpu_util);
    hist_put(hist->gpu_temp, hist, gpu_temp);
    hist_put(hist->freq, hist, (float)snap->cpu.freq_mhz);
    hist_put(hist->power, hist, (float)snap->cpu.package_power_w);
    hist_put(hist->temp, hist, (float)snap->cpu.temp_c);
    hist_put(hist->load1, hist, (float)snap->cpu.load1);
    hist->head = (hist->head + 1) % PRISM_HIST;
    if (hist->count < PRISM_HIST) {
        hist->count++;
    }
}

float history_get(const float *series, const History *hist, int ago) {
    if (series == NULL || hist == NULL || hist->count == 0) {
        return 0;
    }
    if (ago < 0) {
        ago = 0;
    }
    if (ago >= hist->count) {
        ago = hist->count - 1;
    }
    int index = (hist->head - 1 - ago) % PRISM_HIST;
    if (index < 0) {
        index += PRISM_HIST;
    }
    return series[index];
}

const char *history_path(void) {
    static char *path = NULL;
    if (path == NULL) {
        path = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                "history.tsv", NULL);
    }
    return path;
}

void history_rotate_if_large(void) {
    struct stat st;
    if (stat(history_path(), &st) == 0 && st.st_size > 5 * 1024 * 1024) {
        char *old = g_strconcat(history_path(), ".1", NULL);
        rename(history_path(), old);
        g_free(old);
    }
}

void history_append(const Snapshot *snap) {
    if (snap == NULL) {
        return;
    }
    char *dir = g_path_get_dirname(history_path());
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    FILE *f = fopen(history_path(), "a");
    if (f == NULL) {
        return;
    }
    double gpu = snap->gpu_count > 0 ? snap->gpus[0].util_pct : 0;
    double dr = 0, dw = 0;
    for (int i = 0; i < snap->disk_count; i++) {
        dr += snap->disks[i].read_bps;
        dw += snap->disks[i].write_bps;
    }
    fprintf(f, "%.3f\t%.2f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.0f\t%.2f\t%.1f\t%.2f\t%.2f\n",
            (double)snap->timestamp_us / 1000000.0, snap->cpu.usage_pct,
            snap->mem.used_pct, snap->mem.swap_pct, snap->net_rx_bps,
            snap->net_tx_bps, dr, dw, gpu, snap->cpu.temp_c,
            snap->cpu.package_power_w, snap->cpu.load1);
    fclose(f);
}

int history_load(History *hist) {
    if (hist == NULL) {
        return 0;
    }
    FILE *f = fopen(history_path(), "r");
    if (f == NULL) {
        return 0;
    }
    /* Keep only last PRISM_HIST rows: read all, push via temp snapshot */
    int loaded = 0;
    char line[512];
    Snapshot tmp;
    memset(&tmp, 0, sizeof(tmp));
    /* Collect rows in a small ring by seeking: simple approach - read all
     * and push each, history ring keeps last 240 */
    while (fgets(line, sizeof(line), f) != NULL) {
        double v[12];
        char *copy = g_strdup(line);
        char *save = NULL;
        char *tok = strtok_r(copy, "\t\r\n", &save);
        int n = 0;
        while (tok != NULL && n < 12) {
            v[n++] = strtod(tok, NULL);
            tok = strtok_r(NULL, "\t\r\n", &save);
        }
        g_free(copy);
        if (n != 12) {
            continue;
        }
        memset(&tmp, 0, sizeof(tmp));
        tmp.cpu.usage_pct = v[1];
        tmp.cpu.user_pct = v[1] * 0.7;
        tmp.cpu.system_pct = v[1] * 0.3;
        tmp.mem.used_pct = v[2];
        tmp.mem.swap_pct = v[3];
        tmp.net_rx_bps = v[4];
        tmp.net_tx_bps = v[5];
        tmp.cpu.temp_c = v[9];
        tmp.cpu.package_power_w = v[10];
        tmp.cpu.load1 = v[11];
        tmp.cpu.core_count = 1;
        tmp.cpu.per_core[0] = v[1];
        tmp.gpu_count = 1;
        tmp.gpus[0].present = 1;
        tmp.gpus[0].util_pct = v[8];
        tmp.gpus[0].temp_c = v[9];
        tmp.disk_count = 1;
        tmp.disks[0].read_bps = v[6];
        tmp.disks[0].write_bps = v[7];
        history_push(hist, &tmp);
        loaded++;
    }
    fclose(f);
    return loaded;
}
