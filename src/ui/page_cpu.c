#include "core/common.h"
#include "core/format.h"
#include "ui/app.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    CPU_CARD_USAGE,
    CPU_CARD_FREQ,
    CPU_CARD_TEMP,
    CPU_CARD_LOAD
} CpuCardKind;

typedef struct {
    App *app;
    CpuCardKind kind;
    HoverInfo hover;
    double shown;
} CpuCard;

typedef struct {
    App *app;
    HoverInfo hover;
    int hover_core;
} CoreState;

typedef struct {
    App *app;
    HoverInfo hover;
} ChartState;

typedef struct {
    App *app;
    HoverInfo hover;
} LoadState;

typedef struct {
    App *app;
    CpuCard cards[4];
    CoreState cores;
    ChartState chart;
    LoadState load;
} CpuPage;

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    CpuCard *card = data;
    App *app = card->app;
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

    switch (card->kind) {
        case CPU_CARD_USAGE:
            accent = t->accent;
            icon = "cpu";
            title = "UTILIZATION";
            target = app->view->cpu.usage_pct;
            snprintf(value, sizeof(value), "%.1f", card->shown);
            snprintf(unit, sizeof(unit), "%%");
            snprintf(sub, sizeof(sub), "user %.1f%%  \xc2\xb7  system %.1f%%",
                     app->view->cpu.user_pct, app->view->cpu.system_pct);
            break;
        case CPU_CARD_FREQ:
            accent = t->cyan;
            icon = "cpu";
            title = "CLOCK SPEED";
            target = app->view->cpu.freq_mhz;
            fmt_mhz(app->view->cpu.freq_mhz, value, sizeof(value));
            snprintf(sub, sizeof(sub), "%s governor", app->view->cpu.governor[0] ? app->view->cpu.governor : "unknown");
            break;
        case CPU_CARD_TEMP:
            accent = app->view->cpu.temp_c >= 80 ? t->red :
                     (app->view->cpu.temp_c >= 65 ? t->yellow : t->green);
            icon = "sensors";
            title = "PACKAGE TEMP";
            target = app->view->cpu.temp_c;
            snprintf(value, sizeof(value), "%.0f", card->shown);
            snprintf(unit, sizeof(unit), "\xc2\xb0" "C");
            snprintf(sub, sizeof(sub), "%s driver", app->view->cpu.driver[0] ? app->view->cpu.driver : "platform");
            break;
        case CPU_CARD_LOAD:
            accent = t->yellow;
            icon = "system";
            title = "LOAD AVERAGE";
            target = app->view->cpu.load1;
            snprintf(value, sizeof(value), "%.2f", card->shown);
            snprintf(sub, sizeof(sub), "5m %.2f  \xc2\xb7  15m %.2f",
                     app->view->cpu.load5, app->view->cpu.load15);
            break;
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
    }
    if (card->kind == CPU_CARD_USAGE) {
        snprintf(value, sizeof(value), "%.1f", card->shown);
    } else if (card->kind == CPU_CARD_TEMP) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    } else if (card->kind == CPU_CARD_LOAD) {
        snprintf(value, sizeof(value), "%.2f", card->shown);
    }

    draw_card_accent(cr, t, rect, 14, accent, card->hover.active ? 0.9 : 0.3);
    draw_icon(cr, icon, (Rect){16, 14, 14, 14}, accent, 1.6);
    text_draw(cr, layout, title, 37, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    double value_size = 25;
    double unit_width = unit[0] != '\0'
        ? text_width(cr, layout, unit, 10.5, PANGO_WEIGHT_MEDIUM) + 4 : 0;
    double value_width = text_draw_fit(cr, layout, value, 17, 52, value_size,
                                       10, PANGO_WEIGHT_BOLD, t->text, TEXT_LEFT,
                                       width - 34 - unit_width);
    if (unit[0] != '\0') {
        text_draw(cr, layout, unit, 21 + value_width, 55, 10.5, PANGO_WEIGHT_MEDIUM,
                  t->dim, TEXT_LEFT);
    }
    text_draw_ellipsis(cr, layout, sub, 17, 76, 9.5, PANGO_WEIGHT_NORMAL, t->faint,
                       TEXT_LEFT, width - 30);
    g_object_unref(layout);
}

