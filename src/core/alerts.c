#include "core/alerts.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

const char *alerts_log_path(void) {
    static char *path = NULL;
    if (path == NULL) {
        path = g_build_filename(g_get_user_data_dir(), "prism-monitor",
                                "alerts.log", NULL);
    }
    return path;
}

void alerts_log(const char *message) {
    if (message == NULL) {
        return;
    }
    char *dir = g_path_get_dirname(alerts_log_path());
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    FILE *f = fopen(alerts_log_path(), "a");
    if (f == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    fprintf(f, "%s  %s\n", stamp, message);
    fclose(f);
}

void alerts_run_hook(const char *hook_cmd, int fired_mask, const char *message) {
    if (hook_cmd == NULL || hook_cmd[0] == '\0') {
        return;
    }
    /* Hook is user-configured; run via shell with env vars */
    gchar *argv[] = {"/bin/sh", "-c", (gchar *)hook_cmd, NULL};
    g_setenv("PRISM_ALERT_MASK", g_strdup_printf("%d", fired_mask), TRUE);
    g_setenv("PRISM_ALERT_MSG", message != NULL ? message : "", TRUE);
    g_spawn_async(NULL, argv, NULL,
                  G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                  NULL, NULL, NULL, NULL);
}
