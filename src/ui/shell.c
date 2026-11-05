#include "ui/app.h"

#include "core/common.h"
#include "core/export.h"
#include "core/format.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <gdk/gdkkeysyms.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SIDEBAR_WIDTH 220.0
#define SIDEBAR_COLLAPSED_WIDTH 68.0
#define SIDEBAR_TOP 18.0
#define SIDEBAR_GROUP_H 24.0
#define SIDEBAR_COLLAPSED_GROUP_H 12.0
#define SIDEBAR_ITEM_H 38.0
#define SIDEBAR_ITEM_STEP 41.0
#define SIDEBAR_FOOTER_H 164.0
#define SIDEBAR_FOOTER_ROW_H 34.0
#define SIDEBAR_FOOTER_ROWS 4
#define ACTION_SETTINGS (-1)
#define ACTION_THEME (-2)
#define ACTION_SHORTCUTS (-3)
#define ACTION_ABOUT (-4)

typedef struct {
    int action;
    const char *label;
    const char *icon;
    int is_group;
    double y;
    double h;
} SidebarEntry;

static const char *page_names[PAGE_COUNT] = {
    "Overview", "CPU", "Memory", "GPU", "Storage",
    "Network", "Processes", "Sensors", "System",
    "Services", "Startup", "Connections", "Installed Apps",
    "Drivers", "Disk Space", "Benchmarks", "Power & Frequency",
    "Flight Recorder"
};

static const char *page_subtitles[PAGE_COUNT] = {
    "Live system at a glance",
    "Per-core load, clocks and thermal headroom",
    "Physical memory, cache, swap and pressure",
    "Graphics engine, VRAM and power",
    "Capacity, throughput and device health",
    "Interfaces, throughput and sockets",
    "Inspect, sort and control running tasks",
    "Temperatures, fans, power and battery",
    "Host, hardware, sessions and services",
    "Systemd units, runtime state and boot activation",
    "Applications that launch with your session",
    "Sockets, endpoints and owning processes",
    "Desktop entries across system, Flatpak and Snap",
    "Loaded and built-in kernel modules",
    "Mounts, capacity and background disk analysis",
    "Repeatable CPU, GPU, disk and network tests",
    "Package power, clocks, thermal headroom and battery",
    "Capture live telemetry to timestamped recordings"
};

static const char *page_icons[PAGE_COUNT] = {
    "grid", "cpu", "memory", "gpu", "disk",
    "network", "processes", "sensors", "system",
    "service", "startup", "network", "apps",
    "chip", "disk", "gauge", "power", "record"
};

static GtkWidget *(*page_creators[PAGE_COUNT])(App *) = {
    page_overview_create,
    page_cpu_create,
    page_memory_create,
    page_gpu_create,
    page_storage_create,
    page_network_create,
    page_processes_create,
    page_sensors_create,
    page_system_create,
    page_services_create,
    page_startup_create,
    page_connections_create,
    page_apps_create,
    page_drivers_create,
    page_diskspace_create,
    page_benchmarks_create,
    page_power_create,
    page_recorder_create
};

static void (*page_updates[PAGE_COUNT])(App *) = {
    page_overview_update,
    page_cpu_update,
    page_memory_update,
    page_gpu_update,
    page_storage_update,
    page_network_update,
    page_processes_update,
    page_sensors_update,
    page_system_update,
    page_services_update,
    page_startup_update,
    page_connections_update,
    page_apps_update,
    page_drivers_update,
    page_diskspace_update,
    page_benchmarks_update,
    page_power_update,
    page_recorder_update
};

static const PageId sidebar_monitor[] = {
    PAGE_OVERVIEW, PAGE_CPU, PAGE_MEMORY, PAGE_GPU, PAGE_STORAGE, PAGE_NETWORK
};
static const PageId sidebar_system[] = {
    PAGE_PROCESSES, PAGE_SENSORS, PAGE_SYSTEM, PAGE_POWER
};
static const PageId sidebar_manage[] = {
    PAGE_SERVICES, PAGE_STARTUP, PAGE_CONNECTIONS, PAGE_APPS, PAGE_DRIVERS,
    PAGE_DISKSPACE
};
static const PageId sidebar_tools[] = {
    PAGE_BENCHMARKS, PAGE_RECORDER
};

static const PageId *sidebar_groups[] = {
    sidebar_monitor, sidebar_system, sidebar_manage, sidebar_tools
};
static const char *sidebar_group_names[] = {
    "MONITOR", "SYSTEM", "MANAGE", "TOOLS"
};
static const int sidebar_group_sizes[] = {6, 4, 6, 2};

static char *startup_page_name = NULL;
static double startup_interval = 0;
static gboolean startup_paused = FALSE;

void app_configure_startup(const char *page_name, double interval, gboolean paused) {
    g_free(startup_page_name);
    startup_page_name = page_name != NULL ? g_strdup(page_name) : NULL;
    startup_interval = interval;
    startup_paused = paused;
}

