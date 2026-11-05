#include "ui/draw.h"
#include "core/common.h"

#include "core/format.h"
#include "core/history.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define GAUGE_START (PI * 0.75)
#define GAUGE_SWEEP (PI * 1.5)

static void set_color(cairo_t *cr, Color color, double alpha) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
}

typedef struct {
    int pixels_per_update;
    int quality;
    int fill;
    int grid;
    int glow;
} DrawPrefs;

static DrawPrefs prefs = {2, 0, 1, 1, 1};

void draw_set_preferences(int pixels_per_update, int quality, int fill, int grid,
                          int glow) {
    prefs.pixels_per_update = pixels_per_update < 1 ? 1 :
                              (pixels_per_update > 8 ? 8 : pixels_per_update);
    prefs.quality = quality;
    prefs.fill = fill;
    prefs.grid = grid;
    prefs.glow = glow;
}

void rounded_rect(cairo_t *cr, Rect rect, double radius) {
    double r = radius;
    if (r > rect.w / 2.0) {
        r = rect.w / 2.0;
    }
    if (r > rect.h / 2.0) {
        r = rect.h / 2.0;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, rect.x + rect.w - r, rect.y + r, r, -PI / 2, 0);
    cairo_arc(cr, rect.x + rect.w - r, rect.y + rect.h - r, r, 0, PI / 2);
    cairo_arc(cr, rect.x + r, rect.y + rect.h - r, r, PI / 2, PI);
    cairo_arc(cr, rect.x + r, rect.y + r, r, PI, 3 * PI / 2);
    cairo_close_path(cr);
}

void draw_card(cairo_t *cr, const Theme *theme, Rect rect, double radius) {
    cairo_save(cr);
    cairo_new_path(cr);
    rounded_rect(cr, rect, radius);
    cairo_pattern_t *gradient = cairo_pattern_create_linear(
        rect.x, rect.y, rect.x, rect.y + rect.h);
    set_color(cr, theme->card, 1.0);
    cairo_pattern_add_color_stop_rgba(gradient, 0.0,
                                      theme->card.r, theme->card.g, theme->card.b, 0.98);
    cairo_pattern_add_color_stop_rgba(gradient, 1.0,
                                      theme->card2.r, theme->card2.g, theme->card2.b, 0.96);
    cairo_set_source(cr, gradient);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(gradient);
    set_color(cr, theme->border, 0.9);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    rounded_rect(cr, (Rect){rect.x + 1, rect.y + 1, rect.w - 2, 1.0}, 1);
    set_color(cr, (Color){1, 1, 1}, 0.035);
    cairo_fill(cr);
    cairo_restore(cr);
}

void draw_card_accent(cairo_t *cr, const Theme *theme, Rect rect, double radius,
                      Color accent, double glow) {
    draw_card(cr, theme, rect, radius);
    if (glow <= 0) {
        return;
    }
    cairo_save(cr);
    cairo_new_path(cr);
    rounded_rect(cr, rect, radius);
    cairo_set_line_width(cr, 1.2);
    set_color(cr, accent, 0.28 * glow);
    cairo_stroke(cr);

    cairo_pattern_t *grad = cairo_pattern_create_radial(
        rect.x + rect.w * 0.78, rect.y + rect.h * 0.18, 2,
        rect.x + rect.w * 0.78, rect.y + rect.h * 0.18, rect.w * 0.6);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, accent.r, accent.g, accent.b, 0.13 * glow);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, accent.r, accent.g, accent.b, 0.0);
    rounded_rect(cr, rect, radius);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, rect.x + radius, rect.y + 0.7);
    cairo_line_to(cr, rect.x + rect.w - radius, rect.y + 0.7);
    cairo_set_line_width(cr, 1.4);
    set_color(cr, accent, 0.55 * glow);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void draw_separator(cairo_t *cr, const Theme *theme, double x1, double y1,
                    double x2, double y2) {
    cairo_save(cr);
    set_color(cr, theme->border, 0.7);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void layout_prepare(PangoLayout *layout, const char *family, double size,
                           PangoWeight weight) {
    if (size < 9.5) {
        size = 9.5;
    }
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, family);
    pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
    pango_font_description_set_weight(desc, weight);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_font_description_free(desc);
}

