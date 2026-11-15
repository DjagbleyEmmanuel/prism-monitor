#include "ui/island.h"

#include "core/common.h"
#include "core/format.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/widgets.h"

#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#endif

#define ISLAND_PILL_W 340.0
#define ISLAND_PILL_H 58.0
#define ISLAND_TALL_H 296.0
#define ISLAND_SLIVER 10.0
#define ISLAND_PAD 18.0

typedef struct {
    App *app;
    GtkWidget *window;
    GtkWidget *area;
    GtkGesture *drag;
    double drag_start_x;
    double drag_start_y;
    double origin_x;
    double origin_y;
    int seq;
    int metric_offset;
    gboolean docked;
    gboolean expanded;
    double sliver_anchor; /* y (or x) along edge */
    guint hide_source;
    int last_metric_index;
} Island;

static const int metric_bits[] = {
    ISLAND_METRIC_GPU_UTIL, ISLAND_METRIC_GPU_TEMP, ISLAND_METRIC_VRAM,
    ISLAND_METRIC_CPU_TEMP, ISLAND_METRIC_CPU_FREQ, ISLAND_METRIC_CPU_UTIL,
    ISLAND_METRIC_MEMORY, ISLAND_METRIC_DISK, ISLAND_METRIC_NET,
    ISLAND_METRIC_BATTERY, ISLAND_METRIC_PING
};

static const char *metric_labels[] = {
    "GPU", "GPU TEMP", "VRAM", "CPU TEMP", "CPU FREQ", "CPU",
    "MEMORY", "DISK", "NET", "BATTERY", "PING"
};

static const char *metric_icons[] = {
    "gpu", "sensors", "gpu", "sensors", "chip", "cpu",
    "memory", "disk", "network", "battery", "network"
};

static int enabled_metrics(App *app, int order[16]);
static gboolean island_retract(gpointer data);
static void island_resize(Island *island);
static void island_x11_raise(Island *island);

static int enabled_metrics(App *app, int order[16]) {
    int n = 0;
    for (int i = 0; i < 11; i++) {
        if (app->config.island_metrics & metric_bits[i]) {
            order[n++] = i;
        }
    }
    return n;
}

static void metric_value(App *app, int bit_index, char *value, size_t v_len,
                         char *detail, size_t d_len) {
    const Snapshot *snap = app->view;
    switch (metric_bits[bit_index]) {
        case ISLAND_METRIC_GPU_UTIL: {
            double util = snap->gpu_count > 0 ? snap->gpus[0].util_pct : 0;
            snprintf(value, v_len, "%.0f%%", util);
            const char *kind = snap->gpu_count > 0 ? snap->gpus[0].kind : "gpu";
            snprintf(detail, d_len, "%s util", kind);
            break;
        }
        case ISLAND_METRIC_GPU_TEMP: {
            double temp = snap->gpu_count > 0 ? snap->gpus[0].temp_c : 0;
            snprintf(value, v_len, "%.0f°C", temp);
            snprintf(detail, d_len, "GPU junction");
            break;
        }
        case ISLAND_METRIC_VRAM: {
            if (snap->gpu_count > 0 && snap->gpus[0].vram_total_mb > 0) {
                double pct = snap->gpus[0].vram_used_mb /
                             snap->gpus[0].vram_total_mb * 100.0;
                snprintf(value, v_len, "%.0f%%", pct);
                snprintf(detail, d_len, "%.1f / %.1f GB",
                         snap->gpus[0].vram_used_mb / 1024.0,
                         snap->gpus[0].vram_total_mb / 1024.0);
            } else {
                snprintf(value, v_len, "--");
                snprintf(detail, d_len, "VRAM n/a");
            }
            break;
        }
        case ISLAND_METRIC_CPU_TEMP:
            snprintf(value, v_len, "%.0f°C", snap->cpu.temp_c);
            snprintf(detail, d_len, "CPU package");
            break;
        case ISLAND_METRIC_CPU_FREQ:
            snprintf(value, v_len, "%.0f MHz", snap->cpu.freq_mhz);
            snprintf(detail, d_len, "avg clock");
            break;
        case ISLAND_METRIC_CPU_UTIL:
            snprintf(value, v_len, "%.0f%%", snap->cpu.usage_pct);
            snprintf(detail, d_len, "load %.2f", snap->cpu.load1);
            break;
        case ISLAND_METRIC_MEMORY:
            snprintf(value, v_len, "%.0f%%", snap->mem.used_pct);
            {
                char used[24];
                fmt_bytes(snap->mem.used_mb * 1048576.0, used, sizeof(used));
                snprintf(detail, d_len, "%s used", used);
            }
            break;
        case ISLAND_METRIC_DISK:
            snprintf(value, v_len, "%.0f%%", alerts_worst_disk_pct(snap));
            snprintf(detail, d_len, "fullest mount");
            break;
        case ISLAND_METRIC_NET: {
            char rx[32], tx[32];
            fmt_rate(snap->net_rx_bps, rx, sizeof(rx));
            fmt_rate(snap->net_tx_bps, tx, sizeof(tx));
            snprintf(value, v_len, "↓ %.0f", snap->net_rx_bps / 1024.0);
            snprintf(detail, d_len, "KB/s ↓ · ↑ %s", tx);
            break;
        }
        case ISLAND_METRIC_BATTERY:
            if (snap->battery.present) {
                snprintf(value, v_len, "%.0f%%", snap->battery.capacity_pct);
                snprintf(detail, d_len, "%s",
                         snap->battery.status[0] ? snap->battery.status : "battery");
            } else {
                snprintf(value, v_len, "AC");
                snprintf(detail, d_len, "no battery");
            }
            break;
        case ISLAND_METRIC_PING:
            if (snap->ping_ms > 0) {
                snprintf(value, v_len, "%.0f ms", snap->ping_ms);
                snprintf(detail, d_len, "1.1.1.1");
            } else {
                snprintf(value, v_len, "--");
                snprintf(detail, d_len, "ping");
            }
            break;
        default:
            snprintf(value, v_len, "--");
            snprintf(detail, d_len, "");
            break;
    }
}

