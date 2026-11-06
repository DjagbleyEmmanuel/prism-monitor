#include "core/common.h"
#include "core/format.h"
#include "ui/app.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    GAUGE_CPU,
    GAUGE_MEMORY,
    GAUGE_GPU,
    GAUGE_DISK,
    GAUGE_NET_RX,
    GAUGE_NET_TX
} GaugeKind;

typedef enum {
    STAT_LOAD,
    STAT_TEMP,
    STAT_PRESSURE,
    STAT_PROCESSES,
    STAT_BATTERY,
    STAT_UPTIME
} StatKind;

typedef struct {
    App *app;
    GaugeKind kind;
    HoverInfo hover;
    double shown;
} GaugeState;

typedef struct {
    App *app;
    StatKind kind;
    HoverInfo hover;
    double shown;
} StatState;

typedef struct {
    App *app;
    HoverInfo hover;
} ChartState;

typedef struct {
    App *app;
    HoverInfo hover;
    int pids[8];
    int count;
} TopState;

typedef struct {
    App *app;
    HoverInfo hover;
} PluginsState;

typedef struct {
    App *app;
    GaugeState gauges[6];
    StatState stats[6];
    ChartState cpu_chart;
    ChartState net_chart;
    ChartState disk_chart;
    TopState top;
    PluginsState plugins;
} OverviewState;

static void navigate_gauge(GtkGestureClick *gesture, int n_press, double x,
                           double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    GaugeState *state = data;
    switch (state->kind) {
        case GAUGE_CPU:
            app_navigate(state->app, PAGE_CPU);
            break;
        case GAUGE_MEMORY:
            app_navigate(state->app, PAGE_MEMORY);
            break;
        case GAUGE_GPU:
            app_navigate(state->app, PAGE_GPU);
            break;
        case GAUGE_DISK:
            app_navigate(state->app, PAGE_STORAGE);
            break;
        case GAUGE_NET_RX:
        case GAUGE_NET_TX:
            app_navigate(state->app, PAGE_NETWORK);
            break;
    }
}

static void navigate_click(GtkGestureClick *gesture, int n_press, double x,
                           double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    ChartState *state = data;
    if (state->app->current_page == PAGE_OVERVIEW) {
        app_navigate(state->app, PAGE_CPU);
    }
}

static void top_click(GtkGestureClick *gesture, int n_press, double x, double y,
                      gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    TopState *top = data;
    int row = (int)((y - 44) / 36);
    if (row >= 0 && row < top->count && row < 8) {
        top->app->proc_selected_pid = top->pids[row];
        app_navigate(top->app, PAGE_PROCESSES);
    }
}

static const MountInfo *root_mount(App *app) {
    const MountInfo *best = NULL;
    for (int i = 0; i < app->view->mount_count; i++) {
        if (strcmp(app->view->mounts[i].mount, "/") == 0) {
            return &app->view->mounts[i];
        }
        if (best == NULL || app->view->mounts[i].used_pct > best->used_pct) {
            best = &app->view->mounts[i];
        }
    }
    return best;
}

static const GpuInfo *primary_gpu(App *app) {
    for (int i = 0; i < app->view->gpu_count; i++) {
        if (app->view->gpus[i].present && strcmp(app->view->gpus[i].kind, "nvidia") == 0) {
            return &app->view->gpus[i];
        }
    }
    for (int i = 0; i < app->view->gpu_count; i++) {
        if (app->view->gpus[i].present) {
            return &app->view->gpus[i];
        }
    }
    return NULL;
}

static float series_max(const float *series, const History *hist, int window) {
    int count = history_count(hist);
    int shown = prism_min_int(count, window);
    float max = 0;
    for (int i = 0; i < shown; i++) {
        float value = history_get(series, hist, i);
        if (value > max) {
            max = value;
        }
    }
    return max;
}