int app_page_from_name(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    static const char *slugs[PAGE_COUNT] = {
        "overview", "cpu", "memory", "gpu", "storage", "network", "processes",
        "sensors", "system", "services", "startup", "connections", "apps",
        "drivers", "diskspace", "benchmarks", "power", "recorder"
    };
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (g_ascii_strcasecmp(name, slugs[i]) == 0 ||
            g_ascii_strcasecmp(name, page_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static gboolean sidebar_collapsed(App *app) {
    return app->config.sidebar_collapsed != 0;
}

static double sidebar_group_height(App *app) {
    return sidebar_collapsed(app) ? SIDEBAR_COLLAPSED_GROUP_H : SIDEBAR_GROUP_H;
}

static double sidebar_nav_height(App *app) {
    double y = SIDEBAR_TOP;
    y += 4 * (sidebar_group_height(app) + 2.0);
    y += 18 * SIDEBAR_ITEM_STEP;
    y += 4 * 8.0;
    return y + 6.0;
}

static double sidebar_footer_start(int height) {
    double start = (double)height - SIDEBAR_FOOTER_H;
    if (start < SIDEBAR_TOP + 60.0) {
        start = SIDEBAR_TOP + 60.0;
    }
    return start;
}

static double sidebar_nav_bottom(int height) {
    return sidebar_footer_start(height) - 8.0;
}

static double sidebar_viewport(int height) {
    double viewport = sidebar_nav_bottom(height) - SIDEBAR_TOP;
    if (viewport < 60.0) {
        viewport = 60.0;
    }
    return viewport;
}

static double sidebar_max_scroll(App *app, int height) {
    double max = sidebar_nav_height(app) - sidebar_viewport(height);
    return max > 0 ? max : 0;
}

static int sidebar_build(App *app, int height, SidebarEntry *entries, int max) {
    double scroll = app->sidebar_scroll;
    double group_h = sidebar_group_height(app);
    double limit = sidebar_max_scroll(app, height);
    if (scroll > limit) {
        scroll = limit;
        app->sidebar_scroll = limit;
    }
    if (scroll < 0) {
        scroll = 0;
        app->sidebar_scroll = 0;
    }
    int count = 0;
    double y = SIDEBAR_TOP - scroll;
    for (int g = 0; g < 4; g++) {
        if (count < max) {
            entries[count++] = (SidebarEntry){0, sidebar_group_names[g], NULL, 1,
                                              y, group_h};
        }
        y += group_h + 2.0;
        for (int i = 0; i < sidebar_group_sizes[g]; i++) {
            PageId id = sidebar_groups[g][i];
            if (count < max) {
                entries[count++] = (SidebarEntry){id, page_names[id], page_icons[id],
                                                  0, y, SIDEBAR_ITEM_H};
            }
            y += SIDEBAR_ITEM_STEP;
        }
        y += 8.0;
    }

    double footer_start = sidebar_footer_start(height);
    if (count < max) {
        entries[count++] = (SidebarEntry){0, "", NULL, 2, footer_start - 4.0, 1};
    }
    double fy = footer_start;
    if (count < max) {
        entries[count++] = (SidebarEntry){ACTION_SETTINGS, "Settings", "settings",
                                          3, fy, 32};
    }
    fy += SIDEBAR_FOOTER_ROW_H;
    if (count < max) {
        entries[count++] = (SidebarEntry){ACTION_THEME, "Appearance", "palette",
                                          3, fy, 32};
    }
    fy += SIDEBAR_FOOTER_ROW_H;
    if (count < max) {
        entries[count++] = (SidebarEntry){ACTION_SHORTCUTS, "Shortcuts", "grid",
                                          3, fy, 32};
    }
    fy += SIDEBAR_FOOTER_ROW_H;
    if (count < max) {
        entries[count++] = (SidebarEntry){ACTION_ABOUT, "About", "system",
                                          3, fy, 32};
    }
    return count;
}

static double sidebar_page_y(App *app, PageId page) {
    double group_h = sidebar_group_height(app);
    double y = SIDEBAR_TOP;
    for (int g = 0; g < 4; g++) {
        y += group_h + 2.0;
        for (int i = 0; i < sidebar_group_sizes[g]; i++) {
            if (sidebar_groups[g][i] == page) {
                return y;
            }
            y += SIDEBAR_ITEM_STEP;
        }
        y += 8.0;
    }
    return -1;
}

static void sidebar_reveal(App *app, PageId page) {
    if (app->sidebar == NULL) {
        return;
    }
    int height = gtk_widget_get_height(app->sidebar);
    if (height <= 0) {
        return;
    }
    double y = sidebar_page_y(app, page);
    if (y < 0) {
        return;
    }
    double viewport = sidebar_viewport(height);
    if (y - app->sidebar_scroll < SIDEBAR_TOP) {
        app->sidebar_scroll = y - SIDEBAR_TOP;
    } else if (y + SIDEBAR_ITEM_H - app->sidebar_scroll > viewport) {
        app->sidebar_scroll = y + SIDEBAR_ITEM_H - viewport;
    }
    double limit = sidebar_max_scroll(app, height);
    if (app->sidebar_scroll < 0) {
        app->sidebar_scroll = 0;
    }
    if (app->sidebar_scroll > limit) {
        app->sidebar_scroll = limit;
    }
    gtk_widget_queue_draw(app->sidebar);
}

static int sidebar_hit(App *app, double x, double y, int height) {
    (void)x;
    SidebarEntry entries[64];
    int count = sidebar_build(app, height, entries, 64);
    double nav_bottom = sidebar_nav_bottom(height);
    for (int i = 0; i < count; i++) {
        SidebarEntry *entry = &entries[i];
        if (entry->is_group == 1 || entry->is_group == 2) {
            continue;
        }
        if (entry->is_group == 0 && entry->y + entry->h > nav_bottom + 1.0) {
            continue;
        }
        if (y >= entry->y && y <= entry->y + entry->h) {
            return entry->action;
        }
    }
    return 9999;
}

static void sidebar_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    App *app = data;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);

    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(bg, 0, t->panel2.r, t->panel2.g, t->panel2.b, 1);
    cairo_pattern_add_color_stop_rgba(bg, 1, t->bg2.r, t->bg2.g, t->bg2.b, 1);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    cairo_move_to(cr, width - 0.5, 0);
    cairo_line_to(cr, width - 0.5, height);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, t->border.r, t->border.g, t->border.b, 0.8);
    cairo_stroke(cr);

    SidebarEntry entries[64];
    int count = sidebar_build(app, height, entries, 64);
    double nav_bottom = sidebar_nav_bottom(height);

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, width, nav_bottom);
    cairo_clip(cr);
    for (int i = 0; i < count; i++) {
        SidebarEntry *entry = &entries[i];
        if (entry->y + entry->h < -20 || entry->y > nav_bottom + 20) {
            continue;
        }
        if (entry->is_group == 1) {
            if (sidebar_collapsed(app)) {
                draw_separator(cr, t, 20, entry->y + entry->h / 2.0,
                               width - 20, entry->y + entry->h / 2.0);
            } else {
                text_draw(cr, layout, entry->label, 26, entry->y + 12, 8.5,
                          PANGO_WEIGHT_BOLD, t->faint, TEXT_LEFT);
            }
            continue;
        }
        if (entry->is_group >= 2) {
            continue;
        }
        gboolean active = entry->action == (int)app->current_page;
        gboolean hover = app->sidebar_hover_action == entry->action;
        Rect item = {10, entry->y, width - 20, entry->h};
        if (active) {
            rounded_rect(cr, item, 10);
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 0.12);
            cairo_fill(cr);
            cairo_move_to(cr, 14, entry->y + 10);
            cairo_line_to(cr, 14, entry->y + entry->h - 10);
            cairo_set_line_width(cr, 3);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 1);
            cairo_stroke(cr);
        } else if (hover) {
            rounded_rect(cr, item, 10);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
            cairo_fill(cr);
        }
        Color icon_color = active ? t->accent : (hover ? t->text : t->dim);
        Rect icon_rect = {sidebar_collapsed(app) ? (width - 18) / 2.0 : 24,
                          entry->y + (entry->h - 18) / 2.0, 18, 18};
        draw_icon(cr, entry->icon, icon_rect, icon_color, 1.6);
        if (!sidebar_collapsed(app)) {
            text_draw(cr, layout, entry->label, 54, entry->y + entry->h / 2.0, 11.0,
                      active ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM,
                      active ? t->accent : (hover ? t->text : t->dim), TEXT_LEFT);
        }
    }
    cairo_restore(cr);

    draw_separator(cr, t, 20, nav_bottom + 2, width - 20, nav_bottom + 2);
    for (int i = 0; i < count; i++) {
        SidebarEntry *entry = &entries[i];
        if (entry->is_group != 3) {
            continue;
        }
        gboolean active = entry->action == (int)app->current_page;
        gboolean hover = app->sidebar_hover_action == entry->action;
        Rect item = {10, entry->y, width - 20, entry->h};
        if (hover) {
            rounded_rect(cr, item, 9);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
            cairo_fill(cr);
        }
        Color icon_color = active ? t->accent : (hover ? t->text : t->dim);
        Rect icon_rect = {sidebar_collapsed(app) ? (width - 18) / 2.0 : 24,
                          entry->y + (entry->h - 18) / 2.0, 18, 18};
        draw_icon(cr, entry->icon, icon_rect, icon_color, 1.6);
        if (!sidebar_collapsed(app)) {
            text_draw(cr, layout, entry->label, 54, entry->y + entry->h / 2.0, 11.0,
                      PANGO_WEIGHT_MEDIUM, hover ? t->text : t->dim, TEXT_LEFT);
        }
    }

    if (sidebar_collapsed(app)) {
        g_object_unref(layout);
        return;
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "v%s  \xe2\x80\xa2  GTK 4", PRISM_VERSION);
    text_draw(cr, layout, footer, 26, height - 22, 9, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_LEFT);
    char sampling[64];
    snprintf(sampling, sizeof(sampling), "SAMPLING %.1fs", app->config.interval);
    text_draw(cr, layout, sampling, width - 20, height - 22, 8.5,
              PANGO_WEIGHT_BOLD, t->faint, TEXT_RIGHT);

    g_object_unref(layout);
}