static gboolean activity_active(App *app) {
    return app->island_activity[0] != '\0' &&
           app->island_activity_until > g_get_monotonic_time();
}

static void island_x11_props(Island *island, gboolean above) {
#ifdef GDK_WINDOWING_X11
    if (island->window == NULL || !gtk_widget_get_realized(island->window)) {
        return;
    }
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
    if (!GDK_IS_X11_SURFACE(surface)) {
        return;
    }
    Window xid = GDK_SURFACE_XID(surface);
    Display *dpy = GDK_SURFACE_XDISPLAY(surface);
    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, xid, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock, 1);
    if (above) {
        Atom above_atom = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
        Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
        XChangeProperty(dpy, xid, state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&above_atom, 1);
    }
    XFlush(dpy);
#else
    (void)island;
    (void)above;
#endif
}

static void island_x11_move(Island *island, double x, double y) {
#ifdef GDK_WINDOWING_X11
    if (island->window == NULL || !gtk_widget_get_realized(island->window)) {
        return;
    }
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
    if (!GDK_IS_X11_SURFACE(surface)) {
        return;
    }
    XMoveWindow(GDK_SURFACE_XDISPLAY(surface), GDK_SURFACE_XID(surface),
                (int)x, (int)y);
    XFlush(GDK_SURFACE_XDISPLAY(surface));
#else
    (void)island;
    (void)x;
    (void)y;
#endif
}

static void island_screen_size(App *app, int *w, int *h) {
    *w = 1920;
    *h = 1080;
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(display)) {
        Display *dpy = GDK_DISPLAY_XDISPLAY(display);
        *w = XDisplayWidth(dpy, DefaultScreen(dpy));
        *h = XDisplayHeight(dpy, DefaultScreen(dpy));
        return;
    }
#endif
    GdkMonitor *mon = gdk_display_get_monitors(gdk_display_get_default()) != NULL
        ? g_list_model_get_item(gdk_display_get_monitors(gdk_display_get_default()), 0)
        : NULL;
    if (mon != NULL) {
        GdkRectangle geom;
        gdk_monitor_get_geometry(mon, &geom);
        *w = geom.width;
        *h = geom.height;
        g_object_unref(mon);
    }
}

