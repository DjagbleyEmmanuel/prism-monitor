#include "ui/island.h"

#include "core/common.h"
#include "core/format.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#endif

#define ISLAND_W 340
#define ISLAND_H 56
#define ISLAND_PAD 18.0
#define ISLAND_ROW_H 26
#define ISLAND_RADIUS 28.0
#define ISLAND_SLIVER_SHORT 14
#define ISLAND_SNAP 20
#define ISLAND_DRAG_THRESHOLD 5

typedef struct {
    App *app;
    GtkWidget *window;
    GtkWidget *area;

    int last_metric;
    gint64 next_cycle_us;
    guint tick_source;
    guint retract_source;

    gboolean docked;   /* retracted to an edge sliver */
    gboolean expanded; /* tall panel */
    double anchor;     /* position along the docked edge */

    /* pointer-drag state */
    gboolean pressed;
    gboolean dragging;
    gboolean move_issued;
    int press_root_x;
    int press_root_y;
    int win_start_x;
    int win_start_y;

    /* expand / collapse animation */
    guint anim_source;
    gint64 anim_start_us;
    int anim_from_w, anim_from_h, anim_from_x, anim_from_y;
    int anim_to_w, anim_to_h, anim_to_x, anim_to_y;
} Island;

typedef struct {
    int x, y, w, h;        /* monitor geometry */
    int wx, wy, ww, wh;    /* monitor work area (excludes panels) */
} IslandArea;

typedef struct {
    int height;
    int rows;
    int rows_y;
    int spark_y;
    int buttons_y;
    int buttons_h;
} IslandLayout;

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

static void island_resize(Island *island);
static gboolean island_pointer(int *x, int *y);
static void island_area(Island *island, IslandArea *area);
static void island_anim_stop(Island *island);
static void island_anim_finish(Island *island);
static void island_place(Island *island);

/* ------------------------------------------------------------------ */
/* metrics                                                             */
/* ------------------------------------------------------------------ */

static gboolean metric_available(App *app, int i) {
    const Snapshot *snap = app->view;
    switch (metric_bits[i]) {
        case ISLAND_METRIC_GPU_UTIL:
        case ISLAND_METRIC_GPU_TEMP:
            return snap->gpu_count > 0;
        case ISLAND_METRIC_VRAM:
            return snap->gpu_count > 0 && snap->gpus[0].vram_total_mb > 0;
        case ISLAND_METRIC_BATTERY:
            return snap->battery.present;
        default:
            return TRUE;
    }
}

/* Enumerate enabled + present metrics. Returns count. */
static int enabled_metrics(App *app, int order[16]) {
    int n = 0;
    for (int i = 0; i < 11; i++) {
        if ((app->config.island_metrics & metric_bits[i]) != 0 &&
            metric_available(app, i)) {
            order[n++] = i;
        }
    }
    return n;
}

static void metric_value(App *app, int i, char *value, size_t vlen,
                         char *detail, size_t dlen) {
    const Snapshot *snap = app->view;
    value[0] = '\0';
    detail[0] = '\0';
    switch (metric_bits[i]) {
        case ISLAND_METRIC_GPU_UTIL:
            if (snap->gpu_count > 0) {
                snprintf(value, vlen, "%.0f%%", snap->gpus[0].util_pct);
                snprintf(detail, dlen, "%s utilization",
                         snap->gpus[0].kind[0] ? snap->gpus[0].kind : "gpu");
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no gpu", dlen);
            }
            break;
        case ISLAND_METRIC_GPU_TEMP:
            if (snap->gpu_count > 0 && snap->gpus[0].temp_c > 0) {
                snprintf(value, vlen, "%.0f\xC2\xB0" "C", snap->gpus[0].temp_c);
                g_strlcpy(detail, "gpu", dlen);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no sensor", dlen);
            }
            break;
        case ISLAND_METRIC_VRAM:
            if (snap->gpu_count > 0 && snap->gpus[0].vram_total_mb > 0) {
                double pct = snap->gpus[0].vram_used_mb /
                             snap->gpus[0].vram_total_mb * 100.0;
                snprintf(value, vlen, "%.0f%%", pct);
                snprintf(detail, dlen, "%.1f / %.1f GB",
                         snap->gpus[0].vram_used_mb / 1024.0,
                         snap->gpus[0].vram_total_mb / 1024.0);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no vram", dlen);
            }
            break;
        case ISLAND_METRIC_CPU_TEMP:
            if (snap->cpu.temp_c > 0) {
                snprintf(value, vlen, "%.0f\xC2\xB0" "C", snap->cpu.temp_c);
                g_strlcpy(detail, "cpu package", dlen);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no sensor", dlen);
            }
            break;
        case ISLAND_METRIC_CPU_FREQ:
            if (snap->cpu.freq_mhz >= 1000) {
                snprintf(value, vlen, "%.2f GHz", snap->cpu.freq_mhz / 1000.0);
            } else if (snap->cpu.freq_mhz > 0) {
                snprintf(value, vlen, "%.0f MHz", snap->cpu.freq_mhz);
            } else {
                g_strlcpy(value, "--", vlen);
            }
            g_strlcpy(detail, "average clock", dlen);
            break;
        case ISLAND_METRIC_CPU_UTIL:
            snprintf(value, vlen, "%.0f%%", snap->cpu.usage_pct);
            snprintf(detail, dlen, "load %.2f", snap->cpu.load1);
            break;
        case ISLAND_METRIC_MEMORY: {
            char used[24];
            snprintf(value, vlen, "%.0f%%", snap->mem.used_pct);
            fmt_bytes(snap->mem.used_mb * 1048576.0, used, sizeof(used));
            snprintf(detail, dlen, "%s used", used);
            break;
        }
        case ISLAND_METRIC_DISK: {
            double pct = alerts_worst_disk_pct(snap);
            if (pct > 0) {
                snprintf(value, vlen, "%.0f%%", pct);
                g_strlcpy(detail, "fullest mount", dlen);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no mount", dlen);
            }
            break;
        }
        case ISLAND_METRIC_NET: {
            char rx[24], tx[24];
            fmt_rate(snap->net_rx_bps, rx, sizeof(rx));
            fmt_rate(snap->net_tx_bps, tx, sizeof(tx));
            snprintf(value, vlen, "\xE2\x86\x93 %s", rx);
            snprintf(detail, dlen, "\xE2\x86\x91 %s", tx);
            break;
        }
        case ISLAND_METRIC_BATTERY:
            if (snap->battery.present) {
                snprintf(value, vlen, "%.0f%%", snap->battery.capacity_pct);
                g_strlcpy(detail,
                          snap->battery.status[0] ? snap->battery.status
                                                  : "battery", dlen);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no battery", dlen);
            }
            break;
        case ISLAND_METRIC_PING:
            if (snap->ping_ms > 0) {
                snprintf(value, vlen, "%.0f ms", snap->ping_ms);
                g_strlcpy(detail, "ping 1.1.1.1", dlen);
            } else {
                g_strlcpy(value, "--", vlen);
                g_strlcpy(detail, "no reply", dlen);
            }
            break;
        default:
            g_strlcpy(value, "--", vlen);
            break;
    }
}

