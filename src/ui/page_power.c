#include "core/common.h"
#include "core/format.h"
#include "ui/app.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    POWER_CARD_PACKAGE,
    POWER_CARD_FREQ,
    POWER_CARD_TEMP,
    POWER_CARD_BATTERY
} PowerCardKind;

typedef struct {
    App *app;
    PowerCardKind kind;
    HoverInfo hover;
    double shown;
} PowerCard;

typedef struct {
    App *app;
    HoverInfo hover;
} PowerChart;

typedef struct {
    App *app;
    HoverInfo hover;
} PowerCores;

typedef struct {
    App *app;
    PowerCard cards[4];
    PowerChart freq_chart;
    PowerChart power_chart;
    PowerCores cores;
    char governor[32];
    char driver[32];
    char profile[48];
    char profile_choices[160];
    int profile_ready;
    int samples;
} PowerPage;

static Color power_temp_color(const Theme *t, double value) {
    if (value >= 90) {
        return t->red;
    }
    if (value >= 75) {
        return t->orange;
    }
    if (value >= 60) {
        return t->yellow;
    }
    return t->green;
}

static double core_frequency(App *app, int index) {
    if (index < app->view->cpu.core_freq_count &&
        app->view->cpu.core_freq_mhz[index] > 0) {
        return app->view->cpu.core_freq_mhz[index];
    }
    return app->view->cpu.freq_mhz;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    PowerCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->yellow;
    const char *icon = "power";
    const char *title = "";
    char value[48] = "--";
    char unit[16] = "";
    char sub[192] = "";
    double target = 0;

    switch (card->kind) {
        case POWER_CARD_PACKAGE: {
            const CpuInfo *cpu = &app->view->cpu;
            accent = t->yellow;
            icon = "power";
            title = "PACKAGE POWER";
            target = cpu->package_power_w;
            if (target > 0) {
                snprintf(value, sizeof(value), "%.1f", card->shown);
                snprintf(unit, sizeof(unit), "W");
                snprintf(sub, sizeof(sub), "RAPL package domain");
            } else {
                snprintf(sub, sizeof(sub), "no RAPL domain exposed");
            }
            break;
        }
        case POWER_CARD_FREQ: {
            accent = t->cyan;
            icon = "chip";
            title = "AVERAGE CLOCK";
            target = app->view->cpu.freq_mhz;
            if (target > 0) {
                snprintf(value, sizeof(value), "%.0f", card->shown);
                snprintf(unit, sizeof(unit), "MHz");
                int cores = app->view->cpu.core_freq_count > 0
                    ? app->view->cpu.core_freq_count : app->view->cpu.core_count;
                snprintf(sub, sizeof(sub), "%d cores reporting scaling clocks", cores);
            } else {
                snprintf(sub, sizeof(sub), "scaling_cur_freq unavailable");
            }
            break;
        }
        case POWER_CARD_TEMP: {
            accent = power_temp_color(t, app->view->cpu.temp_c);
            icon = "sensors";
            title = "PROCESSOR TEMP";
            target = app->view->cpu.temp_c;
            if (target > 0) {
                app_format_temp(app, card->shown, value, sizeof(value));
                snprintf(sub, sizeof(sub), "%s governor  \xc2\xb7  %s driver",
                         app->view->cpu.governor[0] ? app->view->cpu.governor : "unknown",
                         app->view->cpu.driver[0] ? app->view->cpu.driver : "acpi");
            } else {
                snprintf(sub, sizeof(sub), "no CPU temperature sensor");
            }
            break;
        }
        case POWER_CARD_BATTERY: {
            const BatteryInfo *battery = &app->view->battery;
            accent = battery->capacity_pct <= 20 ? t->red :
                     (battery->capacity_pct <= 45 ? t->yellow : t->green);
            icon = "battery";
            title = "BATTERY";
            target = battery->present ? battery->capacity_pct : 0;
            if (battery->present) {
                snprintf(value, sizeof(value), "%.0f", card->shown);
                snprintf(unit, sizeof(unit), "%%");
                snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %.1f W  \xc2\xb7  %.1f V",
                         battery->status, battery->power_w, battery->voltage_v);
            } else {
                snprintf(sub, sizeof(sub), "no battery detected");
            }
            break;
        }
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
    }
    if (card->kind == POWER_CARD_PACKAGE && target > 0) {
        snprintf(value, sizeof(value), "%.1f", card->shown);
    } else if (card->kind == POWER_CARD_FREQ && target > 0) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    } else if (card->kind == POWER_CARD_TEMP && target > 0) {
        app_format_temp(app, card->shown, value, sizeof(value));
    } else if (card->kind == POWER_CARD_BATTERY && app->view->battery.present) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    }

    draw_card_accent(cr, t, rect, 14, accent, card->hover.active ? 0.9 : 0.3);
    draw_icon(cr, icon, (Rect){16, 14, 14, 14}, accent, 1.6);
    text_draw(cr, layout, title, 37, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    double value_size = strlen(value) > 7 ? 18 : 25;
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

static void profile_read(PowerPage *page) {
    char *value = prism_read_first_line("/sys/firmware/acpi/platform_profile");
    if (value != NULL) {
        g_strlcpy(page->profile, value, sizeof(page->profile));
        g_free(value);
    }
    value = prism_read_first_line("/sys/firmware/acpi/platform_profile_choices");
    if (value != NULL) {
        g_strlcpy(page->profile_choices, value, sizeof(page->profile_choices));
        g_free(value);
    }
    page->profile_ready = 1;
}

static void freq_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    PowerChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "chip", (Rect){17, 15, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "CORE FREQUENCY", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    char head[64];
    snprintf(head, sizeof(head), "%.0f MHz", app->view->cpu.freq_mhz);
    text_draw(cr, layout, head, width - 18, 25, 20, PANGO_WEIGHT_BOLD, t->cyan,
              TEXT_RIGHT);
    text_draw(cr, layout, "scaling clocks, sampled live", width - 18, 45, 8.7,
              PANGO_WEIGHT_NORMAL, t->faint, TEXT_RIGHT);

    double peak = 0;
    int count = app->history.count;
    for (int i = 0; i < count && i < PRISM_HIST; i++) {
        if (app->history.freq[i] > peak) {
            peak = app->history.freq[i];
        }
    }
    double ymax = peak > 0 ? peak * 1.15 : 4000;
    if (ymax < 1000) {
        ymax = 1000;
    }

    ChartSeries series[] = {
        {app->history.freq, t->cyan, "Average clock"}
    };
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 60, width - 28, height - 74},
        .series = series,
        .series_count = 1,
        .ymax = ymax,
        .format = CHART_NUMBER,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 0,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
    };
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static void power_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    PowerChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "power", (Rect){17, 15, 15, 15}, t->yellow, 1.7);
    text_draw(cr, layout, "PACKAGE POWER", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    char head[64];
    if (app->view->cpu.package_power_w > 0) {
        snprintf(head, sizeof(head), "%.1f W", app->view->cpu.package_power_w);
    } else {
        snprintf(head, sizeof(head), "--");
    }
    text_draw(cr, layout, head, width - 18, 25, 20, PANGO_WEIGHT_BOLD, t->yellow,
              TEXT_RIGHT);
    text_draw(cr, layout, "RAPL energy delta per second", width - 18, 45, 8.7,
              PANGO_WEIGHT_NORMAL, t->faint, TEXT_RIGHT);

    double peak = 0;
    int count = app->history.count;
    for (int i = 0; i < count && i < PRISM_HIST; i++) {
        if (app->history.power[i] > peak) {
            peak = app->history.power[i];
        }
    }
    double ymax = peak > 0 ? peak * 1.25 : 25;
    if (ymax < 5) {
        ymax = 5;
    }

    ChartSeries series[] = {
        {app->history.power, t->yellow, "Package power"}
    };
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 60, width - 28, height - 74},
        .series = series,
        .series_count = 1,
        .ymax = ymax,
        .format = CHART_NUMBER,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 0,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
    };
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static void cores_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    PowerCores *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "gauge", (Rect){17, 15, 15, 15}, t->purple, 1.7);
    text_draw(cr, layout, "PER-CORE CLOCKS", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);

    int cores = app->view->cpu.core_count;
    if (cores <= 0) {
        cores = 1;
    }
    if (cores > PRISM_MAX_CORES) {
        cores = PRISM_MAX_CORES;
    }
    double peak = 0;
    for (int i = 0; i < cores; i++) {
        double mhz = core_frequency(app, i);
        if (mhz > peak) {
            peak = mhz;
        }
    }
    if (peak <= 0) {
        peak = 1;
    }
    int columns = cores <= 4 ? 2 : (cores <= 8 ? 4 : 6);
    int rows = (cores + columns - 1) / columns;
    double cell_w = (width - 36 - (columns - 1) * 12) / columns;
    double cell_h = 54;
    double y = 48;
    for (int i = 0; i < cores; i++) {
        int col = i % columns;
        int row = i / columns;
        double x = 18 + col * (cell_w + 12);
        double cy = y + row * cell_h;
        if (cy + cell_h > height - 34) {
            break;
        }
        double mhz = core_frequency(app, i);
        char label[24];
        snprintf(label, sizeof(label), "CPU %d", i);
        text_draw(cr, layout, label, x, cy + 9, 8.6, PANGO_WEIGHT_BOLD,
                  t->faint, TEXT_LEFT);
        char value[32];
        snprintf(value, sizeof(value), "%.0f MHz", mhz);
        text_draw(cr, layout, value, x + cell_w, cy + 9, 9.0, PANGO_WEIGHT_BOLD,
                  t->text, TEXT_RIGHT);
        draw_progress(cr, t, (Rect){x, cy + 18, cell_w, 5},
                      prism_clamp(mhz / peak * 100.0, 0, 100), t->cyan, 2.5);
        draw_progress(cr, t, (Rect){x, cy + 28, cell_w, 3},
                      prism_clamp(app->view->cpu.per_core[i], 0, 100),
                      app->view->cpu.per_core[i] > 80 ? t->red : t->green, 1.5);
    }

    char summary[320];
    snprintf(summary, sizeof(summary),
             "%s  \xc2\xb7  governor %s  \xc2\xb7  driver %s",
             app->view->cpu.model[0] ? app->view->cpu.model : "Processor",
             app->view->cpu.governor[0] ? app->view->cpu.governor : "unknown",
             app->view->cpu.driver[0] ? app->view->cpu.driver : "unknown");
    text_draw_ellipsis(cr, layout, summary, 18, height - 18, 8.6,
                       PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT, width - 36);
    g_object_unref(layout);
}