static void island_position(App *app, Island *island, int *x, int *y) {
    int sw, sh;
    island_screen_size(app, &sw, &sh);
    double pw = island->expanded ? ISLAND_PILL_W : ISLAND_PILL_W;
    double ph = island->expanded ? ISLAND_TALL_H : ISLAND_PILL_H;
    int dock = app->config.island_dock;
    switch (dock) {
        case ISLAND_DOCK_TOP:
            *x = (sw - (int)pw) / 2;
            *y = 12;
            break;
        case ISLAND_DOCK_BOTTOM:
            *x = (sw - (int)pw) / 2;
            *y = sh - (int)ph - 48;
            break;
        case ISLAND_DOCK_LEFT:
            *x = 0;
            *y = (int)island->sliver_anchor;
            if (*y < 40) *y = (sh - (int)ph) / 2;
            break;
        case ISLAND_DOCK_RIGHT:
            *x = sw - (int)pw;
            *y = (int)island->sliver_anchor;
            if (*y < 40) *y = (sh - (int)ph) / 2;
            break;
        default:
            *x = app->config.island_x >= 0 ? app->config.island_x
                                           : sw - (int)pw - 24;
            *y = app->config.island_y >= 0 ? app->config.island_y
                                           : sh - (int)ph - 80;
            break;
    }
}

static void island_resize(Island *island) {
    App *app = island->app;
    int dock = app->config.island_dock;
    int x, y;
    const char *css = NULL;
    (void)css;
    if (island->docked && dock != ISLAND_DOCK_FLOAT) {
        switch (dock) {
            case ISLAND_DOCK_LEFT:
            case ISLAND_DOCK_RIGHT:
                gtk_widget_set_size_request(island->area, (int)ISLAND_SLIVER, 190);
                css = "background:#000;";
                break;
            default:
                gtk_widget_set_size_request(island->area, 250, (int)ISLAND_SLIVER);
                break;
        }
        gtk_window_set_default_size(GTK_WINDOW(island->window), -1, -1);
        island_position(app, island, &x, &y);
        int sw, sh;
        island_screen_size(app, &sw, &sh);
        if (dock == ISLAND_DOCK_LEFT) {
            x = 0;
            y = (int)island->sliver_anchor;
            if (y < 0) y = (sh - 190) / 2;
        } else if (dock == ISLAND_DOCK_RIGHT) {
            x = sw - (int)ISLAND_SLIVER;
            y = (int)island->sliver_anchor;
            if (y < 0) y = (sh - 190) / 2;
        } else if (dock == ISLAND_DOCK_TOP) {
            x = (sw - 250) / 2;
            y = 0;
        } else if (dock == ISLAND_DOCK_BOTTOM) {
            x = (sw - 250) / 2;
            y = sh - (int)ISLAND_SLIVER;
        }
        island_x11_move(island, x, y);
        app->config.island_x = x;
        app->config.island_y = y;
        island_x11_raise(island);
        return;
    }
    int w = (int)ISLAND_PILL_W;
    int h = island->expanded ? (int)ISLAND_TALL_H : (int)ISLAND_PILL_H;
    gtk_widget_set_size_request(island->area, w, h);
    gtk_window_set_default_size(GTK_WINDOW(island->window), w, h);
    island_position(app, island, &x, &y);
    island_x11_move(island, x, y);
    app->config.island_x = x;
    app->config.island_y = y;
    island_x11_raise(island);
}

static const char *env_for(int dock) {
    switch (dock) {
        case ISLAND_DOCK_TOP: return "top";
        case ISLAND_DOCK_RIGHT: return "right";
        case ISLAND_DOCK_BOTTOM: return "bottom";
        case ISLAND_DOCK_LEFT: return "left";
        default: return "float";
    }
}

static void island_update_window_props(Island *island) {
    island_x11_props(island, TRUE);
    (void)env_for;
}

static void island_x11_raise(Island *island) {
#ifdef GDK_WINDOWING_X11
    if (island->window == NULL || !gtk_widget_get_realized(island->window)) {
        return;
    }
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
    if (!GDK_IS_X11_SURFACE(surface)) {
        return;
    }
    XRaiseWindow(GDK_SURFACE_XDISPLAY(surface), GDK_SURFACE_XID(surface));
    XFlush(GDK_SURFACE_XDISPLAY(surface));
#else
    (void)island;
#endif
}