static void sidebar_motion(GtkEventControllerMotion *controller, double x, double y,
                           gpointer data) {
    App *app = data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    int height = gtk_widget_get_height(widget);
    int action = sidebar_hit(app, x, y, height);
    if (action != app->sidebar_hover_action) {
        app->sidebar_hover_action = action;
        if (sidebar_collapsed(app)) {
            const char *label = NULL;
            SidebarEntry entries[64];
            int count = sidebar_build(app, height, entries, 64);
            for (int i = 0; i < count; i++) {
                if (entries[i].action == action && entries[i].is_group != 1) {
                    label = entries[i].label;
                    break;
                }
            }
            gtk_widget_set_tooltip_text(widget, label);
        } else {
            gtk_widget_set_tooltip_text(widget, NULL);
        }
        gtk_widget_queue_draw(widget);
    }
}

static void sidebar_leave(GtkEventControllerMotion *controller, gpointer data) {
    App *app = data;
    app->sidebar_hover_action = 9999;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    gtk_widget_queue_draw(widget);
}

static gboolean sidebar_scrolled(GtkEventControllerScroll *controller, double dx,
                                 double dy, gpointer data) {
    (void)controller;
    (void)dx;
    App *app = data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    int height = gtk_widget_get_height(widget);
    app->sidebar_scroll += dy * 54.0;
    double limit = sidebar_max_scroll(app, height);
    app->sidebar_scroll = prism_clamp(app->sidebar_scroll, 0, limit);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

void app_set_sidebar_collapsed(App *app, gboolean collapsed) {
    app->config.sidebar_collapsed = collapsed ? 1 : 0;
    if (app->sidebar != NULL) {
        gtk_widget_set_size_request(app->sidebar,
                                    collapsed ? (int)SIDEBAR_COLLAPSED_WIDTH
                                              : (int)SIDEBAR_WIDTH, -1);
        gtk_widget_queue_draw(app->sidebar);
    }
    app_save_config(app);
    app_notify(app, collapsed ? "Sidebar collapsed" : "Sidebar expanded");
}

static void sidebar_toggle_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    app_set_sidebar_collapsed(app, !app->config.sidebar_collapsed);
}

static void sidebar_click(GtkGestureClick *gesture, int n_press, double x, double y,
                          gpointer data) {
    (void)gesture;
    (void)n_press;
    App *app = data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int height = gtk_widget_get_height(widget);
    int action = sidebar_hit(app, x, y, height);
    if (action == ACTION_SETTINGS) {
        app_open_settings(app);
    } else if (action == ACTION_THEME) {
        app_open_appearance(app);
    } else if (action == ACTION_SHORTCUTS) {
        app_show_shortcuts(app);
    } else if (action == ACTION_ABOUT) {
        app_show_about(app);
    } else if (action >= 0 && action < PAGE_COUNT) {
        app_navigate(app, (PageId)action);
    }
}

static void logo_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    App *app = data;
    draw_logo(cr, (Rect){1, 1, width - 2, height - 2}, &app->theme, 1.0);
}

static void header_icon_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                             gpointer data) {
    (void)area;
    App *app = data;
    Rect rect = {width * 0.18, height * 0.18, width * 0.64, height * 0.64};
    draw_icon(cr, "more", rect, app->theme.dim, 2.0);
}

static void header_sidebar_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                                int height, gpointer data) {
    (void)area;
    App *app = data;
    Rect rect = {width * 0.16, height * 0.16, width * 0.68, height * 0.68};
    draw_icon(cr, "grid", rect, app->theme.dim, 1.8);
}

static void toast_hide(App *app) {
    gtk_revealer_set_reveal_child(GTK_REVEALER(app->toast_revealer), FALSE);
    app->toast_source = 0;
}

