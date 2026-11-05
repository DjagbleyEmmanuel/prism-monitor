#include "core/alerts.h"

#include <string.h>

void alerts_init(AlertState *state) {
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

double alerts_worst_disk_pct(const Snapshot *snap) {
    double worst = 0;
    if (snap == NULL) {
        return 0;
    }
    for (int i = 0; i < snap->mount_count; i++) {
        if (snap->mounts[i].used_pct > worst) {
            worst = snap->mounts[i].used_pct;
        }
    }
    return worst;
}

static int try_fire(AlertState *state, int index, int condition,
                    gint64 now_us, gint64 cooldown_us) {
    if (!condition) {
        return 0;
    }
    if (state != NULL && cooldown_us > 0 && state->last_us[index] != 0 &&
        now_us - state->last_us[index] < cooldown_us) {
        return 0;
    }
    if (state != NULL) {
        state->last_us[index] = now_us;
    }
    return 1 << index;
}

int alerts_check(AlertState *state, const Snapshot *snap, int enabled,
                 double temp_c, double disk_pct, double mem_pct,
                 double battery_pct, gint64 now_us, gint64 cooldown_us) {
    if (!enabled || snap == NULL) {
        return 0;
    }
    if (cooldown_us <= 0) {
        cooldown_us = 60 * G_USEC_PER_SEC;
    }
    int fired = 0;
    double temp = snap->cpu.temp_c;
    /* Prefer GPU temp when CPU sensor is absent. */
    if (temp <= 0 && snap->gpu_count > 0) {
        temp = snap->gpus[0].temp_c;
    }
    fired |= try_fire(state, 0, temp_c > 0 && temp >= temp_c, now_us, cooldown_us);
    double disk = alerts_worst_disk_pct(snap);
    fired |= try_fire(state, 1, disk_pct > 0 && disk >= disk_pct, now_us, cooldown_us);
    fired |= try_fire(state, 2, mem_pct > 0 && snap->mem.used_pct >= mem_pct,
                      now_us, cooldown_us);
    int low_battery = snap->battery.present && snap->battery.capacity_pct > 0 &&
                      snap->battery.capacity_pct <= battery_pct &&
                      battery_pct > 0;
    fired |= try_fire(state, 3, low_battery, now_us, cooldown_us);
    return fired;
}
