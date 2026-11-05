#ifndef PRISM_APP_H
#define PRISM_APP_H

#include "core/alerts.h"
#include "core/config.h"
#include "core/sampler.h"
#include "core/types.h"
#include "ui/theme.h"

#include <gtk/gtk.h>

typedef enum {
    PAGE_OVERVIEW,
    PAGE_CPU,
    PAGE_MEMORY,
    PAGE_GPU,
    PAGE_STORAGE,
    PAGE_NETWORK,
    PAGE_PROCESSES,
    PAGE_SENSORS,
    PAGE_SYSTEM,
    PAGE_SERVICES,
    PAGE_STARTUP,
    PAGE_CONNECTIONS,
    PAGE_APPS,
    PAGE_DRIVERS,
    PAGE_DISKSPACE,
    PAGE_BENCHMARKS,
    PAGE_POWER,
    PAGE_RECORDER,
    PAGE_COUNT
} PageId;

typedef struct App App;

typedef struct {
    PageId id;
    const char *name;
    const char *subtitle;
    const char *icon;
    GtkWidget *root;
    void (*update)(App *app);
} Page;

struct App {
    GtkApplication *application;
    GtkWindow *window;
    GtkHeaderBar *header;
    GtkStack *stack;
    GtkWidget *sidebar;
    GtkWidget *search;
    GtkLabel *title_label;
    GtkLabel *subtitle_label;
    GtkLabel *live_label;
    GtkLabel *status_left;
    GtkLabel *status_right;
    GtkWidget *toast_revealer;
    GtkLabel *toast_label;
    guint toast_source;
    GtkCssProvider *css_provider;
    GtkWidget *settings_window;
    GtkWidget *appearance_window;
    GtkWidget *shortcuts_window;
    GtkWidget *pause_button;
    GtkWidget *palette_window;
    AlertState alerts;
    gboolean paused;

    Sampler *sampler;
    Snapshot *view;
    History history;
    guint64 seq;
    guint poll_source;
    guint tick_source;
    gint64 anim_until;

    Config config;
    Theme theme;
    Page pages[PAGE_COUNT];
    PageId current_page;
    int sidebar_hover;
    int sidebar_hover_action;
    double sidebar_scroll;

    GPtrArray *draw_widgets;
    GPtrArray *anim_widgets;
    char filter[128];

    int proc_sort;
    int proc_sort_desc;
    int proc_selected_pid;
    int proc_hover_row;
    void *process_state;
};

App *app_create(GtkApplication *application);
void app_destroy(App *app);
void app_navigate(App *app, PageId page);
void app_refresh(App *app);
void app_track_draw(App *app, GtkWidget *widget);
void app_track_anim(App *app, GtkWidget *widget);
void app_apply_theme(App *app);
void app_update_tick(App *app);
void app_save_config(App *app);
void app_notify(App *app, const char *format, ...) G_GNUC_PRINTF(2, 3);
void app_open_settings(App *app);
void app_open_appearance(App *app);
void app_show_shortcuts(App *app);
void app_show_about(App *app);
void app_set_sidebar_collapsed(App *app, gboolean collapsed);
const Theme *app_theme(App *app);
void app_update_header(App *app);
void app_process_filter_changed(App *app);
double app_interval(App *app);
double app_ease_factor(App *app);
int app_chart_window(App *app);
void app_format_temp(App *app, double celsius, char *buf, size_t len);
const char *app_font_family(App *app);
void app_configure_startup(const char *page_name, double interval, gboolean paused);
void app_set_paused(App *app, gboolean paused);
gboolean app_is_paused(App *app);
void app_export_snapshot(App *app);
void app_open_palette(App *app);
void app_check_alerts(App *app);
int app_page_from_name(const char *name);

GtkWidget *app_page_scroller(GtkWidget *content);

#endif