static gboolean toast_timeout(gpointer data) {
    toast_hide(data);
    return G_SOURCE_REMOVE;
}

void app_notify(App *app, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    gtk_label_set_text(app->toast_label, buffer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(app->toast_revealer), TRUE);
    if (app->toast_source != 0) {
        g_source_remove(app->toast_source);
    }
    app->toast_source = g_timeout_add(2800, toast_timeout, app);
}

const Theme *app_theme(App *app) {
    return &app->theme;
}

double app_interval(App *app) {
    return app->config.interval;
}

void app_track_draw(App *app, GtkWidget *widget) {
    g_ptr_array_add(app->draw_widgets, widget);
}

void app_track_anim(App *app, GtkWidget *widget) {
    g_ptr_array_add(app->anim_widgets, widget);
}

static gboolean widget_on_page(App *app, GtkWidget *widget) {
    GtkWidget *root = app->pages[app->current_page].root;
    if (root == NULL) {
        return TRUE;
    }
    return widget == root || gtk_widget_is_ancestor(widget, root);
}

void app_refresh(App *app) {
    for (guint i = 0; i < app->draw_widgets->len; i++) {
        GtkWidget *widget = g_ptr_array_index(app->draw_widgets, i);
        if (!GTK_IS_WIDGET(widget) || !widget_on_page(app, widget)) {
            continue;
        }
        gtk_widget_queue_draw(widget);
    }
}

static void app_refresh_animated(App *app) {
    if (!app->config.animations) {
        return;
    }
    for (guint i = 0; i < app->anim_widgets->len; i++) {
        GtkWidget *widget = g_ptr_array_index(app->anim_widgets, i);
        if (!GTK_IS_WIDGET(widget) || !gtk_widget_get_mapped(widget) ||
            !widget_on_page(app, widget)) {
            continue;
        }
        gtk_widget_queue_draw(widget);
    }
}

double app_ease_factor(App *app) {
    return 0.08 + app->config.smoothing * 0.42;
}

int app_chart_window(App *app) {
    int window = (int)(180.0 * app->config.history_multiplier + 0.5);
    return prism_max_int(30, prism_min_int(window, PRISM_HIST));
}

void app_format_temp(App *app, double celsius, char *buf, size_t len) {
    if (app->config.temp_unit == 1) {
        snprintf(buf, len, "%.0f\xc2\xb0" "F", celsius * 9.0 / 5.0 + 32.0);
    } else {
        snprintf(buf, len, "%.0f\xc2\xb0" "C", celsius);
    }
}

const char *app_font_family(App *app) {
    return app->config.font_family;
}

void app_apply_theme(App *app) {
    theme_resolve(&app->theme, app->config.theme, app->config.accent);
    draw_set_preferences(app->config.pixels_per_update, app->config.quality,
                         app->config.graph_fill, app->config.graph_grid,
                         app->config.graph_glow);
    char *css = theme_build_css(&app->theme, app->config.compact,
                                app->config.font_family, app->config.font_scale,
                                app->config.quality);
    gtk_css_provider_load_from_string(app->css_provider, css);
    g_free(css);
    app_refresh(app);
    if (app->sidebar != NULL) {
        gtk_widget_queue_draw(app->sidebar);
    }
}

void app_save_config(App *app) {
    config_save(&app->config);
}

void app_set_paused(App *app, gboolean paused) {
    if (app == NULL) {
        return;
    }
    app->paused = paused;
    sampler_set_paused(app->sampler, paused);
    if (app->pause_button != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->pause_button),
                             paused ? "Resume" : "Pause");
    }
    app_update_header(app);
    app_notify(app, paused ? "Sampling paused — charts frozen"
                           : "Sampling resumed");
}

gboolean app_is_paused(App *app) {
    return app != NULL && app->paused;
}

void app_check_alerts(App *app) {
    if (app == NULL || !app->config.alerts_enabled) {
        return;
    }
    int fired = alerts_check(&app->alerts, app->view, 1,
                             app->config.alert_temp_c,
                             app->config.alert_disk_pct,
                             app->config.alert_mem_pct,
                             app->config.alert_battery_pct,
                             g_get_monotonic_time(), 60 * G_USEC_PER_SEC);
    if (fired & ALERT_TEMP) {
        double t = app->view->cpu.temp_c;
        if (t <= 0 && app->view->gpu_count > 0) {
            t = app->view->gpus[0].temp_c;
        }
        app_notify(app, "⚠ Temperature %.0f°C ≥ %.0f°C", t,
                   app->config.alert_temp_c);
    }
    if (fired & ALERT_DISK) {
        app_notify(app, "⚠ Disk %.0f%% full (limit %.0f%%)",
                   alerts_worst_disk_pct(app->view),
                   app->config.alert_disk_pct);
    }
    if (fired & ALERT_MEM) {
        app_notify(app, "⚠ Memory %.0f%% used (limit %.0f%%)",
                   app->view->mem.used_pct, app->config.alert_mem_pct);
    }
    if (fired & ALERT_BATTERY) {
        app_notify(app, "⚠ Battery %.0f%% (limit %.0f%%)",
                   app->view->battery.capacity_pct,
                   app->config.alert_battery_pct);
    }
}

static void export_save_done(GObject *source, GAsyncResult *result,
                             gpointer data) {
    (void)source;
    App *app = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR,
                                              GTK_DIALOG_ERROR_DISMISSED)) {
            app_notify(app, "Export cancelled");
        }
        g_clear_error(&error);
        return;
    }
    char *path = g_file_get_path(file);
    if (path != NULL) {
        if (export_snapshot_json_file(path, app->view, &app->history)) {
            app_notify(app, "Snapshot exported to %s", path);
        } else {
            app_notify(app, "Cannot write %s", path);
        }
        g_free(path);
    }
    g_object_unref(file);
}