static gboolean activity_active(App *app) {
    return app->island_activity[0] != '\0' &&
           app->island_activity_until > g_get_monotonic_time();
}

static int pill_metric(App *app, Island *island) {
    int order[16];
    int n = enabled_metrics(app, order);
    if (n == 0) {
        return ISLAND_METRIC_CPU_UTIL;
    }
    if (island->last_metric < 0) {
        island->last_metric = 0;
    }
    return order[island->last_metric % n];
}

/* ------------------------------------------------------------------ */
/* layout                                                              */
/* ------------------------------------------------------------------ */

static void island_layout(App *app, gboolean expanded, IslandLayout *l) {
    int order[16];
    int rows = enabled_metrics(app, order);
    if (expanded && rows > 0) {
        /* Never let the footer buttons fall off a short display. */
        IslandArea area;
        island_area(NULL, &area);
        int max_rows = (area.h - 150) / ISLAND_ROW_H;
        if (max_rows < 1) {
            max_rows = 1;
        }
        if (rows > max_rows) {
            rows = max_rows;
        }
    }
    l->rows = rows;
    l->rows_y = 74;
    l->height = ISLAND_H;
    if (expanded) {
        int body = rows > 0 ? rows * ISLAND_ROW_H : 34;
        l->spark_y = l->rows_y + body + 8;
        l->buttons_y = l->spark_y + 42;
        l->buttons_h = 28;
        l->height = l->buttons_y + l->buttons_h + 14;
    } else {
        l->spark_y = 0;
        l->buttons_y = 0;
        l->buttons_h = 0;
    }
}

static double button_x(int i) {
    return ISLAND_PAD + i * 104.0;
}

/* ------------------------------------------------------------------ */
/* X11 helpers                                                         */
/* ------------------------------------------------------------------ */

static gboolean island_x11_display(Display **dpy) {
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    if (display != NULL && GDK_IS_X11_DISPLAY(display)) {
        *dpy = GDK_DISPLAY_XDISPLAY(display);
        return TRUE;
    }
#endif
    (void)dpy;
    return FALSE;
}

static Window island_xid(Island *island) {
#ifdef GDK_WINDOWING_X11
    if (island->window != NULL && gtk_widget_get_realized(island->window)) {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
        if (surface != NULL && GDK_IS_X11_SURFACE(surface)) {
            return GDK_SURFACE_XID(surface);
        }
    }
#endif
    (void)island;
    return (Window)0;
}

static gboolean island_composited(Display *dpy) {
#ifdef GDK_WINDOWING_X11
    char name[32];
    snprintf(name, sizeof(name), "_NET_WM_CM_S%d", DefaultScreen(dpy));
    return XGetSelectionOwner(dpy, XInternAtom(dpy, name, False)) != None;
#else
    (void)dpy;
    return TRUE;
#endif
}

/* Absolute root position of the window. */
static void island_window_pos(Island *island, int *x, int *y) {
    *x = island->app->config.island_x;
    *y = island->app->config.island_y;
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    Window xid = island_xid(island);
    if (xid != 0 && island_x11_display(&dpy)) {
        Window child = None;
        int ax = 0, ay = 0;
        if (XTranslateCoordinates(dpy, xid, DefaultRootWindow(dpy), 0, 0,
                                  &ax, &ay, &child)) {
            *x = ax;
            *y = ay;
        }
    }
#endif
}

static void island_move(Island *island, int x, int y) {
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    Window xid = island_xid(island);
    if (xid != 0 && island_x11_display(&dpy)) {
        XMoveWindow(dpy, xid, x, y);
        XFlush(dpy);
    }
#else
    (void)island;
    (void)x;
    (void)y;
#endif
}

static void island_x11_raise(Island *island) {
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    Window xid = island_xid(island);
    if (xid != 0 && island_x11_display(&dpy)) {
        XRaiseWindow(dpy, xid);
        XFlush(dpy);
    }
#else
    (void)island;
#endif
}

/* Window type/states: an above-everything, skip-taskbar utility. Set before
 * the surface is mapped so the WM honours it. */