static double device_scale(cairo_t *cr, gboolean vertical) {
    cairo_matrix_t matrix;
    cairo_get_matrix(cr, &matrix);
    double scale = vertical ? matrix.yy : matrix.xx;
    if (scale < 0) {
        scale = -scale;
    }
    return scale > 0 ? scale : 1.0;
}

static double snap_to_device(cairo_t *cr, double value, gboolean vertical) {
    double scale = device_scale(cr, vertical);
    return round(value * scale) / scale;
}

static void text_draw_internal(cairo_t *cr, PangoLayout *layout,
                               const char *text, const char *family, double x,
                               double y, double size, PangoWeight weight,
                               Color color, TextAlign align, double max_width,
                               int ellipsis) {
    if (text == NULL) {
        text = "";
    }
    layout_prepare(layout, family, size, weight);
    pango_layout_set_text(layout, text, -1);
    if (ellipsis && max_width > 0) {
        pango_layout_set_width(layout, (int)(max_width * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    double width = ellipsis && max_width > 0 ? max_width : logical.width;
    double draw_x = x;
    if (align == TEXT_CENTER) {
        draw_x = x - width / 2.0;
    } else if (align == TEXT_RIGHT) {
        draw_x = x - width;
    }
    cairo_save(cr);
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_ON);
    cairo_set_font_options(cr, options);
    cairo_font_options_destroy(options);
    set_color(cr, color, 1.0);
    double draw_y = y - logical.height / 2.0;
    cairo_move_to(cr, snap_to_device(cr, draw_x, FALSE),
                  snap_to_device(cr, draw_y, TRUE));
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

void text_draw(cairo_t *cr, PangoLayout *layout, const char *text, double x,
               double y, double size, PangoWeight weight, Color color,
               TextAlign align) {
    text_draw_internal(cr, layout, text, "Ubuntu", x, y, size, weight, color,
                       align, 0, 0);
}

void text_draw_font(cairo_t *cr, PangoLayout *layout, const char *text,
                    const char *family, double x, double y, double size,
                    PangoWeight weight, Color color, TextAlign align) {
    text_draw_internal(cr, layout, text, family, x, y, size, weight, color,
                       align, 0, 0);
}

void text_draw_ellipsis(cairo_t *cr, PangoLayout *layout, const char *text,
                        double x, double y, double size, PangoWeight weight,
                        Color color, TextAlign align, double max_width) {
    text_draw_internal(cr, layout, text, "Ubuntu", x, y, size, weight, color,
                       align, max_width, 1);
}

double text_width(cairo_t *cr, PangoLayout *layout, const char *text,
                  double size, PangoWeight weight) {
    (void)cr;
    layout_prepare(layout, "Ubuntu", size, weight);
    pango_layout_set_text(layout, text != NULL ? text : "", -1);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    return logical.width;
}

double text_width_font(cairo_t *cr, PangoLayout *layout, const char *text,
                       const char *family, double size, PangoWeight weight) {
    (void)cr;
    layout_prepare(layout, family, size, weight);
    pango_layout_set_text(layout, text != NULL ? text : "", -1);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, NULL, &logical);
    return logical.width;
}

double text_draw_fit(cairo_t *cr, PangoLayout *layout, const char *text,
                     double x, double y, double size, double min_size,
                     PangoWeight weight, Color color, TextAlign align,
                     double max_width) {
    const char *value = text != NULL ? text : "";
    double chosen = size;
    double width = text_width(cr, layout, value, chosen, weight);
    while (width > max_width && chosen > min_size) {
        chosen -= 0.5;
        width = text_width(cr, layout, value, chosen, weight);
    }
    if (width > max_width && max_width > 0) {
        text_draw_ellipsis(cr, layout, value, x, y, chosen, weight, color, align,
                           max_width);
        return max_width;
    }
    text_draw(cr, layout, value, x, y, chosen, weight, color, align);
    return width;
}

void draw_gauge(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double cx, double cy, double radius, double thickness,
                double pct, Color color, const char *value, const char *unit) {
    pct = prism_clamp(pct, 0, 100);
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    int ticks = 44;
    for (int i = 0; i <= ticks; i++) {
        double angle = GAUGE_START + GAUGE_SWEEP * i / ticks;
        double inner = radius + thickness * 0.5 + 5.0;
        double outer = inner + (i % 11 == 0 ? 7.0 : 3.5);
        cairo_set_line_width(cr, i % 11 == 0 ? 1.4 : 0.8);
        set_color(cr, theme->faint, i % 11 == 0 ? 0.55 : 0.25);
        cairo_move_to(cr, cx + cos(angle) * inner, cy + sin(angle) * inner);
        cairo_line_to(cr, cx + cos(angle) * outer, cy + sin(angle) * outer);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, thickness);
    cairo_arc(cr, cx, cy, radius, GAUGE_START, GAUGE_START + GAUGE_SWEEP);
    set_color(cr, theme->track, 0.75);
    cairo_stroke(cr);

    cairo_set_line_width(cr, thickness + 9);
    cairo_arc(cr, cx, cy, radius, GAUGE_START,
              GAUGE_START + GAUGE_SWEEP * pct / 100.0);
    set_color(cr, color, 0.10);
    cairo_stroke(cr);

    cairo_set_line_width(cr, thickness);
    cairo_arc(cr, cx, cy, radius, GAUGE_START,
              GAUGE_START + GAUGE_SWEEP * pct / 100.0);
    set_color(cr, color, 1.0);
    cairo_stroke(cr);

    cairo_set_line_width(cr, thickness * 0.26);
    cairo_arc(cr, cx, cy, radius + thickness * 0.32, GAUGE_START,
              GAUGE_START + GAUGE_SWEEP * pct / 100.0);
    set_color(cr, (Color){1, 1, 1}, 0.16);
    cairo_stroke(cr);

    double value_size = radius * 0.62;
    if (value == NULL) {
        value = "";
    }
    if (strlen(value) > 6) {
        value_size = radius * 0.48;
    }
    text_draw(cr, layout, value, cx, cy - radius * 0.06, value_size,
              PANGO_WEIGHT_BOLD, theme->text, TEXT_CENTER);
    if (unit != NULL && *unit != '\0') {
        text_draw(cr, layout, unit, cx, cy + radius * 0.42, radius * 0.22,
                  PANGO_WEIGHT_MEDIUM, theme->dim, TEXT_CENTER);
    }
    cairo_restore(cr);
}

void draw_progress(cairo_t *cr, const Theme *theme, Rect rect, double pct,
                   Color color, double radius) {
    pct = prism_clamp(pct, 0, 100);
    cairo_save(cr);
    rounded_rect(cr, rect, radius);
    set_color(cr, theme->track, 0.8);
    cairo_fill(cr);
    if (pct > 0) {
        Rect fill = rect;
        fill.w = rect.w * pct / 100.0;
        if (fill.w < radius * 2) {
            fill.w = radius * 2;
        }
        rounded_rect(cr, fill, radius);
        set_color(cr, color, 1.0);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void draw_sparkline(cairo_t *cr, const Theme *theme, Rect rect,
                    const float *series, const History *hist, int window,
                    Color color, int filled) {
    int count = history_count(hist);
    if (count < 2 || window < 2) {
        return;
    }
    int shown = prism_min_int(count, window);
    double max = 0.0;
    for (int j = 0; j < shown; j++) {
        double v = history_get(series, hist, shown - 1 - j);
        if (v > max) {
            max = v;
        }
    }
    if (max <= 0) {
        max = 1;
    }
    double step = rect.w / (window - 1);
    double first = rect.x + (window - shown) * step;
    cairo_save(cr);
    if (filled) {
        cairo_new_path(cr);
        cairo_move_to(cr, first, rect.y + rect.h);
        for (int j = 0; j < shown; j++) {
            double v = history_get(series, hist, shown - 1 - j);
            double x = first + j * step;
            double y = rect.y + rect.h - (v / max) * rect.h;
            cairo_line_to(cr, x, y);
        }
        cairo_line_to(cr, first + (shown - 1) * step, rect.y + rect.h);
        cairo_close_path(cr);
        cairo_pattern_t *grad = cairo_pattern_create_linear(
            0, rect.y, 0, rect.y + rect.h);
        cairo_pattern_add_color_stop_rgba(grad, 0, color.r, color.g, color.b, 0.28);
        cairo_pattern_add_color_stop_rgba(grad, 1, color.r, color.g, color.b, 0.0);
        cairo_set_source(cr, grad);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
    }
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (int j = 0; j < shown; j++) {
        double v = history_get(series, hist, shown - 1 - j);
        double x = first + j * step;
        double y = rect.y + rect.h - (v / max) * rect.h;
        if (j == 0) {
            cairo_move_to(cr, x, y);
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    set_color(cr, color, 0.95);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void chart_format_value(ChartFormat format, double value, char *buf,
                        size_t buf_len) {
    switch (format) {
        case CHART_PERCENT:
            snprintf(buf, buf_len, "%.0f%%", value);
            break;
        case CHART_RATE:
            fmt_rate(value, buf, buf_len);
            break;
        case CHART_MB:
            fmt_bytes(value * 1024.0 * 1024.0, buf, buf_len);
            break;
        case CHART_TEMP:
            snprintf(buf, buf_len, "%.0f\xc2\xb0", value);
            break;
        case CHART_LOAD:
        case CHART_NUMBER:
            fmt_number(value, buf, buf_len);
            break;
        default:
            snprintf(buf, buf_len, "%.0f", value);
            break;
    }
}

static double nice_max(double value) {
    if (value <= 0) {
        return 1;
    }
    double exponent = floor(log10(value));
    double scale = pow(10, exponent);
    double normalized = value / scale;
    double nice;
    if (normalized <= 1.0) {
        nice = 1.0;
    } else if (normalized <= 2.0) {
        nice = 2.0;
    } else if (normalized <= 5.0) {
        nice = 5.0;
    } else {
        nice = 10.0;
    }
    return nice * scale;
}

static double chart_value(const ChartSeries *series, const History *hist,
                          int shown, int index) {
    return history_get(series->data, hist, shown - 1 - index);
}

void draw_chart(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                const ChartSpec *spec) {
    Rect rect = spec->rect;
    cairo_save(cr);
    rounded_rect(cr, rect, 10);
    set_color(cr, theme->bg2, 0.55);
    cairo_fill(cr);
    cairo_restore(cr);

    Rect plot = {
        rect.x + 48,
        rect.y + 10,
        rect.w - 60,
        rect.h - 32
    };
    if (plot.w < 20 || plot.h < 20) {
        return;
    }
    int count = history_count(spec->history);
    int capacity = spec->window > 1 ? spec->window : PRISM_HIST;
    if (capacity > PRISM_HIST) {
        capacity = PRISM_HIST;
    }
    int shown = prism_min_int(count, capacity);

    double max_value = spec->ymax;
    if (spec->format == CHART_PERCENT && max_value <= 0) {
        max_value = 100;
    }
    if (max_value <= 0) {
        max_value = 0;
        for (int j = 0; j < shown; j++) {
            double stack = 0;
            for (int s = 0; s < spec->series_count; s++) {
                double v = chart_value(&spec->series[s], spec->history, shown, j);
                if (spec->stacked) {
                    stack += v;
                } else if (v > max_value) {
                    max_value = v;
                }
            }
            if (spec->stacked && stack > max_value) {
                max_value = stack;
            }
        }
        max_value = nice_max(max_value * 1.12);
    }
    if (max_value <= 0) {
        max_value = 1;
    }

    for (int i = 0; i <= 4; i++) {
        double value = max_value * (4 - i) / 4.0;
        double y = plot.y + plot.h * i / 4.0;
        if (prefs.grid) {
            cairo_save(cr);
            cairo_set_line_width(cr, 1);
            cairo_set_dash(cr, (double[]){2.0, 5.0}, 2, 0);
            set_color(cr, theme->grid, 0.9);
            cairo_move_to(cr, plot.x, y);
            cairo_line_to(cr, plot.x + plot.w, y);
            cairo_stroke(cr);
            cairo_restore(cr);
        }
        char label[48];
        chart_format_value(spec->format, value, label, sizeof(label));
        text_draw(cr, layout, label, plot.x - 8, y, 9, PANGO_WEIGHT_NORMAL,
                  theme->faint, TEXT_RIGHT);
    }

    if (shown < 2) {
        text_draw(cr, layout, "Collecting samples...", plot.x + plot.w / 2,
                  plot.y + plot.h / 2, 11, PANGO_WEIGHT_NORMAL, theme->faint,
                  TEXT_CENTER);
        return;
    }

    double step = plot.w / (capacity - 1);
    double first_x = plot.x + (capacity - shown) * step;
    double sample_seconds = spec->seconds_per_sample > 0 ? spec->seconds_per_sample : 1.0;

    cairo_save(cr);
    cairo_rectangle(cr, plot.x, plot.y - 1, plot.w, plot.h + 2);
    cairo_clip(cr);

    if (spec->stacked) {
        for (int s = 0; s < spec->series_count; s++) {
            cairo_new_path(cr);
            for (int j = 0; j < shown; j++) {
                double total = 0;
                for (int k = 0; k <= s; k++) {
                    total += chart_value(&spec->series[k], spec->history, shown, j);
                }
                double x = first_x + j * step;
                double y = plot.y + plot.h - prism_clamp(total / max_value, 0, 1) * plot.h;
                if (j == 0) {
                    cairo_move_to(cr, x, y);
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
            for (int j = shown - 1; j >= 0; j--) {
                double lower = 0;
                for (int k = 0; k < s; k++) {
                    lower += chart_value(&spec->series[k], spec->history, shown, j);
                }
                double x = first_x + j * step;
                double y = plot.y + plot.h - prism_clamp(lower / max_value, 0, 1) * plot.h;
                cairo_line_to(cr, x, y);
            }
            cairo_close_path(cr);
            Color color = spec->series[s].color;
            if (prefs.fill) {
                cairo_pattern_t *grad = cairo_pattern_create_linear(
                    0, plot.y, 0, plot.y + plot.h);
                cairo_pattern_add_color_stop_rgba(grad, 0, color.r, color.g, color.b, 0.42);
                cairo_pattern_add_color_stop_rgba(grad, 1, color.r, color.g, color.b, 0.08);
                cairo_set_source(cr, grad);
                cairo_fill(cr);
                cairo_pattern_destroy(grad);
            }

            cairo_set_line_width(cr, 1.6);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (int j = 0; j < shown; j++) {
                double total = 0;
                for (int k = 0; k <= s; k++) {
                    total += chart_value(&spec->series[k], spec->history, shown, j);
                }
                double x = first_x + j * step;
                double y = plot.y + plot.h - prism_clamp(total / max_value, 0, 1) * plot.h;
                if (j == 0) {
                    cairo_move_to(cr, x, y);
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
            set_color(cr, color, 1.0);
            cairo_stroke(cr);
        }
    } else {
        for (int s = 0; s < spec->series_count; s++) {
            const ChartSeries *series = &spec->series[s];
            cairo_new_path(cr);
            cairo_move_to(cr, first_x, plot.y + plot.h);
            for (int j = 0; j < shown; j++) {
                double v = chart_value(series, spec->history, shown, j);
                double x = first_x + j * step;
                double y = plot.y + plot.h - prism_clamp(v / max_value, 0, 1) * plot.h;
                cairo_line_to(cr, x, y);
            }
            cairo_line_to(cr, first_x + (shown - 1) * step, plot.y + plot.h);
            cairo_close_path(cr);
            if (prefs.fill) {
                cairo_pattern_t *grad = cairo_pattern_create_linear(
                    0, plot.y, 0, plot.y + plot.h);
                cairo_pattern_add_color_stop_rgba(grad, 0, series->color.r,
                                                  series->color.g, series->color.b, 0.24);
                cairo_pattern_add_color_stop_rgba(grad, 1, series->color.r,
                                                  series->color.g, series->color.b, 0.01);
                cairo_set_source(cr, grad);
                cairo_fill(cr);
                cairo_pattern_destroy(grad);
            }

            cairo_new_path(cr);
            for (int j = 0; j < shown; j++) {
                double v = chart_value(series, spec->history, shown, j);
                double x = first_x + j * step;
                double y = plot.y + plot.h - prism_clamp(v / max_value, 0, 1) * plot.h;
                if (j == 0) {
                    cairo_move_to(cr, x, y);
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            if (prefs.glow && prefs.quality < 3) {
                cairo_set_line_width(cr, 6.0);
                set_color(cr, series->color, 0.10);
                cairo_stroke_preserve(cr);
            }
            cairo_set_line_width(cr, prefs.quality >= 2 ? 1.6 : 2.0);
            set_color(cr, series->color, 1.0);
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);

    for (int i = 0; i <= 4; i++) {
        double x = plot.x + plot.w * i / 4.0;
        int j = (int)lround((x - first_x) / step);
        double seconds = (shown - 1 - j) * sample_seconds;
        if (seconds < -1) {
            seconds = 0;
        }
        char label[32];
        if (seconds < 1.5) {
            snprintf(label, sizeof(label), "now");
        } else if (seconds < 60) {
            snprintf(label, sizeof(label), "-%ds", (int)seconds);
        } else {
            snprintf(label, sizeof(label), "-%dm", (int)(seconds / 60));
        }
        TextAlign align = TEXT_CENTER;
        if (i == 0) {
            align = TEXT_LEFT;
        } else if (i == 4) {
            align = TEXT_RIGHT;
        }
        text_draw(cr, layout, label, x, rect.y + rect.h - 10, 9,
                  PANGO_WEIGHT_NORMAL, theme->faint, align);
    }

    if (spec->hover && spec->hover_x >= plot.x && spec->hover_x <= plot.x + plot.w &&
        spec->hover_y >= plot.y && spec->hover_y <= plot.y + plot.h) {
        int j = (int)lround((spec->hover_x - first_x) / step);
        j = prism_max_int(0, prism_min_int(shown - 1, j));
        double x = first_x + j * step;
        cairo_save(cr);
        cairo_set_dash(cr, (double[]){3.0, 4.0}, 2, 0);
        cairo_set_line_width(cr, 1);
        set_color(cr, theme->text, 0.22);
        cairo_move_to(cr, x, plot.y);
        cairo_line_to(cr, x, plot.y + plot.h);
        cairo_stroke(cr);
        cairo_restore(cr);

        double cumulative = 0;
        for (int s = 0; s < spec->series_count; s++) {
            double v = chart_value(&spec->series[s], spec->history, shown, j);
            double top = spec->stacked ? cumulative + v : v;
            cumulative += v;
            double y = plot.y + plot.h - prism_clamp(top / max_value, 0, 1) * plot.h;
            cairo_save(cr);
            cairo_new_path(cr);
            cairo_arc(cr, x, y, 3.4, 0, 2 * PI);
            set_color(cr, spec->series[s].color, 1.0);
            cairo_fill(cr);
            cairo_set_line_width(cr, 1.4);
            set_color(cr, theme->card, 1.0);
            cairo_stroke(cr);
            cairo_restore(cr);
        }

        char title[48];
        double seconds_ago = (shown - 1 - j) * sample_seconds;
        if (seconds_ago < 1.5) {
            snprintf(title, sizeof(title), "now");
        } else {
            snprintf(title, sizeof(title), "%.0fs ago", seconds_ago);
        }
        double width = 146;
        for (int s = 0; s < spec->series_count; s++) {
            double label_width = spec->series[s].label != NULL
                ? text_width(cr, layout, spec->series[s].label, 9.5, PANGO_WEIGHT_NORMAL)
                : 0;
            if (label_width + 70 > width) {
                width = label_width + 70;
            }
        }
        if (width > plot.w - 8) {
            width = plot.w - 8;
        }
        double height = 26 + spec->series_count * 16;
        double tx = x + 14;
        double ty = spec->hover_y - height - 10;
        if (tx + width > plot.x + plot.w) {
            tx = x - width - 14;
        }
        if (tx < plot.x) {
            tx = plot.x + 4;
        }
        if (tx + width > plot.x + plot.w) {
            tx = plot.x + plot.w - width;
        }
        if (ty < rect.y + 4) {
            ty = spec->hover_y + 14;
        }
        if (ty + height > rect.y + rect.h) {
            ty = rect.y + rect.h - height - 4;
        }
        if (ty < rect.y + 4) {
            ty = rect.y + 4;
        }
        Rect tip = {tx, ty, width, height};
        cairo_save(cr);
        rounded_rect(cr, tip, 9);
        set_color(cr, theme->panel, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1);
        set_color(cr, theme->border, 1.0);
        cairo_stroke(cr);
        text_draw(cr, layout, title, tip.x + 10, tip.y + 13, 9,
                  PANGO_WEIGHT_NORMAL, theme->faint, TEXT_LEFT);
        for (int s = 0; s < spec->series_count; s++) {
            double ly = tip.y + 30 + s * 16;
            cairo_new_path(cr);
            cairo_arc(cr, tip.x + 13, ly, 3, 0, 2 * PI);
            set_color(cr, spec->series[s].color, 1.0);
            cairo_fill(cr);
            text_draw(cr, layout, spec->series[s].label, tip.x + 22, ly, 9.5,
                      PANGO_WEIGHT_NORMAL, theme->dim, TEXT_LEFT);
            double v = chart_value(&spec->series[s], spec->history, shown, j);
            char value[48];
            chart_format_value(spec->format, v, value, sizeof(value));
            text_draw(cr, layout, value, tip.x + width - 10, ly, 9.5,
                      PANGO_WEIGHT_BOLD, theme->text, TEXT_RIGHT);
        }
        cairo_restore(cr);
    }
}

void draw_donut(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double cx, double cy, double radius, double thickness,
                const DonutSegment *segments, int segment_count,
                const char *center_value, const char *center_label) {
    double total = 0;
    for (int i = 0; i < segment_count; i++) {
        total += segments[i].value;
    }
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_line_width(cr, thickness);
    cairo_arc(cr, cx, cy, radius, 0, 2 * PI);
    set_color(cr, theme->track, 0.65);
    cairo_stroke(cr);
    if (total > 0) {
        double angle = -PI / 2;
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        for (int i = 0; i < segment_count; i++) {
            double sweep = segments[i].value / total * 2 * PI;
            if (sweep <= 0.001) {
                continue;
            }
            double gap = 0.014;
            cairo_set_line_width(cr, thickness);
            cairo_arc(cr, cx, cy, radius, angle + gap, angle + sweep - gap);
            set_color(cr, segments[i].color, 1.0);
            cairo_stroke(cr);
            angle += sweep;
        }
    }
    if (center_value != NULL) {
        text_draw(cr, layout, center_value, cx, cy - 7, radius * 0.36,
                  PANGO_WEIGHT_BOLD, theme->text, TEXT_CENTER);
    }
    if (center_label != NULL) {
        text_draw(cr, layout, center_label, cx, cy + radius * 0.32, radius * 0.14,
                  PANGO_WEIGHT_NORMAL, theme->dim, TEXT_CENTER);
    }
    cairo_restore(cr);
}

void draw_badge(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double x, double y, const char *text, Color color) {
    double width = text_width(cr, layout, text, 9.5, PANGO_WEIGHT_BOLD) + 18;
    Rect rect = {x, y - 9, width, 18};
    cairo_save(cr);
    rounded_rect(cr, rect, 9);
    set_color(cr, color, 0.14);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1);
    set_color(cr, color, 0.35);
    cairo_stroke(cr);
    text_draw(cr, layout, text, x + width / 2, y, 9.5, PANGO_WEIGHT_BOLD,
              color, TEXT_CENTER);
    cairo_restore(cr);
}