void app_export_snapshot(App *app) {
    if (app == NULL || app->view == NULL || app->view->timestamp_us == 0) {
        if (app != NULL) {
            app_notify(app, "No sample yet — wait a second and retry");
        }
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char name[128];
    strftime(name, sizeof(name), "prism-snapshot-%Y%m%d-%H%M%S.json", &local);
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Export snapshot (JSON)");
    gtk_file_dialog_set_initial_name(dialog, name);
    gtk_file_dialog_save(dialog, GTK_WINDOW(app->window), NULL,
                         export_save_done, app);
    g_object_unref(dialog);
}

/* ---- command palette (Ctrl+K) ---- */

typedef struct {
    App *app;
    GtkWidget *entry;
    GtkWidget *list;
    char filter[128];
} PaletteState;

typedef struct {
    const char *label;
    const char *hint;
    int kind; /* 0=page, 1=action */
    int value; /* page id or action code */
} PaletteItem;

enum { PAL_PAUSE = 100, PAL_EXPORT = 101, PAL_SETTINGS = 102,
       PAL_APPEARANCE = 103, PAL_ABOUT = 104, PAL_REFRESH = 105 };

static void palette_activate(PaletteState *state, PaletteItem *item) {
    App *app = state->app;
    if (item->kind == 0) {
        app_navigate(app, (PageId)item->value);
    } else {
        switch (item->value) {
            case PAL_PAUSE: app_set_paused(app, !app->paused); break;
            case PAL_EXPORT: app_export_snapshot(app); break;
            case PAL_SETTINGS: app_open_settings(app); break;
            case PAL_APPEARANCE: app_open_appearance(app); break;
            case PAL_ABOUT: app_show_about(app); break;
            case PAL_REFRESH: sampler_invalidate_aux(app->sampler); app_notify(app, "Refreshing slow panels"); break;
            default: break;
        }
    }
    if (app->palette_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(app->palette_window));
        app->palette_window = NULL;
    }
}

static void palette_rebuild(PaletteState *state) {
    GtkWidget *child = gtk_widget_get_first_child(state->list);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(state->list), child);
        child = next;
    }
    PaletteItem items[PAGE_COUNT + 6];
    int count = 0;
    for (int i = 0; i < PAGE_COUNT; i++) {
        items[count++] = (PaletteItem){page_names[i], page_subtitles[i], 0, i};
    }
    items[count++] = (PaletteItem){"Pause / Resume sampling", "Freeze or resume live charts (Ctrl+P)", 1, PAL_PAUSE};
    items[count++] = (PaletteItem){"Export snapshot JSON", "Save current telemetry as JSON (Ctrl+E)", 1, PAL_EXPORT};
    items[count++] = (PaletteItem){"Refresh slow panels", "Re-scan services, connections, users", 1, PAL_REFRESH};
    items[count++] = (PaletteItem){"Settings", "Sampling, alerts, graphics", 1, PAL_SETTINGS};
    items[count++] = (PaletteItem){"Appearance", "Theme, accent, density", 1, PAL_APPEARANCE};
    items[count++] = (PaletteItem){"About", "Version and host info", 1, PAL_ABOUT};
    int shown = 0;
    for (int i = 0; i < count; i++) {
        if (state->filter[0] != '\0' &&
            g_strrstr(items[i].label, state->filter) == NULL &&
            g_strrstr(items[i].hint, state->filter) == NULL) {
            continue;
        }
        GtkWidget *row = gtk_list_box_row_new();
        PaletteItem *copy = g_new0(PaletteItem, 1);
        *copy = items[i];
        g_object_set_data_full(G_OBJECT(row), "palette-item", copy, g_free);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(box, 10);
        gtk_widget_set_margin_end(box, 10);
        gtk_widget_set_margin_top(box, 7);
        gtk_widget_set_margin_bottom(box, 7);
        GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        gtk_widget_set_hexpand(text, TRUE);
        GtkWidget *name = prism_label(items[i].label, "row-title");
        GtkWidget *hint = prism_label(items[i].hint, "row-sub");
        gtk_box_append(GTK_BOX(text), name);
        gtk_box_append(GTK_BOX(text), hint);
        GtkWidget *go = prism_label(items[i].kind == 0 ? "Go →" : "Run →", "faint");
        gtk_box_append(GTK_BOX(box), text);
        gtk_box_append(GTK_BOX(box), go);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(GTK_LIST_BOX(state->list), row);
        shown++;
    }
    if (shown == 0) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                   prism_label("No match — try “cpu” or “export”", "dim"));
        gtk_list_box_append(GTK_LIST_BOX(state->list), row);
    } else {
        gtk_list_box_select_row(GTK_LIST_BOX(state->list),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(state->list), 0));
    }
}

static void palette_search_changed(GtkSearchEntry *entry, gpointer data) {
    PaletteState *state = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(state->filter, text != NULL ? text : "", sizeof(state->filter));
    g_strstrip(state->filter);
    palette_rebuild(state);
}

static void palette_row_activated(GtkListBox *box, GtkListBoxRow *row,
                                  gpointer data) {
    (void)box;
    PaletteState *state = data;
    if (row == NULL) {
        return;
    }
    PaletteItem *item = g_object_get_data(G_OBJECT(row), "palette-item");
    if (item != NULL) {
        palette_activate(state, item);
    }
}

void app_open_palette(App *app) {
    if (app->palette_window != NULL) {
        gtk_window_present(GTK_WINDOW(app->palette_window));
        return;
    }
    PaletteState *state = g_new0(PaletteState, 1);
    state->app = app;
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Command palette");
    gtk_window_set_transient_for(GTK_WINDOW(window), app->window);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 520);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    app->palette_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->palette_window);
    g_object_set_data_full(G_OBJECT(window), "palette-state", state, g_free);

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *done = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(done, "suggested-action");
    g_signal_connect_swapped(done, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), done);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    state->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(state->entry),
                                          "Type a page or action… (e.g. cpu, export, pause)");
    g_signal_connect(state->entry, "search-changed",
                     G_CALLBACK(palette_search_changed), state);
    gtk_box_append(GTK_BOX(content), state->entry);
    state->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(state->list, "manage-list");
    g_signal_connect(state->list, "row-activated",
                     G_CALLBACK(palette_row_activated), state);
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), state->list);
    gtk_box_append(GTK_BOX(content), scroller);
    GtkWidget *hint = prism_label(
        "Enter runs the highlighted item. Esc closes. Pages: 18  ·  Actions: pause, export, refresh, settings.", "faint");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(content), hint);
    gtk_window_set_child(GTK_WINDOW(window), content);
    palette_rebuild(state);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(state->entry);
}

