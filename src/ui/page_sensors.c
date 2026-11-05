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
    SENSE_CARD_TEMP,
    SENSE_CARD_FANS,
    SENSE_CARD_POWER,
    SENSE_CARD_BATTERY
} SensorCardKind;

typedef struct {
    App *app;
    SensorCardKind kind;
    HoverInfo hover;
    double shown;
} SensorCard;

typedef struct {
    App *app;
    HoverInfo hover;
} SensorList;

typedef struct {
    App *app;
    HoverInfo hover;
} BatteryCard;

typedef struct {
    App *app;
    SensorCard cards[4];
    SensorList list;
    BatteryCard battery;
} SensorsPage;

static double hottest_sensor(App *app, const SensorInfo **out) {
    double best = -1;
    *out = NULL;
    for (int i = 0; i < app->view->sensor_count; i++) {
        const SensorInfo *sensor = &app->view->sensors[i];
        if (sensor->kind == SENSOR_TEMP && sensor->value > best) {
            best = sensor->value;
            *out = sensor;
        }
    }
    return best;
}

static Color temp_color(const Theme *t, double value) {
    if (value >= 85) {
        return t->red;
    }
    if (value >= 70) {
        return t->orange;
    }
    if (value >= 55) {
        return t->yellow;
    }
    return t->green;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    SensorCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->orange;
    const char *icon = "sensors";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[160] = "";
    double target = 0;

    switch (card->kind) {
        case SENSE_CARD_TEMP: {
            const SensorInfo *hot = NULL;
            double temp = hottest_sensor(app, &hot);
            accent = temp_color(t, temp);
            title = "HOTTEST SENSOR";
            target = temp > 0 ? temp : 0;
            if (hot != NULL) {
                snprintf(value, sizeof(value), "%.1f", card->shown);
                snprintf(unit, sizeof(unit), "\xc2\xb0" "C");
                snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s", hot->label, hot->chip);
            } else {
                snprintf(sub, sizeof(sub), "no temperature sensors");
            }
            break;
        }
        case SENSE_CARD_FANS: {
            accent = t->cyan;
            icon = "fan";
            title = "COOLING";
            int fans = 0;
            double fastest = 0;
            for (int i = 0; i < app->view->sensor_count; i++) {
                if (app->view->sensors[i].kind == SENSOR_FAN) {
                    fans++;
                    if (app->view->sensors[i].value > fastest) {
                        fastest = app->view->sensors[i].value;
                    }
                }
            }
            target = fans;
            snprintf(value, sizeof(value), "%d", fans);
            if (fans > 0) {
                snprintf(sub, sizeof(sub), "peak %.0f RPM  \xc2\xb7  %d sensors",
                         fastest, app->view->sensor_count);
            } else {
                snprintf(sub, sizeof(sub), "%d thermal sensors", app->view->sensor_count);
            }
            break;
        }
        case SENSE_CARD_POWER: {
            accent = t->yellow;
            icon = "activity";
            title = "POWER DRAW";
            double power = 0;
            for (int i = 0; i < app->view->sensor_count; i++) {
                if (app->view->sensors[i].kind == SENSOR_POWER &&
                    strcmp(app->view->sensors[i].unit, "W") == 0) {
                    power += app->view->sensors[i].value;
                }
            }
            target = power;
            if (power > 0) {
                snprintf(value, sizeof(value), "%.0f", card->shown);
                snprintf(unit, sizeof(unit), "W");
                snprintf(sub, sizeof(sub), "package + platform sensors");
            } else {
                snprintf(sub, sizeof(sub), "no power sensors exposed");
            }
            break;
        }
        case SENSE_CARD_BATTERY: {
            accent = app->view->battery.capacity_pct <= 20 ? t->red :
                     (app->view->battery.capacity_pct <= 45 ? t->yellow : t->green);
            accent = accent;
            icon = "battery";
            title = "BATTERY";
            target = app->view->battery.present ? app->view->battery.capacity_pct : 0;
            if (app->view->battery.present) {
                snprintf(value, sizeof(value), "%.0f", card->shown);
                snprintf(unit, sizeof(unit), "%%");
                char duration[32];
                fmt_duration(app->view->battery.time_remaining_s, duration,
                             sizeof(duration));
                snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s",
                         app->view->battery.status,
                         app->view->battery.time_remaining_s > 0 ? duration : "AC power");
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
    if (card->kind == SENSE_CARD_TEMP) {
        snprintf(value, sizeof(value), "%.1f", card->shown);
    } else if (card->kind == SENSE_CARD_POWER && target > 0) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    } else if (card->kind == SENSE_CARD_BATTERY && app->view->battery.present) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    }

    draw_card_accent(cr, t, rect, 14, accent, card->hover.active ? 0.9 : 0.3);
    draw_icon(cr, icon, (Rect){16, 14, 14, 14}, accent, 1.6);
    text_draw(cr, layout, title, 37, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    double value_size = strlen(value) > 8 ? 18 : 25;
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

static void sensors_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    SensorList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "sensors", (Rect){17, 15, 15, 15}, t->orange, 1.7);
    text_draw(cr, layout, "SENSOR READINGS", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    char count[32];
    snprintf(count, sizeof(count), "%d sensors", app->view->sensor_count);
    text_draw(cr, layout, count, width - 18, 23, 8.6, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);

    int columns = width > 760 ? 2 : 1;
    int rows_per_column = (app->view->sensor_count + columns - 1) / columns;
    if (rows_per_column > 10) {
        rows_per_column = 10;
    }
    double column_w = (width - 36 - (columns - 1) * 24) / columns;
    for (int i = 0; i < app->view->sensor_count && i < rows_per_column * columns; i++) {
        const SensorInfo *sensor = &app->view->sensors[i];
        int col = i / rows_per_column;
        int row = i % rows_per_column;
        double x = 18 + col * (column_w + 24);
        double y = 50 + row * 36;
        char label[128];
        snprintf(label, sizeof(label), "%s", sensor->label[0] ? sensor->label : sensor->chip);
        text_draw_ellipsis(cr, layout, label, x, y + 8, 9.2, PANGO_WEIGHT_MEDIUM,
                           t->text, TEXT_LEFT, column_w - 92);
        char chip[64];
        snprintf(chip, sizeof(chip), "%s", sensor->chip);
        text_draw_ellipsis(cr, layout, chip, x, y + 21, 7.8, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, column_w - 92);
        char reading[48];
        if (sensor->kind == SENSOR_TEMP) {
            snprintf(reading, sizeof(reading), "%.1f\xc2\xb0" "C", sensor->value);
        } else {
            snprintf(reading, sizeof(reading), "%.1f %s", sensor->value, sensor->unit);
        }
        Color color = sensor->kind == SENSOR_TEMP ? temp_color(t, sensor->value) : t->dim;
        text_draw(cr, layout, reading, x + column_w, y + 12, 10, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        if (sensor->kind == SENSOR_TEMP) {
            double pct = prism_clamp(sensor->value / 100.0 * 100.0, 0, 100);
            draw_progress(cr, t, (Rect){x, y + 26, column_w, 3}, pct, color, 1.5);
        }
    }
    if (app->view->sensor_count == 0) {
        text_draw(cr, layout, "No sensors exposed by hwmon", width / 2.0,
                  height / 2.0, 11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static void battery_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    BatteryCard *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "battery", (Rect){17, 15, 15, 15}, t->green, 1.7);
    text_draw(cr, layout, "BATTERY", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim,
              TEXT_LEFT);
    const BatteryInfo *battery = &app->view->battery;
    if (!battery->present) {
        text_draw(cr, layout, "No battery detected", width / 2.0, height / 2.0, 12,
                  PANGO_WEIGHT_BOLD, t->dim, TEXT_CENTER);
        g_object_unref(layout);
        return;
    }
    Color color = battery->capacity_pct <= 20 ? t->red :
                  (battery->capacity_pct <= 45 ? t->yellow : t->green);
    char pct[32];
    snprintf(pct, sizeof(pct), "%.0f%%", battery->capacity_pct);
    text_draw(cr, layout, pct, width - 18, 49, 30, PANGO_WEIGHT_BOLD, color,
              TEXT_RIGHT);
    text_draw(cr, layout, battery->status, 18, 48, 10.5, PANGO_WEIGHT_BOLD,
              t->text, TEXT_LEFT);
    draw_progress(cr, t, (Rect){18, 68, width - 36, 8}, battery->capacity_pct,
                  color, 4);

    char remaining[64];
    fmt_duration(battery->time_remaining_s, remaining, sizeof(remaining));
    char energy[96];
    snprintf(energy, sizeof(energy), "%.1f / %.1f Wh", battery->energy_now_wh,
             battery->energy_full_wh);
    const char *labels[] = {"Runtime", "Energy", "Power", "Voltage", "Health", "Cycles"};
    char values[6][48];
    snprintf(values[0], sizeof(values[0]), "%s",
             battery->time_remaining_s > 0 ? remaining : "AC connected");
    snprintf(values[1], sizeof(values[1]), "%s", energy);
    snprintf(values[2], sizeof(values[2]), "%.1f W", battery->power_w);
    snprintf(values[3], sizeof(values[3]), "%.2f V", battery->voltage_v);
    if (battery->health_pct > 0) {
        snprintf(values[4], sizeof(values[4]), "%.0f%%", battery->health_pct);
    } else {
        snprintf(values[4], sizeof(values[4]), "n/a");
    }
    snprintf(values[5], sizeof(values[5]), "%d", battery->cycles);
    double y = 102;
    for (int i = 0; i < 6; i++) {
        text_draw(cr, layout, labels[i], 18, y, 8.7, PANGO_WEIGHT_NORMAL, t->faint,
                  TEXT_LEFT);
        text_draw(cr, layout, values[i], width - 18, y, 9.4, PANGO_WEIGHT_BOLD,
                  t->text, TEXT_RIGHT);
        y += 30;
    }
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, SensorsPage *page, SensorCardKind kind,
                            int height) {
    SensorCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_sensors_create(App *app) {
    SensorsPage *page = g_new0(SensorsPage, 1);
    page->app = app;
    page->list.app = app;
    page->battery.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (SensorCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *sensors = prism_area(app, 440, sensors_draw, &page->list);
    prism_hover_attach(sensors, &page->list.hover);
    gtk_widget_set_hexpand(sensors, TRUE);
    gtk_widget_set_size_request(sensors, 620, 440);
    gtk_box_append(GTK_BOX(row), sensors);
    GtkWidget *battery = prism_area(app, 440, battery_draw, &page->battery);
    gtk_widget_set_size_request(battery, 360, 440);
    gtk_widget_set_hexpand(battery, FALSE);
    prism_hover_attach(battery, &page->battery.hover);
    gtk_box_append(GTK_BOX(row), battery);
    gtk_box_append(GTK_BOX(content), row);

    g_object_set_data_full(G_OBJECT(content), "sensors-page", page, g_free);
    app->pages[PAGE_SENSORS].root = content;
    return prism_page(content);
}

void page_sensors_update(App *app) {
    (void)app;
}
