#include "types.h"

#include <stdlib.h>
#include <string.h>

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