void app_update_header(App *app) {
    char live[64];
    if (app->paused) {
        snprintf(live, sizeof(live), "❚❚  PAUSED  %.1fs", app->config.interval);
    } else if (app->view->timestamp_us != 0) {
        snprintf(live, sizeof(live), "\xe2\x97\x8f  LIVE  %.1fs", app->config.interval);
    } else {
        snprintf(live, sizeof(live), "\xe2\x97\x8f  SYNCING");
    }
    gtk_label_set_text(app->live_label, live);

    char uptime[64];
    fmt_uptime(app->view->sys.uptime_s, uptime, sizeof(uptime));
    char left[512];
    snprintf(left, sizeof(left), "%s  \xc2\xb7  %s  \xc2\xb7  up %s",
             app->view->sys.hostname[0] != '\0' ? app->view->sys.hostname : "localhost",
             app->view->sys.kernel[0] != '\0' ? app->view->sys.kernel : "linux",
             uptime);
    gtk_label_set_text(app->status_left, left);

    char clock[16];
    fmt_clock_short((double)g_get_real_time() / 1000000.0, clock, sizeof(clock));
    char right[256];
    snprintf(right, sizeof(right),
             "%d processes  \xc2\xb7  %d sensors  \xc2\xb7  %s  \xc2\xb7  v%s",
             app->view->proc_count, app->view->sensor_count, clock, PRISM_VERSION);
    gtk_label_set_text(app->status_right, right);

    const Page *page = &app->pages[app->current_page];
    if (page->name != NULL) {
        gtk_label_set_text(app->title_label, page->name);
        gtk_label_set_text(app->subtitle_label, page->subtitle);
    }
}

void app_navigate(App *app, PageId page) {
    if (page < 0 || page >= PAGE_COUNT) {
        return;
    }
    app->current_page = page;
    app->config.current_page = (int)page;
    app_save_config(app);
    gtk_stack_set_visible_child_name(app->stack, page_names[page]);
    gtk_label_set_text(app->title_label, page_names[page]);
    gtk_label_set_text(app->subtitle_label, page_subtitles[page]);
    if (app->pages[page].update != NULL) {
        app->pages[page].update(app);
    }
    if (app->sidebar != NULL) {
        sidebar_reveal(app, page);
        gtk_widget_queue_draw(app->sidebar);
    }
    app_refresh(app);
}

static gboolean app_poll(gpointer data) {
    App *app = data;
    gboolean changed = sampler_copy(app->sampler, app->view, &app->history, &app->seq);
    if (changed) {
        app_check_alerts(app);
        page_recorder_capture(app);
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (app->pages[i].update != NULL) {
                app->pages[i].update(app);
            }
        }
        app_update_header(app);
        app_refresh(app);
        if (app->config.animations) {
            app->anim_until = g_get_monotonic_time() + 600000;
        }
    }
    return G_SOURCE_CONTINUE;
}

static gboolean app_tick(gpointer data) {
    App *app = data;
    if (app->config.animations && app->anim_until > g_get_monotonic_time() &&
        gtk_widget_get_mapped(GTK_WIDGET(app->window))) {
        app_refresh_animated(app);
    }
    return G_SOURCE_CONTINUE;
}

void app_update_tick(App *app) {
    if (app->tick_source != 0) {
        g_source_remove(app->tick_source);
    }
    int tick_ms = (int)(1000.0 / app->config.fps);
    if (tick_ms < 16) {
        tick_ms = 16;
    }
    app->tick_source = g_timeout_add((guint)tick_ms, app_tick, app);
}

static void search_changed(GtkSearchEntry *entry, gpointer data) {
    App *app = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(app->filter, text != NULL ? text : "", sizeof(app->filter));
    g_strstrip(app->filter);
    page_processes_update(app);
    if (app->current_page == PAGE_PROCESSES) {
        app_refresh(app);
    }
}

static void search_activate(GtkSearchEntry *entry, gpointer data) {
    (void)entry;
    App *app = data;
    app_navigate(app, PAGE_PROCESSES);
}

static void search_stop(GtkSearchEntry *entry, gpointer data) {
    (void)entry;
    App *app = data;
    app->filter[0] = '\0';
    page_processes_update(app);
    app_refresh(app);
}

static gboolean key_pressed(GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    App *app = data;
    gboolean control = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    if (control && keyval == GDK_KEY_q) {
        g_application_quit(G_APPLICATION(app->application));
        return TRUE;
    }
    if (control && keyval == GDK_KEY_f) {
        app_navigate(app, PAGE_PROCESSES);
        gtk_widget_grab_focus(app->search);
        return TRUE;
    }
    if (control && keyval == GDK_KEY_comma) {
        app_open_settings(app);
        return TRUE;
    }
    if (control && keyval == GDK_KEY_b) {
        app_set_sidebar_collapsed(app, !app->config.sidebar_collapsed);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        app_open_palette(app);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        app_set_paused(app, !app->paused);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_e || keyval == GDK_KEY_E)) {
        app_export_snapshot(app);
        return TRUE;
    }
    if (control && keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        int page = (int)(keyval - GDK_KEY_1);
        if (page < PAGE_COUNT) {
            app_navigate(app, (PageId)page);
        }
        return TRUE;
    }
    if (control && keyval == GDK_KEY_Tab) {
        int page = (int)app->current_page;
        page = shift ? (page + PAGE_COUNT - 1) % PAGE_COUNT : (page + 1) % PAGE_COUNT;
        app_navigate(app, (PageId)page);
        return TRUE;
    }
    if (keyval == GDK_KEY_F5) {
        app_notify(app, "Sampling every %.1f seconds", app->config.interval);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape) {
        if (gtk_editable_get_text(GTK_EDITABLE(app->search))[0] != '\0') {
            gtk_editable_set_text(GTK_EDITABLE(app->search), "");
        }
        return FALSE;
    }
    return FALSE;
}

static void action_settings(GSimpleAction *action, GVariant *parameter,
                            gpointer data) {
    (void)action;
    (void)parameter;
    app_open_settings(data);
}

static void action_shortcuts(GSimpleAction *action, GVariant *parameter,
                             gpointer data) {
    (void)action;
    (void)parameter;
    app_show_shortcuts(data);
}

static void action_about(GSimpleAction *action, GVariant *parameter,
                         gpointer data) {
    (void)action;
    (void)parameter;
    app_show_about(data);
}