static void draw_pill(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                      gpointer data) {
    (void)area;
    Island *island = data;
    App *app = island->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect full = {1, 1, w - 2, h - 2};
    int radius = h / 2;

    if (island->docked) {
        rounded_rect(cr, full, radius);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.94);
        cairo_fill(cr);
        /* accent edge pip */
        cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 0.95);
        if (app->config.island_dock == ISLAND_DOCK_LEFT ||
            app->config.island_dock == ISLAND_DOCK_RIGHT) {
            cairo_rectangle(cr, 0, h * 0.42, w, h * 0.16);
        } else {
            cairo_rectangle(cr, w * 0.42, 0, w * 0.16, h);
        }
        cairo_fill(cr);
        g_object_unref(layout);
        return;
    }

    rounded_rect(cr, full, radius);
    cairo_set_source_rgba(cr, 0.02, 0.02, 0.03, 0.97);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, (Rect){1.5, 1.5, w - 3, h - 3}, radius);
    cairo_stroke(cr);

    gboolean has_activity = activity_active(app);
    char label[40] = "PRISM";
    char value[64] = "";
    char sub[80] = "";
    char icon[32] = "gauge";
    Color accent = t->accent;
    if (has_activity) {
        if (strcmp(app->island_activity, "recorder") == 0) {
            g_strlcpy(label, "REC", sizeof(label));
            snprintf(value, sizeof(value), "%.0f samples",
                     app->island_activity_progress);
            snprintf(sub, sizeof(sub), "flight recorder");
            g_strlcpy(icon, "record", sizeof(icon));
            accent = t->red;
        } else if (strcmp(app->island_activity, "bench") == 0) {
            g_strlcpy(label, "BENCH", sizeof(label));
            snprintf(value, sizeof(value), "running");
            snprintf(sub, sizeof(sub), "benchmark");
            g_strlcpy(icon, "gauge", sizeof(icon));
            accent = t->yellow;
        } else if (strcmp(app->island_activity, "alert") == 0) {
            g_strlcpy(label, "ALERT", sizeof(label));
            snprintf(value, sizeof(value), "check telemetry");
            g_strlcpy(icon, "alert", sizeof(icon));
            accent = t->orange;
        } else {
            g_strlcpy(label, "PRISM", sizeof(label));
            snprintf(value, sizeof(value), "%s", app->island_activity);
            g_strlcpy(icon, "gauge", sizeof(icon));
        }
    } else if (island->expanded) {
        g_strlcpy(label, "PRISM ISLAND", sizeof(label));
        snprintf(value, sizeof(value), "%s", env_for(app->config.island_dock));
        g_strlcpy(icon, "logo", sizeof(icon));
    } else {
        int order[16];
        int n = enabled_metrics(app, order);
        if (n == 0) {
            order[0] = 6;
            n = 1;
        }
        int slot = order[island->last_metric_index % n];
        metric_value(app, slot, value, sizeof(value), sub, sizeof(sub));
        snprintf(label, sizeof(label), "%s", metric_labels[slot]);
        g_strlcpy(icon, metric_icons[slot], sizeof(icon));
    }

    if (!island->expanded) {
        /* leading icon */
        draw_icon(cr, icon, (Rect){ISLAND_PAD - 4, (h - 22) / 2, 22, 22},
                  accent, 1.7);
        /* pulsing dot for activities */
        if (has_activity) {
            double pulse = 0.55 + 0.35 * sin(g_get_real_time() / 180000.0);
            cairo_arc(cr, ISLAND_PAD + 14, (h / 2) - 7, 4.2, 0,
                      2 * 3.141592653589793);
            cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, pulse);
            cairo_fill(cr);
        }
        text_draw(cr, layout, label, ISLAND_PAD + 30, h / 2 - 3, 8.2,
                  PANGO_WEIGHT_BOLD, (Color){0.62, 0.62, 0.66}, TEXT_LEFT);
        text_draw(cr, layout, value, ISLAND_PAD + 30, h / 2 + 9, 15.5,
                  PANGO_WEIGHT_BOLD, (Color){0.96, 0.96, 0.98}, TEXT_LEFT);
        if (sub[0] != '\0') {
            text_draw_ellipsis(cr, layout, sub, ISLAND_PAD + 30, h - 10, 7.6,
                               PANGO_WEIGHT_NORMAL, (Color){0.55, 0.55, 0.59},
                               TEXT_LEFT, 120);
        }
        /* cycle dots */
        int order2[16];
        int n2 = enabled_metrics(app, order2);
        double dx = w - ISLAND_PAD;
        for (int i = n2 - 1; i >= 0; i--) {
            double dot_y = h / 2.0;
            double dot_x = dx - 6;
            cairo_arc(cr, dot_x, dot_y, i == island->last_metric_index % n2 ? 3.4 : 2.0,
                      0, 2 * 3.141592653589793);
            double a = i == island->last_metric_index % n2 ? 0.9 : 0.28;
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, a);
            cairo_fill(cr);
            dx -= 12;
        }
        /* expand chevron */
        draw_icon(cr, "chevron_right",
                  (Rect){w - ISLAND_PAD + 4, (h - 16) / 2, 12, 16},
                  (Color){0.75, 0.75, 0.78}, 1.8);
        g_object_unref(layout);
        return;
    }

    /* expanded */
    text_draw(cr, layout, label, ISLAND_PAD, 34, 11.5, PANGO_WEIGHT_BOLD,
              (Color){0.62, 0.62, 0.66}, TEXT_LEFT);
    text_draw(cr, layout, value, ISLAND_PAD, 56, 19, PANGO_WEIGHT_BOLD,
              (Color){0.96, 0.96, 0.98}, TEXT_LEFT);
    text_draw_ellipsis(cr, layout, sub, ISLAND_PAD, 74, 8.6,
                       PANGO_WEIGHT_NORMAL, (Color){0.55, 0.55, 0.59},
                       TEXT_LEFT, w - 2 * ISLAND_PAD);

    double y = 100;
    int order3[16];
    int n3 = enabled_metrics(app, order3);
    int shown = 0;
    for (int i = 0; i < n3 && shown < 5; i++) {
        char mv[40], md[48];
        metric_value(app, order3[i], mv, sizeof(mv), md, sizeof(md));
        draw_icon(cr, metric_icons[order3[i]], (Rect){ISLAND_PAD, y, 14, 14},
                  (Color){0.65, 0.65, 0.70}, 1.4);
        text_draw(cr, layout, metric_labels[order3[i]], ISLAND_PAD + 22, y + 10,
                  8.0, PANGO_WEIGHT_BOLD, (Color){0.6, 0.6, 0.66}, TEXT_LEFT);
        text_draw(cr, layout, mv, w - ISLAND_PAD, y + 10, 11.5,
                  PANGO_WEIGHT_BOLD, (Color){0.95, 0.95, 0.97}, TEXT_RIGHT);
        y += 30;
        shown++;
    }

    /* cpu sparkline */
    Rect spark = {ISLAND_PAD, y + 4, w - 2 * ISLAND_PAD, 34};
    draw_sparkline(cr, t, spark, app->history.cpu_total, &app->history, 90,
                   t->accent, 1);
    y = spark.y + spark.h + 14;

    /* action buttons */
    const char *btns[] = {"pause", "record", "settings"};
    const char *tools[] = {"Pause", "REC", "Settings"};
    double bx = ISLAND_PAD;
    for (int i = 0; i < 3; i++) {
        Rect br = {bx, y, 88, 26};
        rounded_rect(cr, br, 13);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
        cairo_fill(cr);
        draw_icon(cr, btns[i], (Rect){bx + 10, y + 6, 14, 14},
                  (Color){0.85, 0.85, 0.88}, 1.4);
        text_draw(cr, layout, tools[i], bx + 30, y + 17, 8.5,
                  PANGO_WEIGHT_MEDIUM, (Color){0.85, 0.85, 0.88}, TEXT_LEFT);
        bx += 96;
    }
    g_object_unref(layout);
}

