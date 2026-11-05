#ifndef PRISM_SENSORS_H
#define PRISM_SENSORS_H

#include "core/types.h"

void sensors_read(Snapshot *snap);
void battery_read(BatteryInfo *out);

#endif