static void action_quit(GSimpleAction *action, GVariant *parameter,
                        gpointer data) {
    (void)action;
    (void)parameter;
    App *app = data;
    g_application_quit(G_APPLICATION(app->application));
}

static void action_appearance(GSimpleAction *action, GVariant *parameter,
                              gpointer data) {
    (void)action;
    (void)parameter;
    app_open_appearance(data);
}

static void action_pause(GSimpleAction *action, GVariant *parameter,
                         gpointer data) {
    (void)action;
    (void)parameter;
    App *app = data;
    app_set_paused(app, !app->paused);
}

static void action_export(GSimpleAction *action, GVariant *parameter,
                          gpointer data) {
    (void)action;
    (void)parameter;
    app_export_snapshot(data);
}

static void action_palette(GSimpleAction *action, GVariant *parameter,
                           gpointer data) {
    (void)action;
    (void)parameter;
    app_open_palette(data);
}

static const GActionEntry app_actions[] = {
    {"settings", action_settings, NULL, NULL, NULL},
    {"shortcuts", action_shortcuts, NULL, NULL, NULL},
    {"about", action_about, NULL, NULL, NULL},
    {"quit", action_quit, NULL, NULL, NULL},
    {"appearance", action_appearance, NULL, NULL, NULL},
    {"pause", action_pause, NULL, NULL, NULL},
    {"export-snapshot", action_export, NULL, NULL, NULL},
    {"palette", action_palette, NULL, NULL, NULL},
};

static void pause_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    app_set_paused(app, !app->paused);
}

static void palette_clicked(GtkButton *button, gpointer data) {
    (void)button;
    app_open_palette(data);
}

static void export_clicked(GtkButton *button, gpointer data) {
    (void)button;
    app_export_snapshot(data);
}

static gboolean window_close(GtkWindow *window, gpointer data) {
    (void)window;
    App *app = data;
    int w = 0, h = 0;
    gtk_window_get_default_size(app->window, &w, &h);
    if (w >= 1120 && h >= 720) {
        app->config.window_width = w;
        app->config.window_height = h;
    }
    app->config.current_page = (int)app->current_page;
    app_save_config(app);
    return FALSE;
}

static GtkWidget *build_header(App *app, GtkWindow *window) {
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    app->header = GTK_HEADER_BAR(header);

    GtkWidget *sidebar_button = gtk_button_new();
    gtk_widget_add_css_class(sidebar_button, "flat");
    gtk_widget_set_tooltip_text(sidebar_button, "Toggle sidebar (Ctrl+B)");
    GtkWidget *sidebar_icon = gtk_drawing_area_new();
    gtk_widget_set_size_request(sidebar_icon, 22, 22);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sidebar_icon), header_sidebar_draw,
                                   app, NULL);
    gtk_button_set_child(GTK_BUTTON(sidebar_button), sidebar_icon);
    g_signal_connect(sidebar_button, "clicked", G_CALLBACK(sidebar_toggle_clicked),
                     app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), sidebar_button);

    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *logo = gtk_drawing_area_new();
    gtk_widget_set_size_request(logo, 28, 28);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(logo), logo_draw, app, NULL);
    app_track_draw(app, logo);
    gtk_box_append(GTK_BOX(brand), logo);
    GtkWidget *brand_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *name = prism_label("PRISM", "brand");
    GtkWidget *tagline = prism_label("SYSTEM MONITOR", "faint");
    gtk_widget_add_css_class(tagline, "subtitle");
    gtk_box_append(GTK_BOX(brand_text), name);
    gtk_box_append(GTK_BOX(brand_text), tagline);
    gtk_box_append(GTK_BOX(brand), brand_text);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), brand);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(title_box, GTK_ALIGN_CENTER);
    app->title_label = GTK_LABEL(prism_label("Overview", "title"));
    app->subtitle_label = GTK_LABEL(prism_label("Live system at a glance", "dim"));
    gtk_widget_add_css_class(GTK_WIDGET(app->subtitle_label), "subtitle");
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(app->title_label));
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(app->subtitle_label));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    GtkWidget *end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    gtk_widget_set_valign(end, GTK_ALIGN_CENTER);
    app->search = gtk_search_entry_new();
    gtk_widget_set_size_request(app->search, 230, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(app->search),
                                          "Search processes…  (Ctrl+K palette)");
    g_signal_connect(app->search, "search-changed", G_CALLBACK(search_changed), app);
    g_signal_connect(app->search, "activate", G_CALLBACK(search_activate), app);
    g_signal_connect(app->search, "stop-search", G_CALLBACK(search_stop), app);
    gtk_box_append(GTK_BOX(end), app->search);

    GtkWidget *palette_button = prism_button("⌘K", NULL);
    gtk_widget_set_tooltip_text(palette_button, "Command palette (Ctrl+K)");
    g_signal_connect(palette_button, "clicked", G_CALLBACK(palette_clicked), app);
    gtk_box_append(GTK_BOX(end), palette_button);

    app->pause_button = prism_button("Pause", NULL);
    gtk_widget_set_tooltip_text(app->pause_button,
                                "Pause / resume sampling (Ctrl+P)");
    g_signal_connect(app->pause_button, "clicked", G_CALLBACK(pause_clicked), app);
    gtk_box_append(GTK_BOX(end), app->pause_button);

    GtkWidget *export_button = prism_button("Export", NULL);
    gtk_widget_set_tooltip_text(export_button,
                                "Export snapshot JSON (Ctrl+E)");
    g_signal_connect(export_button, "clicked", G_CALLBACK(export_clicked), app);
    gtk_box_append(GTK_BOX(end), export_button);

    app->live_label = GTK_LABEL(prism_label("\xe2\x97\x8f  SYNCING", "live-pill"));
    gtk_box_append(GTK_BOX(end), GTK_WIDGET(app->live_label));

    GtkWidget *menu_button = gtk_menu_button_new();
    GtkWidget *menu_icon = gtk_drawing_area_new();
    gtk_widget_set_size_request(menu_icon, 30, 30);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(menu_icon), header_icon_draw,
                                   app, NULL);
    app_track_draw(app, menu_icon);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), menu_icon);
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Command Palette (Ctrl+K)", "app.palette");
    g_menu_append(menu, "Pause / Resume (Ctrl+P)", "app.pause");
    g_menu_append(menu, "Export Snapshot JSON (Ctrl+E)", "app.export-snapshot");
    g_menu_append(menu, "Settings", "app.settings");
    g_menu_append(menu, "Appearance", "app.appearance");
    g_menu_append(menu, "Keyboard Shortcuts", "app.shortcuts");
    g_menu_append(menu, "About Prism Monitor", "app.about");
    g_menu_append(menu, "Quit", "app.quit");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_box_append(GTK_BOX(end), menu_button);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), end);
    gtk_window_set_titlebar(window, header);
    return header;
}