static void draw_card_heading(cairo_t *cr, PangoLayout *layout, const Theme *t,
                              Rect rect, const char *icon, const char *title,
                              Color accent) {
    draw_icon(cr, icon, (Rect){rect.x + 16, rect.y + 14, 15, 15}, accent, 1.7);
    text_draw(cr, layout, title, rect.x + 38, rect.y + 22, 10.5,
              PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
}

static void gauge_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    GaugeState *g = data;
    App *app = g->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->accent;
    const char *icon = "cpu";
    const char *title = "CPU";
    char value[32] = "--";
    char unit[16] = "";
    char caption[160] = "";
    double target = 0;

    switch (g->kind) {
        case GAUGE_CPU: {
            accent = t->accent;
            icon = "cpu";
            title = "CPU";
            target = app->view->cpu.usage_pct;
            snprintf(value, sizeof(value), "%.0f", g->shown);
            snprintf(unit, sizeof(unit), "%%");
            snprintf(caption, sizeof(caption), "user %.0f%%  \xc2\xb7  sys %.0f%%",
                     app->view->cpu.user_pct, app->view->cpu.system_pct);
            break;
        }
        case GAUGE_MEMORY: {
            accent = t->purple;
            icon = "memory";
            title = "MEMORY";
            target = app->view->mem.used_pct;
            snprintf(value, sizeof(value), "%.0f", g->shown);
            snprintf(unit, sizeof(unit), "%%");
            char used[32], total[32], used_unit[16], total_unit[16];
            fmt_bytes_parts(app->view->mem.used_mb * 1048576.0, used, sizeof(used), used_unit, sizeof(used_unit));
            fmt_bytes_parts(app->view->mem.total_mb * 1048576.0, total, sizeof(total), total_unit, sizeof(total_unit));
            snprintf(caption, sizeof(caption), "%s %s of %s %s",
                     used, used_unit, total, total_unit);
            break;
        }
        case GAUGE_GPU: {
            accent = t->green;
            icon = "gpu";
            title = "GPU";
            const GpuInfo *gpu = primary_gpu(app);
            if (gpu != NULL) {
                target = gpu->util_pct;
                snprintf(value, sizeof(value), "%.0f", g->shown);
                snprintf(unit, sizeof(unit), "%%");
                if (strcmp(gpu->kind, "nvidia") == 0) {
                    snprintf(caption, sizeof(caption), "%s  \xc2\xb7  %.0f\xc2\xb0" "C",
                             gpu->name, gpu->temp_c);
                } else {
                    snprintf(caption, sizeof(caption), "%s  \xc2\xb7  telemetry limited", gpu->name);
                }
            } else {
                snprintf(value, sizeof(value), "--");
                snprintf(caption, sizeof(caption), "No GPU detected");
            }
            break;
        }
        case GAUGE_DISK: {
            accent = t->orange;
            icon = "disk";
            title = "STORAGE";
            const MountInfo *mount = root_mount(app);
            if (mount != NULL) {
                target = mount->used_pct;
                snprintf(value, sizeof(value), "%.0f", g->shown);
                snprintf(unit, sizeof(unit), "%%");
                char used[24], unit_buf[16], total[24], total_unit[16];
                fmt_bytes_parts(mount->used_mb * 1048576.0, used, sizeof(used), unit_buf, sizeof(unit_buf));
                fmt_bytes_parts(mount->total_mb * 1048576.0, total, sizeof(total), total_unit, sizeof(total_unit));
                snprintf(caption, sizeof(caption), "%s %s / %s %s  \xc2\xb7  %s",
                         used, unit_buf, total, total_unit,
                         strcmp(mount->mount, "/") == 0 ? "root" : mount->mount);
            }
            break;
        }
        case GAUGE_NET_RX:
        case GAUGE_NET_TX: {
            gboolean rx = g->kind == GAUGE_NET_RX;
            accent = rx ? t->blue : t->cyan;
            icon = "network";
            title = rx ? "NETWORK IN" : "NETWORK OUT";
            double rate = rx ? app->view->net_rx_bps : app->view->net_tx_bps;
            double peak = series_max(rx ? app->history.net_rx : app->history.net_tx,
                                     &app->history, 120);
            target = peak > 1 ? rate / peak * 100.0 : 0;
            char unit_text[16];
            fmt_rate_parts(rate, value, sizeof(value), unit_text, sizeof(unit_text));
            snprintf(unit, sizeof(unit), "%s", unit_text);
            unsigned long long total = 0;
            int active = 0;
            for (int i = 0; i < app->view->nic_count; i++) {
                total += rx ? app->view->nics[i].rx_total : app->view->nics[i].tx_total;
                if (strcmp(app->view->nics[i].state, "up") == 0) {
                    active++;
                }
            }
            char total_text[32];
            fmt_bytes((double)total, total_text, sizeof(total_text));
            snprintf(caption, sizeof(caption), "%s  \xc2\xb7  %d active", total_text, active);
            break;
        }
    }

    gboolean hovered = g->hover.active;
    if (app->config.animations) {
        g->shown = prism_ease(g->shown, target, app_ease_factor(app));
    } else {
        g->shown = target;
    }
    if (g->kind == GAUGE_CPU || g->kind == GAUGE_MEMORY || g->kind == GAUGE_GPU) {
        snprintf(value, sizeof(value), "%.0f", g->shown);
    }

    draw_card_accent(cr, t, rect, 16, accent, hovered ? 1.0 : 0.38);
    draw_card_heading(cr, layout, t, rect, icon, title, accent);
    double radius = fmin((width - 66) * 0.5, 52.0);
    double cy = height * 0.54;
    draw_gauge(cr, layout, t, width / 2.0, cy, radius, 9.0, g->shown,
               accent, value, unit);
    if (caption[0] != '\0') {
        text_draw_ellipsis(cr, layout, caption, width / 2.0, height - 15, 9,
                           PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER,
                           width - 28);
    }
    g_object_unref(layout);
}

