#ifndef PRISM_PAGES_H
#define PRISM_PAGES_H

#include "ui/app.h"

GtkWidget *page_overview_create(App *app);
void page_overview_update(App *app);
GtkWidget *page_cpu_create(App *app);
void page_cpu_update(App *app);
GtkWidget *page_memory_create(App *app);
void page_memory_update(App *app);
GtkWidget *page_gpu_create(App *app);
void page_gpu_update(App *app);
GtkWidget *page_storage_create(App *app);
void page_storage_update(App *app);
GtkWidget *page_network_create(App *app);
void page_network_update(App *app);
GtkWidget *page_processes_create(App *app);
void page_processes_update(App *app);
void page_processes_focus_search(App *app);
void page_processes_act(App *app, int force);
void page_processes_pause(App *app);
GtkWidget *page_sensors_create(App *app);
void page_sensors_update(App *app);
GtkWidget *page_system_create(App *app);
void page_system_update(App *app);

GtkWidget *page_services_create(App *app);
void page_services_update(App *app);
GtkWidget *page_startup_create(App *app);
void page_startup_update(App *app);
GtkWidget *page_connections_create(App *app);
void page_connections_update(App *app);
GtkWidget *page_apps_create(App *app);
void page_apps_update(App *app);
GtkWidget *page_drivers_create(App *app);
void page_drivers_update(App *app);
GtkWidget *page_diskspace_create(App *app);
void page_diskspace_update(App *app);
GtkWidget *page_benchmarks_create(App *app);
void page_benchmarks_update(App *app);
GtkWidget *page_power_create(App *app);
void page_power_update(App *app);
GtkWidget *page_recorder_create(App *app);
void page_recorder_update(App *app);
void page_recorder_capture(App *app);

#endif
