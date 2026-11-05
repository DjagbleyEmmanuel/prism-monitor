#ifndef PRISM_DRAW_H
#define PRISM_DRAW_H

#include "core/types.h"
#include "ui/theme.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

typedef struct {
    double x;
    double y;
    double w;
    double h;
} Rect;

typedef enum {
    TEXT_LEFT,
    TEXT_CENTER,
    TEXT_RIGHT
} TextAlign;

typedef enum {
    CHART_PERCENT,
    CHART_RATE,
    CHART_MB,
    CHART_TEMP,
    CHART_LOAD,
    CHART_NUMBER
} ChartFormat;

typedef struct {
    const float *data;
    Color color;
    const char *label;
} ChartSeries;

typedef struct {
    const History *history;
    Rect rect;
    const ChartSeries *series;
    int series_count;
    double ymax;
    ChartFormat format;
    double seconds_per_sample;
    int window;
    int stacked;
    int hover;
    double hover_x;
    double hover_y;
} ChartSpec;

typedef struct {
    double value;
    Color color;
    const char *label;
} DonutSegment;

void rounded_rect(cairo_t *cr, Rect rect, double radius);
void draw_card(cairo_t *cr, const Theme *theme, Rect rect, double radius);
void draw_card_accent(cairo_t *cr, const Theme *theme, Rect rect, double radius,
                      Color accent, double glow);
void draw_separator(cairo_t *cr, const Theme *theme, double x1, double y1,
                    double x2, double y2);

void text_draw(cairo_t *cr, PangoLayout *layout, const char *text, double x,
               double y, double size, PangoWeight weight, Color color,
               TextAlign align);
void text_draw_font(cairo_t *cr, PangoLayout *layout, const char *text,
                    const char *family, double x, double y, double size,
                    PangoWeight weight, Color color, TextAlign align);
void text_draw_ellipsis(cairo_t *cr, PangoLayout *layout, const char *text,
                        double x, double y, double size, PangoWeight weight,
                        Color color, TextAlign align, double max_width);
double text_width(cairo_t *cr, PangoLayout *layout, const char *text,
                  double size, PangoWeight weight);
double text_draw_fit(cairo_t *cr, PangoLayout *layout, const char *text,
                     double x, double y, double size, double min_size,
                     PangoWeight weight, Color color, TextAlign align,
                     double max_width);
double text_width_font(cairo_t *cr, PangoLayout *layout, const char *text,
                       const char *family, double size, PangoWeight weight);

void draw_gauge(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double cx, double cy, double radius, double thickness,
                double pct, Color color, const char *value, const char *unit);
void draw_progress(cairo_t *cr, const Theme *theme, Rect rect, double pct,
                   Color color, double radius);
void draw_sparkline(cairo_t *cr, const Theme *theme, Rect rect,
                    const float *series, const History *hist, int window,
                    Color color, int filled);
void draw_chart(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                const ChartSpec *spec);
void draw_donut(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double cx, double cy, double radius, double thickness,
                const DonutSegment *segments, int segment_count,
                const char *center_value, const char *center_label);
void chart_format_value(ChartFormat format, double value, char *buf,
                        size_t buf_len);
void draw_badge(cairo_t *cr, PangoLayout *layout, const Theme *theme,
                double x, double y, const char *text, Color color);
void draw_set_preferences(int pixels_per_update, int quality, int fill, int grid,
                          int glow);

#endif