static void island_window_type(Island *island) {
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    Window xid = island_xid(island);
    if (xid == 0 || !island_x11_display(&dpy)) {
        return;
    }
    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, xid, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);

    Atom states[4] = {
        XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False),
        XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
        XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False),
        XInternAtom(dpy, "_NET_WM_STATE_STICKY", False),
    };
    XChangeProperty(dpy, xid, XInternAtom(dpy, "_NET_WM_STATE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char *)states, 4);

    unsigned long all = 0xFFFFFFFFUL;
    XChangeProperty(dpy, xid, XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&all, 1);
    XFlush(dpy);
#else
    (void)island;
#endif
}

/* Without a compositor an ARGB window shows its theme background, so fall
 * back to a 1-bit shape mask to keep the pill curved instead of square. */
static void island_shape(Island *island, int w, int h) {
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    Window xid = island_xid(island);
    if (xid == 0 || w <= 0 || h <= 0 || !island_x11_display(&dpy)) {
        return;
    }
    if (island_composited(dpy)) {
        return;
    }
    Pixmap mask = XCreatePixmap(dpy, xid, (unsigned)w, (unsigned)h, 1);
    if (mask == None) {
        return;
    }
    GC gc = XCreateGC(dpy, mask, 0, NULL);
    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, mask, gc, 0, 0, (unsigned)w, (unsigned)h);
    XSetForeground(dpy, gc, 1);
    /* Match the drawn shape: a capsule when collapsed, a gently rounded
       panel when expanded. Using h/2 for the tall panel clipped the whole
       window into an ellipse and swallowed clicks near the corners. */
    double r = island->expanded ? ISLAND_RADIUS : h / 2.0;
    if (r > w / 2.0) r = w / 2.0;
    for (int y = 0; y < h; y++) {
        double cy = -1.0;
        if (y < r) {
            cy = r - (y + 0.5);
        } else if (y >= h - r) {
            cy = (y + 0.5) - (h - r);
        }
        int inset = 0;
        if (cy >= 0.0) {
            inset = (int)(r - sqrt(r * r - cy * cy) + 0.5);
        }
        if (inset < 0) {
            inset = 0;
        }
        if (w - 2 * inset > 0) {
            XFillRectangle(dpy, mask, gc, inset, y,
                           (unsigned)(w - 2 * inset), 1);
        }
    }
    XShapeCombineMask(dpy, xid, ShapeBounding, 0, 0, mask, ShapeSet);
    XShapeCombineMask(dpy, xid, ShapeInput, 0, 0, mask, ShapeSet);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, mask);
    XFlush(dpy);
#else
    (void)island;
    (void)w;
    (void)h;
#endif
}

/* Resolve the monitor under the pointer (or under the window) and its work
 * area, so docking never lands between monitors or under a panel. */
static void island_area(Island *island, IslandArea *a) {
    a->x = 0;
    a->y = 0;
    a->w = 1920;
    a->h = 1080;
    a->wx = a->x;
    a->wy = a->y;
    a->ww = a->w;
    a->wh = a->h;

#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    if (island_x11_display(&dpy)) {
        a->w = XDisplayWidth(dpy, DefaultScreen(dpy));
        a->h = XDisplayHeight(dpy, DefaultScreen(dpy));
    }
#endif

    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = NULL;
    int rx = 0, ry = 0;
    gboolean have_pointer = display != NULL && island_pointer(&rx, &ry);
    if (have_pointer && display != NULL) {
        GListModel *monitors = gdk_display_get_monitors(display);
        guint count = monitors != NULL ? g_list_model_get_n_items(monitors) : 0;
        for (guint i = 0; i < count && monitor == NULL; i++) {
            GdkMonitor *candidate = g_list_model_get_item(monitors, i);
            if (candidate == NULL) {
                continue;
            }
            GdkRectangle g;
            gdk_monitor_get_geometry(candidate, &g);
            if (rx >= g.x && rx < g.x + g.width &&
                ry >= g.y && ry < g.y + g.height) {
                monitor = candidate;
            } else {
                g_object_unref(candidate);
            }
        }
    }
    if (monitor == NULL && display != NULL && island != NULL &&
        island->window != NULL) {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
        if (surface != NULL) {
            monitor = gdk_display_get_monitor_at_surface(display, surface);
        }
    }
    if (monitor == NULL && display != NULL) {
        GListModel *monitors = gdk_display_get_monitors(display);
        if (monitors != NULL && g_list_model_get_n_items(monitors) > 0) {
            monitor = g_list_model_get_item(monitors, 0);
        }
    }
    if (monitor == NULL) {
        return;
    }
    GdkRectangle geom, work;
    gdk_monitor_get_geometry(monitor, &geom);
    a->x = geom.x;
    a->y = geom.y;
    a->w = geom.width;
    a->h = geom.height;
    gboolean have_work = FALSE;
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_MONITOR(monitor)) {
        work = (GdkRectangle){0, 0, 0, 0};
        gdk_x11_monitor_get_workarea(monitor, &work);
        have_work = work.width > 0 && work.height > 0;
    }
#else
    (void)work;
#endif
    if (have_work) {
        a->wx = work.x;
        a->wy = work.y;
        a->ww = work.width;
        a->wh = work.height;
    } else {
        a->wx = a->x;
        a->wy = a->y;
        a->ww = a->w;
        a->wh = a->h;
    }
    g_object_unref(monitor);
}

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

static void island_size(Island *island, int w, int h) {
    if (gtk_widget_get_width(island->area) != w ||
        gtk_widget_get_height(island->area) != h) {
        gtk_widget_set_size_request(island->area, w, h);
        gtk_window_set_default_size(GTK_WINDOW(island->window), w, h);
    }
    island_shape(island, w, h);
}