static gboolean island_leave(GtkEventControllerMotion *controller,
                             gpointer data) {
    Island *island = data;
    if (!island->docked || island->hide_source != 0) {
        return FALSE;
    }
    island->hide_source = g_timeout_add(800, island_retract, island);
    return FALSE;
}

static gboolean island_enter(GtkEventControllerMotion *controller,
                             double x, double y, gpointer data) {
    Island *island = data;
    if (island->hide_source != 0) {
        g_source_remove(island->hide_source);
        island->hide_source = 0;
    }
    if (island->docked) {
        island->docked = FALSE;
        island_resize(island);
        gtk_widget_queue_draw(island->area);
    }
    return FALSE;
}

static gboolean island_retract(gpointer data) {
    Island *island = data;
    island->hide_source = 0;
    if (island->docked) {
        return G_SOURCE_REMOVE;
    }
    island->docked = TRUE;
    island_resize(island);
    gtk_widget_queue_draw(island->area);
    return G_SOURCE_REMOVE;
}

static void drag_begin(GtkGestureDrag *gesture, double x, double y,
                       gpointer data) {
    Island *island = data;
    island->drag_start_x = x;
    island->drag_start_y = y;
    island->origin_x = island->app->config.island_x >= 0 ? island->app->config.island_x : 100;
    island->origin_y = island->app->config.island_y >= 0 ? island->app->config.island_y : 100;
#ifndef GDK_WINDOWING_X11
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
    if (GDK_IS_TOPLEVEL(surface)) {
        GdkDevice *device = gtk_gesture_get_device(GTK_GESTURE(gesture));
        gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device,
                                gtk_gesture_get_current_event_time(GTK_GESTURE(gesture)),
                                x, y);
    }