static void platform_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                          gpointer data) {
    (void)area;
    PowerPage *page = data;
    App *app = page->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "settings", (Rect){17, 15, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "POWER PROFILE", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, page->profile[0] ? page->profile : "not exposed",
              width - 18, 27, 13, PANGO_WEIGHT_BOLD, t->text, TEXT_RIGHT);
    text_draw_ellipsis(cr, layout,
                       page->profile_choices[0] ? page->profile_choices
                                                : "no platform profile interface",
                       18, 50, 8.6, PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT,
                       width - 36);
    draw_separator(cr, t, 18, 62, width - 18, 62);

    const char *keys[] = {"Governor", "Scaling driver", "Threads", "Load average"};
    char threads[32];
    snprintf(threads, sizeof(threads), "%d logical", app->view->cpu.core_count);
    char load[64];
    snprintf(load, sizeof(load), "%.2f  \xc2\xb7  %.2f  \xc2\xb7  %.2f",
             app->view->cpu.load1, app->view->cpu.load5, app->view->cpu.load15);
    const char *values[] = {
        app->view->cpu.governor[0] ? app->view->cpu.governor : "unknown",
        app->view->cpu.driver[0] ? app->view->cpu.driver : "unknown",
        threads,
        load
    };
    double y = 80;
    for (int i = 0; i < 4; i++) {
        text_draw(cr, layout, keys[i], 18, y, 8.7, PANGO_WEIGHT_NORMAL, t->faint,
                  TEXT_LEFT);
        text_draw_ellipsis(cr, layout, values[i], width - 18, y, 9.2,
                           PANGO_WEIGHT_BOLD, t->text, TEXT_RIGHT, width - 140);
        y += 26;
    }

    const BatteryInfo *battery = &app->view->battery;
    draw_separator(cr, t, 18, y + 2, width - 18, y + 2);
    y += 20;
    text_draw(cr, layout, "BATTERY HEALTH", 18, y, 8.7, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_LEFT);
    y += 18;
    if (battery->present) {
        char health[64];
        if (battery->health_pct > 0) {
            snprintf(health, sizeof(health), "%.0f%% health  \xc2\xb7  %d cycles",
                     battery->health_pct, battery->cycles);
        } else {
            snprintf(health, sizeof(health), "health unavailable  \xc2\xb7  %d cycles",
                     battery->cycles);
        }
        text_draw_ellipsis(cr, layout, health, 18, y, 9.4, PANGO_WEIGHT_NORMAL,
                           t->dim, TEXT_LEFT, width - 36);
        y += 18;
        char energy[96];
        snprintf(energy, sizeof(energy), "%.1f / %.1f Wh  \xc2\xb7  %s",
                 battery->energy_now_wh, battery->energy_full_wh, battery->model);
        text_draw_ellipsis(cr, layout, energy, 18, y, 8.8, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, width - 36);
    } else {
        text_draw(cr, layout, "This system reports AC power only.", 18, y, 9.4,
                  PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT);
    }
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, PowerPage *page, PowerCardKind kind, int height) {
    PowerCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_power_create(App *app) {
    PowerPage *page = g_new0(PowerPage, 1);
    page->app = app;
    page->freq_chart.app = app;
    page->power_chart.app = app;
    page->cores.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (PowerCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *freq = prism_area(app, 290, freq_draw, &page->freq_chart);
    prism_hover_attach(freq, &page->freq_chart.hover);
    gtk_widget_set_hexpand(freq, TRUE);
    gtk_widget_set_size_request(freq, 480, 290);
    gtk_box_append(GTK_BOX(row), freq);
    GtkWidget *power = prism_area(app, 290, power_draw, &page->power_chart);
    prism_hover_attach(power, &page->power_chart.hover);
    gtk_widget_set_hexpand(power, TRUE);
    gtk_widget_set_size_request(power, 480, 290);
    gtk_box_append(GTK_BOX(row), power);
    gtk_box_append(GTK_BOX(content), row);

    int cores = app->view->cpu.core_count > 0 ? app->view->cpu.core_count : 4;
    if (cores > PRISM_MAX_CORES) {
        cores = PRISM_MAX_CORES;
    }
    int columns = cores <= 4 ? 2 : (cores <= 8 ? 4 : 6);
    int rows = (cores + columns - 1) / columns;
    int core_height = 58 + rows * 54;
    if (core_height < 230) {
        core_height = 230;
    }
    GtkWidget *bottom = prism_row();
    GtkWidget *core_area = prism_area(app, core_height, cores_draw, &page->cores);
    prism_hover_attach(core_area, &page->cores.hover);
    gtk_widget_set_hexpand(core_area, TRUE);
    gtk_widget_set_size_request(core_area, 620, core_height);
    gtk_box_append(GTK_BOX(bottom), core_area);
    GtkWidget *platform = prism_area(app, core_height, platform_draw, page);
    gtk_widget_set_size_request(platform, 380, core_height);
    gtk_widget_set_hexpand(platform, FALSE);
    gtk_box_append(GTK_BOX(bottom), platform);
    gtk_box_append(GTK_BOX(content), bottom);

    g_object_set_data_full(G_OBJECT(content), "power-page", page, g_free);
    app->pages[PAGE_POWER].root = content;
    return prism_page(content);
}

void page_power_update(App *app) {
    PowerPage *page = g_object_get_data(G_OBJECT(app->pages[PAGE_POWER].root),
                                        "power-page");
    if (page == NULL || app->current_page != PAGE_POWER) {
        return;
    }
    page->samples++;
    if (!page->profile_ready || (page->samples % 30) == 0) {
        profile_read(page);
    }
}