/* Compute the target size and position for a state without applying it. */
static void island_geometry(Island *island, gboolean expanded, gboolean docked,
                            int *pw, int *ph, int *px, int *py) {
    App *app = island->app;
    IslandArea a;
    island_area(island, &a);
    int dock = app->config.island_dock;
    IslandLayout layout;
    island_layout(app, expanded, &layout);

    if (docked && dock != ISLAND_DOCK_FLOAT && !expanded) {
        int w = (dock == ISLAND_DOCK_TOP || dock == ISLAND_DOCK_BOTTOM)
                    ? ISLAND_W : ISLAND_SLIVER_SHORT;
        int h = (dock == ISLAND_DOCK_TOP || dock == ISLAND_DOCK_BOTTOM)
                    ? ISLAND_SLIVER_SHORT : ISLAND_H;
        *pw = w;
        *ph = h;
        if (dock == ISLAND_DOCK_TOP) {
            *px = a.wx + (a.ww - w) / 2;
            *py = a.wy;
        } else if (dock == ISLAND_DOCK_BOTTOM) {
            *px = a.wx + (a.ww - w) / 2;
            *py = a.wy + a.wh - h;
        } else {
            *px = (dock == ISLAND_DOCK_LEFT) ? a.x : a.x + a.w - w;
            *py = (int)prism_clamp(island->anchor, a.wy + 8,
                                   a.wy + a.wh - h - 8);
            if (*py < a.y + 8) *py = a.y + 8;
        }
        return;
    }

    int w = ISLAND_W;
    int h = expanded ? layout.height : ISLAND_H;
    *pw = w;
    *ph = h;
    if (dock == ISLAND_DOCK_LEFT || dock == ISLAND_DOCK_RIGHT) {
        *px = (dock == ISLAND_DOCK_LEFT) ? a.x : a.x + a.w - w;
        *py = (int)prism_clamp(island->anchor, a.wy + 8,
                               a.wy + a.wh - h - 8);
    } else if (dock == ISLAND_DOCK_TOP) {
        *px = a.wx + (a.ww - w) / 2;
        *py = a.wy;
    } else if (dock == ISLAND_DOCK_BOTTOM) {
        *px = a.wx + (a.ww - w) / 2;
        *py = a.wy + a.wh - h;
    } else {
        *px = app->config.island_x >= 0 ? app->config.island_x
                                        : a.wx + (a.ww - w) / 2;
        *py = app->config.island_y >= 0 ? app->config.island_y : a.wy + 8;
    }
    *px = (int)prism_clamp(*px, a.x, a.x + a.w - w);
    *py = (int)prism_clamp(*py, a.y, a.y + a.h - h - 4);
    if (*py < a.y + 4) {
        *py = a.y + 4;
    }
}

static void island_place(Island *island) {
    int w, h, x, y;
    island_geometry(island, island->expanded, island->docked, &w, &h, &x, &y);
    island_size(island, w, h);
    island_move(island, x, y);
    if (island->app->config.island_dock == ISLAND_DOCK_FLOAT) {
        island->app->config.island_x = x;
        island->app->config.island_y = y;
    }
    island_x11_raise(island);
}

/* --- expand / collapse animation ----------------------------------- */

static void island_anim_stop(Island *island) {
    if (island->anim_source != 0) {
        g_source_remove(island->anim_source);
        island->anim_source = 0;
    }
}

static void island_anim_finish(Island *island) {
    if (island->anim_source == 0) {
        return;
    }
    island_anim_stop(island);
    island_place(island);
}

static int island_lerp(int a, int b, double t) {
    return (int)(a + (b - a) * t + 0.5);
}

static gboolean island_anim_step(gpointer data) {
    Island *island = data;
    gint64 elapsed = g_get_monotonic_time() - island->anim_start_us;
    double t = (double)elapsed / 180000.0;
    if (t >= 1.0) {
        island->anim_source = 0;
        island_place(island);
        return G_SOURCE_REMOVE;
    }
    /* cubic ease-out: quick start, soft landing */
    double e = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    island_size(island,
                island_lerp(island->anim_from_w, island->anim_to_w, e),
                island_lerp(island->anim_from_h, island->anim_to_h, e));
    island_move(island,
                island_lerp(island->anim_from_x, island->anim_to_x, e),
                island_lerp(island->anim_from_y, island->anim_to_y, e));
    if (island->area != NULL) {
        gtk_widget_queue_draw(island->area);
    }
    return G_SOURCE_CONTINUE;
}

static void island_animate(Island *island) {
    island_anim_stop(island);
    int w, h, x, y;
    island_geometry(island, island->expanded, island->docked, &w, &h, &x, &y);
    int cw = gtk_widget_get_width(island->area);
    int ch = gtk_widget_get_height(island->area);
    int cx, cy;
    island_window_pos(island, &cx, &cy);
    if (!gtk_widget_get_visible(island->window) || cw <= 0 || ch <= 0 ||
        (cw == w && ch == h)) {
        island_place(island);
        return;
    }
    island->anim_from_w = cw;
    island->anim_from_h = ch;
    island->anim_from_x = cx;
    island->anim_from_y = cy;
    island->anim_to_w = w;
    island->anim_to_h = h;
    island->anim_to_x = x;
    island->anim_to_y = y;
    island->anim_start_us = g_get_monotonic_time();
    island->anim_source = g_timeout_add(16, island_anim_step, island);
}