static void stat_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    StatState *s = data;
    App *app = s->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->accent;
    const char *icon = "cpu";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[128] = "";
    double target = 0;

    switch (s->kind) {
        case STAT_LOAD:
            accent = t->yellow;
            icon = "cpu";
            title = "LOAD AVERAGE";
            target = app->view->cpu.load1;
            snprintf(value, sizeof(value), "%.2f", s->shown);
            snprintf(sub, sizeof(sub), "5m %.2f  \xc2\xb7  15m %.2f",
                     app->view->cpu.load5, app->view->cpu.load15);
            break;
        case STAT_TEMP:
            accent = app->view->cpu.temp_c >= 80 ? t->red :
                     (app->view->cpu.temp_c >= 65 ? t->yellow : t->green);
            icon = "sensors";
            title = "CPU TEMPERATURE";
            target = app->view->cpu.temp_c;
            snprintf(value, sizeof(value), "%.0f", s->shown);
            snprintf(unit, sizeof(unit), "\xc2\xb0" "C");
            snprintf(sub, sizeof(sub), "package temperature");
            break;
        case STAT_PRESSURE:
            accent = t->accent2;
            icon = "system";
            title = "MEMORY PRESSURE";
            target = app->view->mem.psi_some10;
            snprintf(value, sizeof(value), "%.1f", s->shown);
            snprintf(unit, sizeof(unit), "%%");
            snprintf(sub, sizeof(sub), "PSI some 10s  \xc2\xb7  full %.1f%%",
                     app->view->mem.psi_full10);
            break;
        case STAT_PROCESSES:
            accent = t->cyan;
            icon = "processes";
            title = "PROCESSES";
            target = app->view->proc_count;
            snprintf(value, sizeof(value), "%d", app->view->proc_count);
            unsigned long long threads = 0;
            for (int i = 0; i < app->view->proc_count; i++) {
                threads += app->view->procs[i].threads;
            }
            snprintf(sub, sizeof(sub), "%llu running  \xc2\xb7  %llu threads",
                     app->view->cpu.procs_running, threads);
            break;
        case STAT_BATTERY:
            accent = app->view->battery.capacity_pct <= 20 ? t->red :
                     (app->view->battery.capacity_pct <= 45 ? t->yellow : t->green);
            icon = "battery";
            title = "BATTERY";
            target = app->view->battery.present ? app->view->battery.capacity_pct : 0;
            if (app->view->battery.present) {
                snprintf(value, sizeof(value), "%.0f", s->shown);
                snprintf(unit, sizeof(unit), "%%");
                char duration[32];
                fmt_duration(app->view->battery.time_remaining_s, duration, sizeof(duration));
                snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s",
                         app->view->battery.status,
                         app->view->battery.time_remaining_s > 0 ? duration : "AC");
            } else {
                snprintf(value, sizeof(value), "--");
                snprintf(sub, sizeof(sub), "no battery");
            }
            break;
        case STAT_UPTIME: {
            accent = t->purple;
            icon = "system";
            title = "UPTIME";
            target = app->view->sys.uptime_s;
            char duration[64];
            fmt_uptime(app->view->sys.uptime_s, duration, sizeof(duration));
            snprintf(value, sizeof(value), "%s", duration);
            char boot[32];
            fmt_clock_short(app->view->sys.boot_time, boot, sizeof(boot));
            snprintf(sub, sizeof(sub), "booted at %s  \xc2\xb7  %s",
                     boot, app->view->sys.arch);
            break;
        }
    }

    if (app->config.animations) {
        s->shown = prism_ease(s->shown, target, app_ease_factor(app));
    } else {
        s->shown = target;
    }
    if (s->kind == STAT_LOAD) {
        snprintf(value, sizeof(value), "%.2f", s->shown);
    } else if (s->kind == STAT_TEMP) {
        snprintf(value, sizeof(value), "%.0f", s->shown);
    } else if (s->kind == STAT_PRESSURE) {
        snprintf(value, sizeof(value), "%.1f", s->shown);
    } else if (s->kind == STAT_BATTERY && app->view->battery.present) {
        snprintf(value, sizeof(value), "%.0f", s->shown);
    }

    draw_card_accent(cr, t, rect, 14, accent, s->hover.active ? 0.8 : 0.28);
    draw_icon(cr, icon, (Rect){rect.x + 14, rect.y + 12, 13, 13}, accent, 1.5);
    text_draw(cr, layout, title, rect.x + 33, rect.y + 20, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    double value_size = 24;
    double unit_width = unit[0] != '\0'
        ? text_width(cr, layout, unit, 10.5, PANGO_WEIGHT_MEDIUM) + 4 : 0;
    double value_width = text_draw_fit(cr, layout, value, rect.x + 16,
                                       rect.y + 49, value_size, 10,
                                       PANGO_WEIGHT_BOLD, t->text, TEXT_LEFT,
                                       width - 34 - unit_width);
    if (unit[0] != '\0') {
        text_draw(cr, layout, unit, rect.x + 20 + value_width, rect.y + 52, 10.5,
                  PANGO_WEIGHT_MEDIUM, t->dim, TEXT_LEFT);
    }
    gboolean compact = width < 190;
    text_draw_ellipsis(cr, layout, sub, rect.x + 16, rect.y + 72, 9.5,
                       PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT,
                       compact ? width - 32 : width - 90);

    Rect spark = {width - 70, height - 36, 54, 24};
    if (compact) {
        if (s->kind == STAT_BATTERY && app->view->battery.present) {
            Rect bar = {16, height - 14, width - 32, 5};
            draw_progress(cr, t, bar, s->shown, accent, 2.5);
        }
    } else if (s->kind == STAT_LOAD) {
        draw_sparkline(cr, t, spark, app->history.load1, &app->history, 90,
                       accent, 1);
    } else if (s->kind == STAT_TEMP) {
        draw_sparkline(cr, t, spark, app->history.temp, &app->history, 90,
                       accent, 1);
    } else if (s->kind == STAT_PRESSURE) {
        draw_sparkline(cr, t, spark, app->history.mem_used, &app->history, 90,
                       accent, 1);
    } else if (s->kind == STAT_BATTERY && app->view->battery.present) {
        Rect bar = {16, height - 14, width - 32, 5};
        draw_progress(cr, t, bar, s->shown, accent, 2.5);
    }
    g_object_unref(layout);
}