#endif
}

static void drag_update(GtkGestureDrag *gesture, double offset_x,
                        double offset_y, gpointer data) {
    Island *island = data;
    App *app = island->app;
#ifdef GDK_WINDOWING_X11
    double x = island->origin_x + offset_x;
    double y = island->origin_y + offset_y;
    int sw, sh;
    island_screen_size(app, &sw, &sh);
    x = prism_clamp(x, 0, sw - 80);
    y = prism_clamp(y, 0, sh - 40);
    if (app->config.island_dock != ISLAND_DOCK_FLOAT &&
        island->docked) {
        /* undock when user drags away from edge */
        island->docked = FALSE;
        island_resize(island);
    }
    island_x11_move(island, x, y);
    app->config.island_x = (int)x;
    app->config.island_y = (int)y;
#endif
}

static void drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
                     gpointer data) {
    Island *island = data;
    App *app = island->app;
    int sw, sh;
    island_screen_size(app, &sw, &sh);
    double x = island->origin_x + offset_x;
    double y = island->origin_y + offset_y;
    /* dock when near an edge */
    if (y < 32) {
        app->config.island_dock = ISLAND_DOCK_TOP;
    } else if (y > sh - (int)ISLAND_PILL_H - 24) {
        app->config.island_dock = ISLAND_DOCK_BOTTOM;
    } else if (x < 24) {
        app->config.island_dock = ISLAND_DOCK_LEFT;
    } else if (x > sw - ISLAND_PILL_W - 24) {
        app->config.island_dock = ISLAND_DOCK_RIGHT;
    } else {
        app->config.island_dock = ISLAND_DOCK_FLOAT;
    }
    if (app->config.island_dock == ISLAND_DOCK_LEFT ||
        app->config.island_dock == ISLAND_DOCK_RIGHT) {
        island->sliver_anchor = y;
    }
    island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT;
    island_resize(island);
    app_save_config(app);
    gtk_widget_queue_draw(island->area);
}

static void island_click(GtkGestureClick *gesture, int n_press, double x,
                         double y, gpointer data) {
    (void)n_press;
    Island *island = data;
    App *app = island->app;
    int w = gtk_widget_get_width(island->area);
    int h = gtk_widget_get_height(island->area);

    if (island->expanded) {
        double bx = ISLAND_PAD;
        int btn = -1;
        for (int i = 0; i < 3; i++) {
            if (x >= bx && x <= bx + 88 && y >= h - 46 && y <= h - 18) {
                btn = i;
                break;
            }
            bx += 96;
        }
        if (btn == 0) {
            app_set_paused(app, !app->paused);
            return;
        }
        if (btn == 1) {
            app->config.auto_record = app->config.auto_record ? 0 : 1;
            app_save_config(app);
            page_recorder_capture(app);
            app_notify(app, app->config.auto_record
                       ? "Flight recorder armed"
                       : "Flight recorder disarmed");
            return;
        }
        if (btn == 2) {
            app_open_settings(app);
            return;
        }
        if (x >= w - ISLAND_PAD - 20 && y <= 46) {
            island->expanded = FALSE;
            app->config.island_expanded = 0;
            island_resize(island);
            app_save_config(app);
            gtk_widget_queue_draw(island->area);
            return;
        }
        return;
    }

    if (x >= w - ISLAND_PAD - 26) {
        /* chevron toggles expand */
        island->expanded = TRUE;
        app->config.island_expanded = 1;
        island_resize(island);
        app_save_config(app);
        gtk_widget_queue_draw(island->area);
        return;
    }
    /* body click cycles metric */
    int order[16];
    int n = enabled_metrics(app, order);
    if (n > 0) {
        island->last_metric_index = (island->last_metric_index + 1) % n;
        gtk_widget_queue_draw(island->area);
    }
}