static void island_resize(Island *island) {
    island_anim_stop(island);
    if (island->app->config.island_dock == ISLAND_DOCK_FLOAT) {
        island->docked = FALSE;
    }
    island_place(island);
    if (island->area != NULL) {
        gtk_widget_queue_draw(island->area);
    }
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_pill(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                      gpointer data) {
    (void)area;
    Island *island = data;
    App *app = island->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    double radius = island->expanded ? ISLAND_RADIUS : h / 2.0;

    if (island->docked) {
        rounded_rect(cr, (Rect){0, 0, w, h}, h / 2.0);
        cairo_set_source_rgba(cr, 0.01, 0.01, 0.015, 0.94);
        cairo_fill(cr);
        double pulse = 0.68 + 0.30 * sin((double)g_get_real_time() / 240000.0);
        cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, pulse);
        if (app->config.island_dock == ISLAND_DOCK_LEFT ||
            app->config.island_dock == ISLAND_DOCK_RIGHT) {
            cairo_rectangle(cr, 0, h * 0.40, w, h * 0.20);
        } else {
            cairo_rectangle(cr, w * 0.40, 0, w * 0.20, h);
        }
        cairo_fill(cr);
        g_object_unref(layout);
        return;
    }

    rounded_rect(cr, (Rect){1, 1, w - 2, h - 2}, radius);
    cairo_set_source_rgba(cr, 0.012, 0.012, 0.016, 0.97);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, (Rect){1.5, 1.5, w - 3, h - 3}, radius);
    cairo_stroke(cr);

    gboolean has_activity = activity_active(app);
    char label[40] = "PRISM";
    char value[64] = "";
    char sub[96] = "";
    char icon[32] = "gauge";
    Color accent = t->accent;

    if (has_activity) {
        if (strcmp(app->island_activity, "recorder") == 0) {
            g_strlcpy(label, "REC", sizeof(label));
            snprintf(value, sizeof(value), "%.0f samples",
                     app->island_activity_progress);
            g_strlcpy(sub, "flight recorder", sizeof(sub));
            g_strlcpy(icon, "record", sizeof(icon));
            accent = t->red;
        } else if (strcmp(app->island_activity, "bench") == 0) {
            g_strlcpy(label, "BENCH", sizeof(label));
            g_strlcpy(value, "running", sizeof(value));
            g_strlcpy(sub, "benchmark", sizeof(sub));
            g_strlcpy(icon, "gauge", sizeof(icon));
            accent = t->yellow;
        } else if (strcmp(app->island_activity, "alert") == 0) {
            g_strlcpy(label, "ALERT", sizeof(label));
            g_strlcpy(value, "check telemetry", sizeof(value));
            g_strlcpy(sub, "threshold crossed", sizeof(sub));
            g_strlcpy(icon, "alert", sizeof(icon));
            accent = t->orange;
        } else {
            g_strlcpy(label, "PRISM", sizeof(label));
            g_strlcpy(value, app->island_activity, sizeof(value));
            g_strlcpy(icon, "gauge", sizeof(icon));
        }
    } else {
        int slot = pill_metric(app, island);
        metric_value(app, slot, value, sizeof(value), sub, sizeof(sub));
        g_strlcpy(label, metric_labels[slot], sizeof(label));
        g_strlcpy(icon, metric_icons[slot], sizeof(icon));
    }

    if (!island->expanded) {
        draw_icon(cr, icon, (Rect){16, (h - 22) / 2.0, 22, 22}, accent, 1.7);
        if (has_activity) {
            double pulse = 0.5 + 0.4 * sin((double)g_get_real_time() / 160000.0);
            cairo_arc(cr, 29, h / 2.0 - 8, 4.0, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, pulse);
            cairo_fill(cr);
        }
        text_draw(cr, layout, label, 48, 21, 7.6, PANGO_WEIGHT_BOLD,
                  (Color){0.58, 0.58, 0.64}, TEXT_LEFT);
        text_draw_ellipsis(cr, layout, value, 48, 39, 15.0, PANGO_WEIGHT_BOLD,
                           (Color){0.97, 0.97, 0.99}, TEXT_LEFT, 170);
        if (sub[0] != '\0') {
            text_draw_ellipsis(cr, layout, sub, 48, 50, 6.8,
                               PANGO_WEIGHT_NORMAL, (Color){0.5, 0.5, 0.55},
                               TEXT_LEFT, 160);
        }

        /* cycle dots */
        int order[16];
        int n = enabled_metrics(app, order);
        if (n > 0) {
            int active = island->last_metric % n;
            int shown = n > 6 ? 6 : n;
            double dx = w - 46;
            for (int i = 0; i < shown; i++) {
                double cx = dx - i * 10.0;
                cairo_arc(cr, cx, h / 2.0, i == active ? 3.0 : 1.6,
                          0, 2 * G_PI);
                cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b,
                                      i == active ? 0.95 : 0.30);
                cairo_fill(cr);
            }
        }
        draw_icon(cr, "chevron_right", (Rect){w - 26, (h - 16) / 2.0, 10, 16},
                  (Color){0.72, 0.72, 0.77}, 1.8);
        g_object_unref(layout);
        return;
    }

    IslandLayout l;
    island_layout(app, TRUE, &l);

    /* header */
    draw_icon(cr, "logo", (Rect){ISLAND_PAD, 16, 20, 20}, t->accent, 1.4);
    text_draw(cr, layout, has_activity ? label : "PRISM ISLAND",
              ISLAND_PAD + 28, 25, 9.4, PANGO_WEIGHT_BOLD,
              (Color){0.62, 0.62, 0.68}, TEXT_LEFT);
    text_draw_ellipsis(cr, layout, value, ISLAND_PAD + 28, 46, 16.0,
                       PANGO_WEIGHT_BOLD, (Color){0.97, 0.97, 0.99},
                       TEXT_LEFT, w - ISLAND_PAD - 60);
    if (sub[0] != '\0') {
        text_draw_ellipsis(cr, layout, sub, w - ISLAND_PAD - 34, 25, 7.4,
                           PANGO_WEIGHT_NORMAL, (Color){0.5, 0.5, 0.55},
                           TEXT_RIGHT, 150);
    }
    /* collapse chevron */
    Rect close = {w - ISLAND_PAD - 14, 14, 20, 20};
    draw_icon(cr, "chevron_down", (Rect){close.x + 4, close.y + 3, 12, 12},
              (Color){0.75, 0.75, 0.8}, 1.8);
    draw_separator(cr, t, ISLAND_PAD, 60, w - ISLAND_PAD, 60);

    /* metric rows */
    int order[16];
    int n = enabled_metrics(app, order);
    double y = l.rows_y;
    if (n == 0) {
        text_draw(cr, layout, "No metrics enabled \xE2\x80\x94 open Settings "
                  "\xE2\x86\x92 Island",
                  ISLAND_PAD, y + 14, 8.5, PANGO_WEIGHT_NORMAL,
                  (Color){0.6, 0.6, 0.66}, TEXT_LEFT);
        y += 34;
    } else {
        for (int i = 0; i < n; i++) {
            char mv[48], md[48];
            metric_value(app, order[i], mv, sizeof(mv), md, sizeof(md));
            draw_icon(cr, metric_icons[order[i]],
                      (Rect){ISLAND_PAD, y + 5, 13, 13},
                      (Color){0.62, 0.62, 0.69}, 1.4);
            text_draw(cr, layout, metric_labels[order[i]], ISLAND_PAD + 21,
                      y + 15, 7.8, PANGO_WEIGHT_BOLD,
                      (Color){0.55, 0.55, 0.61}, TEXT_LEFT);
            if (md[0] != '\0') {
                text_draw_ellipsis(cr, layout, md, ISLAND_PAD + 106,
                                   y + 15, 7.2, PANGO_WEIGHT_NORMAL,
                                   (Color){0.44, 0.44, 0.5}, TEXT_LEFT,
                                   w - ISLAND_PAD - 130);
            }
            text_draw_ellipsis(cr, layout, mv, w - ISLAND_PAD, y + 15, 11.5,
                               PANGO_WEIGHT_BOLD, (Color){0.95, 0.95, 0.98},
                               TEXT_RIGHT, 110);
            draw_separator(cr, t, ISLAND_PAD, y + ISLAND_ROW_H - 1,
                           w - ISLAND_PAD, y + ISLAND_ROW_H - 1);
            y += ISLAND_ROW_H;
        }
    }

    /* CPU sparkline with a small heading */
    text_draw(cr, layout, "CPU", ISLAND_PAD, l.spark_y + 5, 6.8,
              PANGO_WEIGHT_BOLD, (Color){0.48, 0.48, 0.54}, TEXT_LEFT);
    {
        char cur[24];
        snprintf(cur, sizeof(cur), "%.0f%%", app->view->cpu.usage_pct);
        text_draw(cr, layout, cur, w - ISLAND_PAD, l.spark_y + 5, 6.8,
                  PANGO_WEIGHT_BOLD, (Color){0.62, 0.62, 0.68}, TEXT_RIGHT);
    }
    draw_sparkline(cr, t,
                   (Rect){ISLAND_PAD, l.spark_y + 9, w - 2 * ISLAND_PAD, 24},
                   app->history.cpu_total, &app->history, 90, t->accent, 1);

    /* footer buttons */
    const char *icons[3] = {"pause", "record", "settings"};
    const char *names[3] = {app->paused ? "Resume" : "Pause", "REC", "Settings"};
    for (int i = 0; i < 3; i++) {
        Rect br = {button_x(i), l.buttons_y, 96, l.buttons_h};
        rounded_rect(cr, br, 14);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.09);
        cairo_fill(cr);
        draw_icon(cr, icons[i], (Rect){br.x + 11, br.y + 7, 14, 14},
                  (Color){0.87, 0.87, 0.91}, 1.4);
        text_draw(cr, layout, names[i], br.x + 31, br.y + 19, 8.3,
                  PANGO_WEIGHT_MEDIUM, (Color){0.87, 0.87, 0.91}, TEXT_LEFT);
    }
    g_object_unref(layout);
}