App *app_create(GtkApplication *application) {
    App *app = g_new0(App, 1);
    app->application = application;
    config_load(&app->config);
    if (startup_interval > 0) {
        app->config.interval = prism_clamp(startup_interval, 0.25, 10.0);
    }
    alerts_init(&app->alerts);
    theme_resolve(&app->theme, app->config.theme, app->config.accent);
    app->draw_widgets = g_ptr_array_new();
    app->anim_widgets = g_ptr_array_new();
    app->sampler = sampler_new(app->config.interval);
    sampler_set_process_interval(app->sampler, app->config.process_interval);
    app->view = snapshot_new();
    app->sidebar_hover_action = 9999;
    app->proc_sort = 0;
    app->proc_sort_desc = 1;
    app->proc_selected_pid = -1;
    app->proc_hover_row = -1;

    app->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(app->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_application_window_new(application);
    app->window = GTK_WINDOW(window);
    gtk_window_set_title(GTK_WINDOW(window), PRISM_APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(window), app->config.window_width,
                                app->config.window_height);
    gtk_widget_set_size_request(window, 1120, 720);
    g_signal_connect(window, "close-request", G_CALLBACK(window_close), app);

    GtkWidget *logo_icon = gtk_drawing_area_new();
    gtk_widget_set_size_request(logo_icon, 64, 64);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(logo_icon), logo_draw, app, NULL);
    gtk_window_set_icon_name(GTK_WINDOW(window), "utilities-system-monitor");

    GtkWidget *header = build_header(app, app->window);
    (void)header;

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(content, TRUE);

    app->sidebar = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->sidebar,
                                app->config.sidebar_collapsed
                                    ? (int)SIDEBAR_COLLAPSED_WIDTH
                                    : (int)SIDEBAR_WIDTH,
                                -1);
    gtk_widget_set_hexpand(app->sidebar, FALSE);
    gtk_widget_set_vexpand(app->sidebar, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->sidebar), sidebar_draw,
                                   app, NULL);
    app_track_draw(app, app->sidebar);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(sidebar_motion), app);
    g_signal_connect(motion, "leave", G_CALLBACK(sidebar_leave), app);
    gtk_widget_add_controller(app->sidebar, motion);
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(sidebar_click), app);
    gtk_widget_add_controller(app->sidebar, GTK_EVENT_CONTROLLER(click));
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(sidebar_scrolled), app);
    gtk_widget_add_controller(app->sidebar, scroll);
    gtk_box_append(GTK_BOX(content), app->sidebar);

    app->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(app->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(app->stack, 170);
    gtk_widget_set_hexpand(GTK_WIDGET(app->stack), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(app->stack), TRUE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(app->stack));
    gtk_box_append(GTK_BOX(body), content);

    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(status, "statusbar");
    app->status_left = GTK_LABEL(prism_label("Waiting for first sample...", NULL));
    app->status_right = GTK_LABEL(prism_label("", NULL));
    gtk_label_set_ellipsize(app->status_left, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(status), GTK_WIDGET(app->status_left));
    gtk_box_append(GTK_BOX(status), prism_spacer());
    gtk_box_append(GTK_BOX(status), GTK_WIDGET(app->status_right));
    gtk_box_append(GTK_BOX(body), status);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), body);
    app->toast_label = GTK_LABEL(prism_label("", "toast"));
    GtkWidget *toast_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(toast_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(toast_revealer), 220);
    gtk_revealer_set_child(GTK_REVEALER(toast_revealer), GTK_WIDGET(app->toast_label));
    gtk_widget_set_halign(toast_revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(toast_revealer, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(toast_revealer, 44);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), toast_revealer);
    app->toast_revealer = toast_revealer;
    gtk_window_set_child(GTK_WINDOW(window), overlay);

    for (int i = 0; i < PAGE_COUNT; i++) {
        GtkWidget *root = page_creators[i](app);
        gtk_stack_add_named(app->stack, root, page_names[i]);
        app->pages[i] = (Page){
            .id = (PageId)i,
            .name = page_names[i],
            .subtitle = page_subtitles[i],
            .icon = page_icons[i],
            .root = root,
            .update = page_updates[i],
        };
    }

    app_apply_theme(app);
    {
        int start_page = app_page_from_name(startup_page_name);
        if (start_page < 0) {
            start_page = app->config.current_page;
        }
        if (start_page < 0 || start_page >= PAGE_COUNT) {
            start_page = PAGE_OVERVIEW;
        }
        app_navigate(app, (PageId)start_page);
    }
    app_update_header(app);
    if (startup_paused) {
        app_set_paused(app, TRUE);
    }

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(key_pressed), app);
    gtk_widget_add_controller(window, key);

    g_action_map_add_action_entries(G_ACTION_MAP(application), app_actions,
                                    G_N_ELEMENTS(app_actions), app);

    sampler_start(app->sampler);
    app->poll_source = g_timeout_add(200, app_poll, app);
    int tick_ms = (int)(1000.0 / app->config.fps);
    if (tick_ms < 16) {
        tick_ms = 16;
    }
    app->tick_source = g_timeout_add((guint)tick_ms, app_tick, app);

    gtk_window_present(GTK_WINDOW(window));
    return app;
}

void app_destroy(App *app) {
    if (app == NULL) {
        return;
    }
    if (app->poll_source != 0) {
        g_source_remove(app->poll_source);
    }
    if (app->tick_source != 0) {
        g_source_remove(app->tick_source);
    }
    if (app->toast_source != 0) {
        g_source_remove(app->toast_source);
    }
    sampler_free(app->sampler);
    snapshot_free(app->view);
    g_ptr_array_free(app->draw_widgets, TRUE);
    g_ptr_array_free(app->anim_widgets, TRUE);
    g_clear_object(&app->css_provider);
    g_free(app);
}

GtkWidget *app_page_scroller(GtkWidget *content) {
    return prism_page(content);
}