static void cores_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    CoreState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "cpu", (Rect){17, 15, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "PER-CORE UTILIZATION", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    char summary[96];
    snprintf(summary, sizeof(summary), "%d logical cores  \xc2\xb7  %s",
             app->view->cpu.core_count,
             app->view->cpu.model[0] ? app->view->cpu.model : "processor");
    text_draw_ellipsis(cr, layout, summary, width - 18, 23, 9, PANGO_WEIGHT_NORMAL,
                       t->faint, TEXT_RIGHT, width * 0.55);

    int cores = app->view->cpu.core_count;
    if (cores <= 0) {
        text_draw(cr, layout, "Waiting for CPU sample...", width / 2.0, height / 2.0,
                  11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
        g_object_unref(layout);
        return;
    }
    if (cores > PRISM_MAX_CORES) {
        cores = PRISM_MAX_CORES;
    }
    int columns = cores <= 4 ? 2 : 4;
    int rows = (cores + columns - 1) / columns;
    double top = 48;
    double cell_w = (width - 34 - (columns - 1) * 14) / columns;
    double cell_h = (height - top - 18 - (rows - 1) * 10) / rows;
    for (int i = 0; i < cores; i++) {
        int col = i % columns;
        int row = i / columns;
        double x = 17 + col * (cell_w + 14);
        double y = top + row * (cell_h + 10);
        double pct = app->view->cpu.per_core[i];
        Color color = pct >= 85 ? t->red : (pct >= 60 ? t->yellow : t->accent);
        if (state->hover.active && state->hover.y >= y && state->hover.y <= y + cell_h) {
            Rect hover = {x - 4, y - 3, cell_w + 8, cell_h + 6};
            rounded_rect(cr, hover, 8);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.035);
            cairo_fill(cr);
        }
        char label[32];
        snprintf(label, sizeof(label), "CORE %d", i);
        text_draw(cr, layout, label, x, y + 10, 9, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
        char pct_text[32];
        snprintf(pct_text, sizeof(pct_text), "%.0f%%", pct);
        text_draw(cr, layout, pct_text, x + cell_w, y + 10, 10, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        draw_progress(cr, t, (Rect){x, y + 22, cell_w, 6}, pct, color, 3);
        char freq[32];
        fmt_mhz(app->view->cpu.freq_mhz, freq, sizeof(freq));
        text_draw(cr, layout, freq, x, y + 41, 8.2, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_LEFT);
        text_draw(cr, layout, pct >= 85 ? "high" : (pct >= 60 ? "busy" : "nominal"),
                  x + cell_w, y + 41, 8.2, PANGO_WEIGHT_NORMAL, t->faint, TEXT_RIGHT);
    }
    g_object_unref(layout);
}

static void chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    ChartState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "cpu", (Rect){17, 15, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "CPU HISTORY", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    text_draw(cr, layout, "normalized to 100%", 39, 40, 8.5, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_LEFT);
    char usage[32];
    snprintf(usage, sizeof(usage), "%.1f%%", app->view->cpu.usage_pct);
    text_draw(cr, layout, usage, width - 18, 25, 24, PANGO_WEIGHT_BOLD, t->accent,
              TEXT_RIGHT);
    char util[64];
    snprintf(util, sizeof(util), "idle %.0f%%  \xc2\xb7  iowait %.1f%%",
             app->view->cpu.idle_pct, app->view->cpu.iowait_pct);
    text_draw(cr, layout, util, width - 18, 47, 8.7, PANGO_WEIGHT_NORMAL,
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
        cairo_arc(cr, lx + 4, 63, 3.2, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, series[i].color.r, series[i].color.g,
                              series[i].color.b, 1);
        cairo_fill(cr);
        text_draw(cr, layout, series[i].label, lx + 12, 63, 8.7,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        lx += 16 + text_width(cr, layout, series[i].label, 8.7, PANGO_WEIGHT_NORMAL);
    }
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 76, width - 28, height - 88},
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

static void load_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    LoadState *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "activity", (Rect){17, 15, 15, 15}, t->yellow, 1.7);
    text_draw(cr, layout, "LOAD & PRESSURE", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);

    const char *labels[] = {"1 minute", "5 minutes", "15 minutes"};
    double loads[] = {app->view->cpu.load1, app->view->cpu.load5, app->view->cpu.load15};
    double y = 48;
    double max_load = app->view->cpu.core_count > 0 ? app->view->cpu.core_count : 4;
    for (int i = 0; i < 3; i++) {
        char value[32];
        snprintf(value, sizeof(value), "%.2f", loads[i]);
        text_draw(cr, layout, labels[i], 18, y + 8, 9.5, PANGO_WEIGHT_NORMAL,
                  t->dim, TEXT_LEFT);
        text_draw(cr, layout, value, width - 18, y + 8, 12, PANGO_WEIGHT_BOLD,
                  t->text, TEXT_RIGHT);
        Color color = loads[i] > max_load ? t->red : (loads[i] > max_load * 0.7 ? t->yellow : t->green);
        draw_progress(cr, t, (Rect){18, y + 20, width - 36, 5},
                      loads[i] / max_load * 100.0, color, 2.5);
        y += 38;
    }

    draw_separator(cr, t, 18, y + 2, width - 18, y + 2);
    y += 20;
    text_draw(cr, layout, "MEMORY PRESSURE", 18, y, 8.7, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_LEFT);
    y += 18;
    char psi[64];
    snprintf(psi, sizeof(psi), "some 10s %.1f%%", app->view->mem.psi_some10);
    text_draw(cr, layout, psi, 18, y, 9.5, PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
    draw_progress(cr, t, (Rect){18, y + 10, (width - 46) * 0.48, 5},
                  app->view->mem.psi_some10, t->purple, 2.5);
    snprintf(psi, sizeof(psi), "full 10s %.1f%%", app->view->mem.psi_full10);
    text_draw(cr, layout, psi, width / 2.0 + 6, y, 9.5, PANGO_WEIGHT_NORMAL,
              t->dim, TEXT_LEFT);
    draw_progress(cr, t, (Rect){width / 2.0 + 6, y + 10, (width - 46) * 0.48, 5},
                  app->view->mem.psi_full10, t->red, 2.5);

    char ctxt[64];
    fmt_count(app->view->cpu.ctxt, ctxt, sizeof(ctxt));
    char schedule[128];
    snprintf(schedule, sizeof(schedule),
             "%llu runnable  \xc2\xb7  %llu blocked  \xc2\xb7  %s ctx/s",
             app->view->cpu.procs_running, app->view->cpu.procs_blocked, ctxt);
    text_draw_ellipsis(cr, layout, schedule, 18, height - 18, 8.6,
                       PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT, width - 36);
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, CpuPage *page, CpuCardKind kind, int height) {
    CpuCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_cpu_create(App *app) {
    CpuPage *page = g_new0(CpuPage, 1);
    page->app = app;
    page->cores.app = app;
    page->chart.app = app;
    page->load.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (CpuCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    int cores = app->view->cpu.core_count > 0 ? app->view->cpu.core_count : 4;
    if (cores > PRISM_MAX_CORES) {
        cores = PRISM_MAX_CORES;
    }
    int columns = cores <= 4 ? 2 : 4;
    int rows = (cores + columns - 1) / columns;
    int core_height = 48 + rows * 82;
    if (core_height < 220) {
        core_height = 220;
    }
    GtkWidget *core_area = prism_area(app, core_height, cores_draw, &page->cores);
    prism_hover_attach(core_area, &page->cores.hover);
    gtk_box_append(GTK_BOX(content), core_area);

    GtkWidget *row = prism_row();
    GtkWidget *chart = prism_area(app, 300, chart_draw, &page->chart);
    prism_hover_attach(chart, &page->chart.hover);
    gtk_widget_set_hexpand(chart, TRUE);
    gtk_widget_set_size_request(chart, 520, 300);
    gtk_box_append(GTK_BOX(row), chart);
    GtkWidget *load = prism_area(app, 300, load_draw, &page->load);
    gtk_widget_set_size_request(load, 340, 300);
    gtk_widget_set_hexpand(load, FALSE);
    gtk_box_append(GTK_BOX(row), load);
    gtk_box_append(GTK_BOX(content), row);

    GtkWidget *footer = prism_area(app, 92, load_draw, &page->load);
    gtk_widget_set_visible(footer, FALSE);
    (void)footer;

    g_object_set_data_full(G_OBJECT(content), "cpu-page", page, g_free);
    app->pages[PAGE_CPU].root = content;
    return prism_page(content);
}

void page_cpu_update(App *app) {
    (void)app;
}