/* ------------------------------------------------------------------ */
/* interaction                                                         */
/* ------------------------------------------------------------------ */

static gboolean island_pointer(int *x, int *y) {
#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    if (!island_x11_display(&dpy)) {
        return FALSE;
    }
    Window root = DefaultRootWindow(dpy);
    Window rr = None, child = None;
    int wx = 0, wy = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(dpy, root, &rr, &child, x, y, &wx, &wy, &mask)) {
        return FALSE;
    }
    return TRUE;
#else
    (void)x;
    (void)y;
    return FALSE;
#endif
}

static void island_toggle_expanded(Island *island, gboolean expanded) {
    App *app = island->app;
    island->expanded = expanded;
    app->config.island_expanded = expanded ? 1 : 0;
    if (expanded) {
        island->docked = FALSE;
    } else if (app->config.island_dock != ISLAND_DOCK_FLOAT) {
        island->docked = TRUE;
    }
    island_animate(island);
    app_save_config(app);
}

static void island_do_click(Island *island, double x, double y) {
    App *app = island->app;
    IslandLayout l;
    island_layout(app, island->expanded, &l);

    if (island->docked) {
        island->docked = FALSE;
        island_resize(island);
        return;
    }

    if (island->expanded) {
        if (x >= ISLAND_W - 44 && y <= 46) {
            island_toggle_expanded(island, FALSE);
            return;
        }
        for (int i = 0; i < 3; i++) {
            double bx = button_x(i);
            if (x >= bx && x <= bx + 96 &&
                y >= l.buttons_y && y <= l.buttons_y + l.buttons_h) {
                if (i == 0) {
                    app_set_paused(app, !app->paused);
                } else if (i == 1) {
                    app->config.auto_record = app->config.auto_record ? 0 : 1;
                    app_save_config(app);
                    app_notify(app, app->config.auto_record
                               ? "Flight recorder armed"
                               : "Flight recorder disarmed");
                    page_recorder_capture(app);
                } else {
                    app_open_settings(app);
                }
                gtk_widget_queue_draw(island->area);
                return;
            }
        }
        return;
    }

    if (x >= ISLAND_W - 44) {
        island_toggle_expanded(island, TRUE);
        return;
    }

    int order[16];
    int n = enabled_metrics(app, order);
    if (n > 0) {
        island->last_metric = (island->last_metric + 1) % n;
        island->next_cycle_us = g_get_monotonic_time() +
                                (gint64)(app->config.island_cycle_s *
                                         G_USEC_PER_SEC);
        gtk_widget_queue_draw(island->area);
    }
}