static void cpu_chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                           gpointer data) {
    (void)area;
    ChartState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    draw_card(cr, t, rect, 16);
    draw_icon(cr, "cpu", (Rect){16, 14, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "CPU UTILIZATION", 38, 22, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "user  \xc2\xb7  system  \xc2\xb7  iowait  \xc2\xb7  irq",
              38, 39, 8.5, PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT);
    char usage[32];
    snprintf(usage, sizeof(usage), "%.1f%%", app->view->cpu.usage_pct);
    text_draw(cr, layout, usage, width - 18, 24, 22, PANGO_WEIGHT_BOLD,
              t->accent, TEXT_RIGHT);
    char freq[32];
    fmt_mhz(app->view->cpu.freq_mhz, freq, sizeof(freq));
    text_draw(cr, layout, freq, width - 18, 44, 9, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_RIGHT);

    ChartSeries series[] = {
        {app->history.cpu_user, t->blue, "User"},
        {app->history.cpu_sys, t->purple, "System"},
        {app->history.cpu_io, t->yellow, "I/O wait"},
        {app->history.cpu_irq, t->cyan, "IRQ"},
    };
    double lx = 18;
    for (int i = 0; i < 4; i++) {
        cairo_new_path(cr);
        cairo_arc(cr, lx + 4, 65, 3.2, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, series[i].color.r, series[i].color.g,
                              series[i].color.b, 1);
        cairo_fill(cr);
        text_draw(cr, layout, series[i].label, lx + 12, 65, 8.7,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        lx += 16 + text_width(cr, layout, series[i].label, 8.7, PANGO_WEIGHT_NORMAL);
    }

    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 78, width - 28, height - 90},
        .series = series,
        .series_count = 4,
        .ymax = 100,
        .format = CHART_PERCENT,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 1,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
    };
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static void chart_card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                            gpointer data) {
    (void)area;
    ChartState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    draw_card(cr, t, rect, 16);
    const char *title = "NETWORK TRAFFIC";
    const char *icon = "network";
    char current[64];
    char rx[32], tx[32];
    fmt_rate(app->view->net_rx_bps, rx, sizeof(rx));
    fmt_rate(app->view->net_tx_bps, tx, sizeof(tx));
    snprintf(current, sizeof(current), "\xe2\x86\x93 %s   \xe2\x86\x91 %s", rx, tx);
    text_draw(cr, layout, current, width - 18, 22, 10.5, PANGO_WEIGHT_BOLD,
              t->text, TEXT_RIGHT);

    ChartSeries series[2];
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 40, width - 28, height - 52},
        .series = series,
        .series_count = 2,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 0,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
    };
    draw_icon(cr, icon, (Rect){16, 14, 15, 15}, t->blue, 1.7);
    text_draw(cr, layout, title, 38, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    series[0] = (ChartSeries){app->history.net_rx, t->blue, "Receive"};
    series[1] = (ChartSeries){app->history.net_tx, t->cyan, "Transmit"};
    spec.format = CHART_RATE;
    double lx = 18;
    for (int i = 0; i < 2; i++) {
        cairo_new_path(cr);
        cairo_arc(cr, lx + 4, 37, 3.2, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, series[i].color.r, series[i].color.g,
                              series[i].color.b, 1);
        cairo_fill(cr);
        text_draw(cr, layout, series[i].label, lx + 12, 37, 8.7,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        lx += 16 + text_width(cr, layout, series[i].label, 8.7, PANGO_WEIGHT_NORMAL);
    }
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static void disk_chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                            gpointer data) {
    (void)area;
    ChartState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    draw_card(cr, t, rect, 16);
    draw_icon(cr, "disk", (Rect){16, 14, 15, 15}, t->orange, 1.7);
    text_draw(cr, layout, "DISK THROUGHPUT", 38, 22, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    double read_rate = 0;
    double write_rate = 0;
    for (int i = 0; i < app->view->disk_count; i++) {
        read_rate += app->view->disks[i].read_bps;
        write_rate += app->view->disks[i].write_bps;
    }
    char rr[32], wr[32], current[128];
    fmt_rate(read_rate, rr, sizeof(rr));
    fmt_rate(write_rate, wr, sizeof(wr));
    snprintf(current, sizeof(current), "R %s   W %s", rr, wr);
    text_draw(cr, layout, current, width - 18, 22, 10.5, PANGO_WEIGHT_BOLD,
              t->text, TEXT_RIGHT);
    ChartSeries series[] = {
        {app->history.disk_r, t->orange, "Read"},
        {app->history.disk_w, t->red, "Write"},
    };
    double lx = 18;
    for (int i = 0; i < 2; i++) {
        cairo_new_path(cr);
        cairo_arc(cr, lx + 4, 37, 3.2, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, series[i].color.r, series[i].color.g,
                              series[i].color.b, 1);
        cairo_fill(cr);
        text_draw(cr, layout, series[i].label, lx + 12, 37, 8.7,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        lx += 16 + text_width(cr, layout, series[i].label, 8.7, PANGO_WEIGHT_NORMAL);
    }
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 40, width - 28, height - 52},
        .series = series,
        .series_count = 2,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 0,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
        .format = CHART_RATE,
    };
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static int compare_cpu(const void *a, const void *b) {
    const ProcInfo *pa = *(const ProcInfo *const *)a;
    const ProcInfo *pb = *(const ProcInfo *const *)b;
    if (pa->cpu_pct < pb->cpu_pct) {
        return 1;
    }
    if (pa->cpu_pct > pb->cpu_pct) {
        return -1;
    }
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

static void top_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                     gpointer data) {
    (void)area;
    TopState *top = data;
    App *app = top->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    draw_card(cr, t, rect, 16);
    draw_icon(cr, "processes", (Rect){16, 14, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "TOP PROCESSES", 38, 22, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "CPU", width - 78, 22, 8.5, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);
    text_draw(cr, layout, "MEMORY", width - 18, 22, 8.5, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);

    const ProcInfo *items[PRISM_MAX_PROCS];
    int count = 0;
    for (int i = 0; i < app->view->proc_count; i++) {
        if (!app->config.kernel_threads && app->view->procs[i].kernel_thread) {
            continue;
        }
        if (app->view->procs[i].cpu_pct <= 0.05 && count > 8) {
            continue;
        }
        items[count++] = &app->view->procs[i];
    }
    qsort(items, count, sizeof(items[0]), compare_cpu);
    int rows = prism_min_int(count, 6);
    top->count = rows;
    for (int i = 0; i < rows; i++) {
        top->pids[i] = items[i]->pid;
    }
    double max_cpu = rows > 0 ? items[0]->cpu_pct : 1;
    if (max_cpu < 1) {
        max_cpu = 1;
    }
    double y = 48;
    for (int i = 0; i < rows; i++) {
        const ProcInfo *p = items[i];
        Color row_color = i == 0 ? t->accent : (i == 1 ? t->cyan : t->dim);
        if (i == 0) {
            Rect hl = {10, y - 2, width - 20, 36};
            rounded_rect(cr, hl, 8);
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 0.07);
            cairo_fill(cr);
        }
        char rank[8];
        snprintf(rank, sizeof(rank), "%d", i + 1);
        text_draw(cr, layout, rank, 22, y + 16, 9.5, PANGO_WEIGHT_BOLD,
                  t->faint, TEXT_CENTER);
        text_draw_ellipsis(cr, layout, p->name, 38, y + 12, 10,
                           PANGO_WEIGHT_MEDIUM, t->text, TEXT_LEFT, width - 170);
        char mem[32];
        fmt_bytes((double)p->rss_kb * 1024.0, mem, sizeof(mem));
        text_draw(cr, layout, mem, width - 18, y + 12, 8.8, PANGO_WEIGHT_NORMAL,
                  t->dim, TEXT_RIGHT);
        char cpu[32];
        snprintf(cpu, sizeof(cpu), "%.1f%%", p->cpu_pct);
        text_draw(cr, layout, cpu, width - 78, y + 12, 9.5, PANGO_WEIGHT_BOLD,
                  row_color, TEXT_RIGHT);
        double bar_w = (width - 150) * prism_clamp(p->cpu_pct / max_cpu, 0, 1);
        Rect bar = {38, y + 26, width - 140, 2.5};
        rounded_rect(cr, bar, 1.5);
        cairo_set_source_rgba(cr, t->track.r, t->track.g, t->track.b, 0.8);
        cairo_fill(cr);
        if (bar_w > 2) {
            bar.w = bar_w;
            rounded_rect(cr, bar, 1.5);
            cairo_set_source_rgba(cr, row_color.r, row_color.g, row_color.b, 0.85);
            cairo_fill(cr);
        }
        y += 36;
    }
    if (rows == 0) {
        text_draw(cr, layout, "Collecting process samples...", width / 2, height / 2,
                  11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static void plugins_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    PluginsState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "system", (Rect){16, 14, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "PLUGINS", 38, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    if (app->view->plugin_count == 0) {
        text_draw(cr, layout,
                  "No plugins — place executables in ~/.config/prism-monitor/plugins/ "
                  "(output: name=value unit, e.g. weather=21 C)",
                  18, height / 2, 9.5, PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT);
    } else {
        double y = 48;
        int rows = app->view->plugin_count < 6 ? app->view->plugin_count : 6;
        for (int i = 0; i < rows; i++) {
            const PluginInfo *p = &app->view->plugins[i];
            text_draw_ellipsis(cr, layout, p->name, 18, y, 9.5, PANGO_WEIGHT_BOLD,
                               t->text, TEXT_LEFT, width / 2);
            char val[96];
            snprintf(val, sizeof(val), "%s %s", p->value, p->unit);
            text_draw(cr, layout, val, width - 18, y, 10, PANGO_WEIGHT_BOLD,
                      t->accent, TEXT_RIGHT);
            y += 22;
        }
    }
    g_object_unref(layout);
}

static GtkWidget *make_gauge(App *app, OverviewState *state, GaugeKind kind,
                             int height) {
    GaugeState *g = &state->gauges[kind];
    g->app = app;
    g->kind = kind;
    GtkWidget *area = prism_card_area(app, height, gauge_draw, g);
    app_track_anim(app, area);
    prism_hover_attach(area, &g->hover);
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(navigate_gauge), g);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));
    return area;
}

static GtkWidget *make_stat(App *app, OverviewState *state, StatKind kind,
                            int height) {
    StatState *s = &state->stats[kind];
    s->app = app;
    s->kind = kind;
    GtkWidget *area = prism_card_area(app, height, stat_draw, s);
    app_track_anim(app, area);
    prism_hover_attach(area, &s->hover);
    return area;
}

GtkWidget *page_overview_create(App *app) {
    OverviewState *state = g_new0(OverviewState, 1);
    state->app = app;

    GtkWidget *content = prism_column();

    GtkWidget *gauges = prism_grid();
    for (int i = 0; i < 6; i++) {
        prism_grid_put(GTK_GRID(gauges), make_gauge(app, state, (GaugeKind)i, 174),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), gauges);

    GtkWidget *stats = prism_grid();
    for (int i = 0; i < 6; i++) {
        prism_grid_put(GTK_GRID(stats), make_stat(app, state, (StatKind)i, 100),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), stats);

    GtkWidget *charts = prism_grid();
    state->cpu_chart.app = app;
    GtkWidget *cpu_area = prism_area(app, 300, cpu_chart_draw, &state->cpu_chart);
    prism_hover_attach(cpu_area, &state->cpu_chart.hover);
    GtkGesture *cpu_click = gtk_gesture_click_new();
    g_signal_connect(cpu_click, "pressed", G_CALLBACK(navigate_click), &state->cpu_chart);
    gtk_widget_add_controller(cpu_area, GTK_EVENT_CONTROLLER(cpu_click));
    prism_grid_put(GTK_GRID(charts), cpu_area, 0, 0, 3, 1);

    state->top.app = app;
    GtkWidget *top_area = prism_area(app, 300, top_draw, &state->top);
    prism_hover_attach(top_area, &state->top.hover);
    GtkGesture *top_click_gesture = gtk_gesture_click_new();
    g_signal_connect(top_click_gesture, "pressed", G_CALLBACK(top_click), &state->top);
    gtk_widget_add_controller(top_area, GTK_EVENT_CONTROLLER(top_click_gesture));
    prism_grid_put(GTK_GRID(charts), top_area, 3, 0, 2, 1);
    gtk_box_append(GTK_BOX(content), charts);

    GtkWidget *io = prism_grid();
    state->net_chart.app = app;
    state->disk_chart.app = app;
    GtkWidget *net_area = prism_area(app, 240, chart_card_draw, &state->net_chart);
    prism_hover_attach(net_area, &state->net_chart.hover);
    GtkWidget *disk_area = prism_area(app, 240, disk_chart_draw, &state->disk_chart);
    prism_hover_attach(disk_area, &state->disk_chart.hover);
    prism_grid_put(GTK_GRID(io), net_area, 0, 0, 1, 1);
    prism_grid_put(GTK_GRID(io), disk_area, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(content), io);

    state->plugins.app = app;
    GtkWidget *plug_area = prism_area(app, 150, plugins_draw, &state->plugins);
    prism_hover_attach(plug_area, &state->plugins.hover);
    gtk_box_append(GTK_BOX(content), plug_area);

    g_object_set_data_full(G_OBJECT(content), "overview-state", state, g_free);
    app->pages[PAGE_OVERVIEW].root = content;
    return prism_page(content);
}

void page_overview_update(App *app) {
    (void)app;
}
