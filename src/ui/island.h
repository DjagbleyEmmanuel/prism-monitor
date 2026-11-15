#ifndef PRISM_ISLAND_H
#define PRISM_ISLAND_H

#include "ui/app.h"

enum {
    ISLAND_METRIC_GPU_UTIL = 1 << 0,
    ISLAND_METRIC_GPU_TEMP = 1 << 1,
    ISLAND_METRIC_VRAM = 1 << 2,
    ISLAND_METRIC_CPU_TEMP = 1 << 3,
    ISLAND_METRIC_CPU_FREQ = 1 << 4,
    ISLAND_METRIC_CPU_UTIL = 1 << 5,
    ISLAND_METRIC_MEMORY = 1 << 6,
    ISLAND_METRIC_DISK = 1 << 7,
    ISLAND_METRIC_NET = 1 << 8,
    ISLAND_METRIC_BATTERY = 1 << 9,
    ISLAND_METRIC_PING = 1 << 10,
};

#define ISLAND_DOCK_FLOAT 0
#define ISLAND_DOCK_TOP 1
#define ISLAND_DOCK_RIGHT 2
#define ISLAND_DOCK_BOTTOM 3
#define ISLAND_DOCK_LEFT 4

void island_create(App *app);
void island_destroy(App *app);
void island_show(App *app, gboolean show);
void island_toggle(App *app);
void island_tick(App *app);
void island_set_dock(App *app, int dock);
void island_refresh_metrics(App *app);
void island_raise(App *app);

#endif