static void island_drag_finished(Island *island) {
    App *app = island->app;
    IslandArea a;
    island_area(island, &a);
    int w = ISLAND_W;
    int h = island->expanded ? 0 : ISLAND_H;
    if (island->expanded) {
        IslandLayout l;
        island_layout(app, TRUE, &l);
        h = l.height;
    }

    int x, y;
    island_window_pos(island, &x, &y);

    /* Snap to whichever edge is nearest, not by fixed priority: a pill
     * released near a top corner should dock to the side it hugs most. */
    int dtop = y - a.y;
    int dbottom = (a.y + a.h) - (y + h);
    int dleft = x - a.x;
    int dright = (a.x + a.w) - (x + w);
    int best = MIN(MIN(dtop, dbottom), MIN(dleft, dright));
    int dock = ISLAND_DOCK_FLOAT;
    if (best <= ISLAND_SNAP) {
        if (best == dtop) {
            dock = ISLAND_DOCK_TOP;
        } else if (best == dbottom) {
            dock = ISLAND_DOCK_BOTTOM;
        } else if (best == dleft) {
            dock = ISLAND_DOCK_LEFT;
        } else {
            dock = ISLAND_DOCK_RIGHT;
        }
    }

    app->config.island_dock = dock;
    if (dock == ISLAND_DOCK_LEFT || dock == ISLAND_DOCK_RIGHT) {
        island->anchor = y;
    } else {
        island->anchor = 0;
    }
    island->docked = dock != ISLAND_DOCK_FLOAT && !island->expanded;
    if (dock == ISLAND_DOCK_FLOAT) {
        app->config.island_x = x;
        app->config.island_y = y;
    }
    island_resize(island);
    app_save_config(app);
}

static void island_pressed(GtkGestureClick *gesture, int n_press, double x,
                           double y, gpointer data) {
    (void)gesture;
    (void)x;
    (void)y;
    if (n_press != 1) {
        return;
    }
    Island *island = data;
    island_anim_finish(island);
    island->pressed = TRUE;
    island->dragging = FALSE;
    island->move_issued = FALSE;
    island_pointer(&island->press_root_x, &island->press_root_y);
    island_window_pos(island, &island->win_start_x, &island->win_start_y);
}

static void island_released(GtkGestureClick *gesture, int n_press, double x,
                            double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    Island *island = data;
    if (!island->pressed) {
        return;
    }
    gboolean dragged = island->dragging;
    island->pressed = FALSE;
    island->dragging = FALSE;
    if (dragged) {
        island_drag_finished(island);
    } else {
        island_do_click(island, x, y);
    }
}

static void island_motion(GtkEventControllerMotion *controller, double x,
                          double y, gpointer data) {
    (void)controller;
    (void)x;
    (void)y;
    Island *island = data;
    /* cancel a pending retract when the pointer comes back */
    if (island->retract_source != 0) {
        g_source_remove(island->retract_source);
        island->retract_source = 0;
    }

    /* hover reveal from a docked sliver */
    if (island->docked && !island->pressed) {
        int rx = 0, ry = 0;
        int dock = island->app->config.island_dock;
        if (island_pointer(&rx, &ry) &&
            (dock == ISLAND_DOCK_LEFT || dock == ISLAND_DOCK_RIGHT)) {
            island->anchor = ry - ISLAND_H / 2;
        }
        island->docked = FALSE;
        island_resize(island);
        return;
    }

    if (!island->pressed) {
        return;
    }

    int rx = 0, ry = 0;
    if (!island_pointer(&rx, &ry)) {
        return;
    }
    int dx = rx - island->press_root_x;
    int dy = ry - island->press_root_y;
    if (!island->dragging) {
        if (abs(dx) + abs(dy) < ISLAND_DRAG_THRESHOLD) {
            return;
        }
        island->dragging = TRUE;
    }

#ifdef GDK_WINDOWING_X11
    Display *dpy = NULL;
    if (island_x11_display(&dpy)) {
        IslandArea a;
        island_area(island, &a);
        int w = gtk_widget_get_width(island->area);
        int h = gtk_widget_get_height(island->area);
        int nx = island->win_start_x + dx;
        int ny = island->win_start_y + dy;
        nx = (int)prism_clamp(nx, a.x, a.x + a.w - w);
        ny = (int)prism_clamp(ny, a.y, a.y + a.h - h);
        island_move(island, nx, ny);
        return;
    }
#endif

    /* Wayland: delegate the move to the compositor once per drag. */
    if (!island->move_issued) {
        GdkEvent *event = gtk_event_controller_get_current_event(
            GTK_EVENT_CONTROLLER(controller));
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(island->window));
        if (event != NULL && surface != NULL && GDK_IS_TOPLEVEL(surface)) {
            GdkDevice *device = gdk_event_get_device(event);
            if (device != NULL) {
                gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device, 1,
                                        x, y, gdk_event_get_time(event));
            }
        }
        island->move_issued = TRUE;
    }
}

static gboolean island_retract(gpointer data) {
    Island *island = data;
    island->retract_source = 0;
    if (island->docked || island->expanded ||
        island->app->config.island_dock == ISLAND_DOCK_FLOAT) {
        return G_SOURCE_REMOVE;
    }
    island->docked = TRUE;
    island_resize(island);
    return G_SOURCE_REMOVE;
}