void island_create(App *app) {
    Island *island = g_new0(Island, 1);
    island->app = app;
    island->expanded = app->config.island_expanded != 0;
    island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT &&
                     !island->expanded;
    island->sliver_anchor = 480;
    island->metric_offset = 0;

    GtkWidget *window = gtk_window_new();
    island->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Prism Island");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_modal(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), FALSE);
    gtk_window_set_hide_on_close(GTK_WINDOW(window), TRUE);
    app->island = island;

    GtkWidget *area = gtk_drawing_area_new();
    island->area = area;
    prism_set_a11y(area, "Prism Island",
                   "Live hardware status pill. Click to cycle GPU, VRAM and temperatures. Click the chevron to expand. Drag to dock to any screen edge.");
    gtk_widget_set_size_request(area, (int)ISLAND_PILL_W,
                                island->expanded ? (int)ISLAND_TALL_H
                                                 : (int)ISLAND_PILL_H);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_pill, island, NULL);
    gtk_window_set_child(GTK_WINDOW(window), area);

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "released", G_CALLBACK(island_click), island);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), island);
    g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), island);
    g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), island);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    island->drag = drag;

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(island_enter), island);
    g_signal_connect(motion, "leave", G_CALLBACK(island_leave), island);
    gtk_widget_add_controller(area, motion);

    island_update_window_props(island);
    island->sliver_anchor = 480;
    gtk_widget_set_visible(window, TRUE);
    /* Now realized: apply theme border, position and size for real */
    island_resize(island);
    island_update_window_props(island);
    island_x11_raise(island);
}

void island_destroy(App *app) {
    Island *island = app->island;
    if (island == NULL) {
        return;
    }
    if (island->hide_source != 0) {
        g_source_remove(island->hide_source);
    }
    gtk_window_destroy(GTK_WINDOW(island->window));
    g_free(island);
    app->island = NULL;
}

void island_show(App *app, gboolean show) {
    if (app->island == NULL) {
        return;
    }
    Island *island = app->island;
    gtk_widget_set_visible(island->window, show);
    if (show) {
        island_resize(island);
        island_x11_raise(island);
        gtk_widget_queue_draw(island->area);
    }
}

void island_toggle(App *app) {
    if (app->island == NULL) {
        island_create(app);
        return;
    }
    gboolean vis = gtk_widget_get_visible(((Island *)app->island)->window);
    app->config.island_enabled = !vis;
    island_show(app, !vis);
    app_save_config(app);
    app_notify(app, !vis ? "Island shown" : "Island hidden");
}

void island_raise(App *app) {
    if (app->island != NULL) {
        island_x11_raise((Island *)app->island);
    }
}

void island_tick(App *app) {
    if (app->island == NULL) {
        return;
    }
    Island *island = app->island;
    int order[16];
    int n = enabled_metrics(app, order);
    if (n > 0) {
        island->seq++;
        if (island->seq >= (int)(app->config.island_cycle_s * 2.0)) {
            island->seq = 0;
            island->last_metric_index = (island->last_metric_index + 1) % n;
        }
    }
    gtk_widget_queue_draw(island->area);
}

void island_set_dock(App *app, int dock) {
    app->config.island_dock = prism_clamp(dock, 0, 4);
    if (app->island == NULL) {
        island_create(app);
    } else {
        Island *island = app->island;
        island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT;
        island->sliver_anchor = 420;
        island_resize(island);
        gtk_widget_queue_draw(island->area);
    }
    app_save_config(app);
}

void app_island_toggle(App *app) {
    app->config.island_enabled = app->config.island_enabled ? 0 : 1;
    if (app->island == NULL && app->config.island_enabled) {
        island_create(app);
    } else if (app->island != NULL) {
        island_show(app, app->config.island_enabled);
    }
    app_save_config(app);
    app_notify(app, app->config.island_enabled ? "Island enabled"
                                                : "Island disabled");
}

void app_island_set_visible(App *app, gboolean visible) {
    island_show(app, visible);
}

void app_island_activity_set(App *app, const char *what, double progress) {
    g_strlcpy(app->island_activity, what != NULL ? what : "",
              sizeof(app->island_activity));
    app->island_activity_progress = progress;
    app->island_activity_until = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    if (app->island != NULL) {
        gtk_widget_queue_draw(((Island *)app->island)->area);
    }
}

void app_island_refresh_config(App *app) {
    if (app->island != NULL) {
        Island *island = app->island;
        island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT;
        island_resize(island);
        gtk_widget_queue_draw(island->area);
    }
}

void island_refresh_metrics(App *app) {
    app_island_refresh_config(app);
}
