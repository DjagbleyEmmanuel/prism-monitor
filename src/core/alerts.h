#ifndef PRISM_ALERTS_H
#define PRISM_ALERTS_H

#include "core/types.h"

#include <glib.h>

enum {
    ALERT_TEMP = 1 << 0,
    ALERT_DISK = 1 << 1,
    ALERT_MEM = 1 << 2,
    ALERT_BATTERY = 1 << 3,
    ALERT_COUNT = 4
};

typedef struct {
    gint64 last_us[ALERT_COUNT];
} AlertState;

void alerts_init(AlertState *state);

/* Returns bitmask of alerts that fired on this call (cooldown applied).
 * now_us is monotonic microseconds. Thresholds <=0 disable that check. */
int alerts_check(AlertState *state, const Snapshot *snap, int enabled,
                 double temp_c, double disk_pct, double mem_pct,
                 double battery_pct, gint64 now_us, gint64 cooldown_us);

/* Highest used mount percentage, or 0 when no mounts. */
double alerts_worst_disk_pct(const Snapshot *snap);

#endif