static void island_leave(GtkEventControllerMotion *controller, gpointer data) {
    (void)controller;
    Island *island = data;
    if (island->expanded || island->pressed ||
        island->app->config.island_dock == ISLAND_DOCK_FLOAT ||
        island->docked || island->retract_source != 0) {
        return;
    }
    island->retract_source = g_timeout_add(600, island_retract, island);
}

static gboolean island_cycle_tick(gpointer data) {
    Island *island = data;
    App *app = island->app;
    int order[16];
    int n = enabled_metrics(app, order);
    gint64 now = g_get_monotonic_time();
    if (n > 0 && !island->docked && !activity_active(app) &&
        now >= island->next_cycle_us) {
        island->last_metric = (island->last_metric + 1) % n;
        island->next_cycle_us = now +
                                (gint64)(app->config.island_cycle_s *
                                         G_USEC_PER_SEC);
    }
    if (gtk_widget_get_visible(island->window)) {
        gtk_widget_queue_draw(island->area);
    }
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

void island_create(App *app) {
    if (app->island != NULL) {
        return;
    }
    Island *island = g_new0(Island, 1);
    island->app = app;
    island->expanded = app->config.island_expanded != 0;
    island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT &&
                     !island->expanded;
    island->anchor = app->config.island_y >= 0 ? app->config.island_y : 420;
    island->last_metric = 0;
    island->next_cycle_us = g_get_monotonic_time() +
                            (gint64)(app->config.island_cycle_s *
                                     G_USEC_PER_SEC);

    IslandLayout layout;
    island_layout(app, island->expanded, &layout);

    GtkWidget *window = gtk_window_new();
    island->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Prism Island");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_modal(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), FALSE);
    gtk_window_set_hide_on_close(GTK_WINDOW(window), TRUE);
    gtk_widget_set_can_focus(window, FALSE);
    gtk_widget_add_css_class(window, "island");
    gtk_window_set_default_size(GTK_WINDOW(window), ISLAND_W,
                                island->expanded ? layout.height : ISLAND_H);
    app->island = island;

    GtkWidget *area = gtk_drawing_area_new();
    island->area = area;
    gtk_widget_add_css_class(area, "island-area");
    prism_set_a11y(area, "Prism Island",
                   "Live hardware status pill. Click to cycle GPU, VRAM and "
                   "temperatures. Click the chevron to expand. Drag to move, "
                   "release near a screen edge to dock.");
    gtk_widget_set_size_request(area, ISLAND_W,
                                island->expanded ? layout.height : ISLAND_H);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_pill, island,
                                   NULL);
    gtk_widget_set_cursor_from_name(GTK_WIDGET(area), "pointer");
    gtk_widget_set_tooltip_text(GTK_WIDGET(area),
        "Prism Island — click to cycle metrics, chevron to expand, "
        "drag to move or dock on a screen edge");
    gtk_window_set_child(GTK_WINDOW(window), area);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(island_pressed), island);
    g_signal_connect(click, "released", G_CALLBACK(island_released), island);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(island_motion), island);
    g_signal_connect(motion, "leave", G_CALLBACK(island_leave), island);
    gtk_widget_add_controller(area, motion);

    /* Set WM properties before the surface is mapped. */
    gtk_widget_realize(window);
    island_window_type(island);
    gtk_widget_set_visible(window, TRUE);
    island_resize(island);

    island->tick_source = g_timeout_add(200, island_cycle_tick, island);
}

void island_destroy(App *app) {
    Island *island = app->island;
    if (island == NULL) {
        return;
    }
    island_anim_stop(island);
    if (island->tick_source != 0) {
        g_source_remove(island->tick_source);
        island->tick_source = 0;
    }
    if (island->retract_source != 0) {
        g_source_remove(island->retract_source);
        island->retract_source = 0;
    }
    if (island->window != NULL) {
        gtk_window_destroy(GTK_WINDOW(island->window));
    }
    g_free(island);
    app->island = NULL;
}

void island_show(App *app, gboolean show) {
    if (app->island == NULL) {
        return;
    }
    Island *island = app->island;
    if (!show) {
        island_anim_stop(island);
    }
    gtk_widget_set_visible(island->window, show);
    if (show) {
        gtk_widget_realize(island->window);
        island_window_type(island);
        island_resize(island);
    }
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
    if (gtk_widget_get_visible(island->window)) {
        gtk_widget_queue_draw(island->area);
    }
}

void island_set_dock(App *app, int dock) {
    app->config.island_dock = prism_clamp(dock, 0, 4);
    if (app->island != NULL) {
        Island *island = app->island;
        island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT &&
                         !island->expanded;
        island_resize(island);
    }
    app_save_config(app);
}

void island_refresh_metrics(App *app) {
    if (app->island == NULL) {
        return;
    }
    Island *island = app->island;
    int order[16];
    int n = enabled_metrics(app, order);
    if (n > 0 && island->last_metric >= n) {
        island->last_metric = 0;
    }
    if (island->expanded) {
        island_resize(island);
    }
    gtk_widget_queue_draw(island->area);
}

/* --- App-level wrappers (called from the rest of the UI) ------------- */

void app_island_toggle(App *app) {
    app->config.island_enabled = app->config.island_enabled ? 0 : 1;
    if (app->config.island_enabled) {
        if (app->island == NULL) {
            island_create(app);
        } else {
            island_show(app, TRUE);
        }
    } else if (app->island != NULL) {
        island_show(app, FALSE);
    }
    app_save_config(app);
    app_notify(app, app->config.island_enabled ? "Island shown"
                                                : "Island hidden");
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
    if (app->island == NULL) {
        return;
    }
    Island *island = app->island;
    if (app->config.island_enabled) {
        gtk_widget_set_visible(island->window, TRUE);
    }
    island->docked = app->config.island_dock != ISLAND_DOCK_FLOAT &&
                     !island->expanded;
    island_resize(island);
}
