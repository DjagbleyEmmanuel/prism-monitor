#include "ui/icons.h"

#include <math.h>
#include <string.h>

static void set_color(cairo_t *cr, Color color, double alpha) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
}

#define ICON_PI 3.14159265358979323846

static double ix(Rect r, double fx) {
    return r.x + fx * r.w;
}

static double iy(Rect r, double fy) {
    return r.y + fy * r.h;
}

static void line(cairo_t *cr, Rect r, double x1, double y1, double x2, double y2) {
    cairo_move_to(cr, ix(r, x1), iy(r, y1));
    cairo_line_to(cr, ix(r, x2), iy(r, y2));
}

static void rect_path(cairo_t *cr, Rect r, double x, double y, double w, double h,
                      double radius) {
    Rect sub = {ix(r, x), iy(r, y), w * r.w, h * r.h};
    rounded_rect(cr, sub, radius * r.w);
}

void draw_icon(cairo_t *cr, const char *name, Rect rect, Color color,
               double line_width) {
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, 1.0);
    cairo_set_line_width(cr, line_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (strcmp(name, "grid") == 0) {
        rect_path(cr, rect, 0.16, 0.16, 0.28, 0.28, 0.04);
        rect_path(cr, rect, 0.56, 0.16, 0.28, 0.28, 0.04);
        rect_path(cr, rect, 0.16, 0.56, 0.28, 0.28, 0.04);
        rect_path(cr, rect, 0.56, 0.56, 0.28, 0.28, 0.04);
        cairo_stroke(cr);
    } else if (strcmp(name, "cpu") == 0) {
        rect_path(cr, rect, 0.22, 0.22, 0.56, 0.56, 0.08);
        cairo_stroke(cr);
        for (int i = 0; i < 3; i++) {
            double p = 0.32 + i * 0.18;
            line(cr, rect, p, 0.10, p, 0.22);
            line(cr, rect, p, 0.78, p, 0.90);
            line(cr, rect, 0.10, p, 0.22, p);
            line(cr, rect, 0.78, p, 0.90, p);
        }
        cairo_stroke(cr);
        rect_path(cr, rect, 0.38, 0.38, 0.24, 0.24, 0.03);
        cairo_stroke(cr);
    } else if (strcmp(name, "memory") == 0) {
        rect_path(cr, rect, 0.12, 0.30, 0.76, 0.40, 0.06);
        cairo_stroke(cr);
        for (int i = 0; i < 4; i++) {
            double x = 0.24 + i * 0.15;
            line(cr, rect, x, 0.40, x, 0.60);
        }
        cairo_stroke(cr);
        line(cr, rect, 0.28, 0.70, 0.28, 0.82);
        line(cr, rect, 0.72, 0.70, 0.72, 0.82);
        cairo_stroke(cr);
    } else if (strcmp(name, "gpu") == 0) {
        rect_path(cr, rect, 0.12, 0.24, 0.76, 0.52, 0.07);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.40), iy(rect, 0.50), rect.w * 0.13, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        for (int i = 0; i < 3; i++) {
            double a = -ICON_PI / 2 + i * 2 * ICON_PI / 3;
            cairo_move_to(cr, ix(rect, 0.40), iy(rect, 0.50));
            cairo_line_to(cr, ix(rect, 0.40) + cos(a) * rect.w * 0.12,
                          iy(rect, 0.50) + sin(a) * rect.w * 0.12);
        }
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.69), iy(rect, 0.43), rect.w * 0.045, 0, 2 * ICON_PI);
        cairo_fill(cr);
        line(cr, rect, 0.16, 0.84, 0.26, 0.74);
        line(cr, rect, 0.30, 0.84, 0.40, 0.74);
        cairo_stroke(cr);
    } else if (strcmp(name, "disk") == 0) {
        rect_path(cr, rect, 0.18, 0.12, 0.64, 0.76, 0.14);
        cairo_stroke(cr);
        line(cr, rect, 0.18, 0.50, 0.82, 0.50);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.68), iy(rect, 0.68), rect.w * 0.035, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "network") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.22), rect.w * 0.09, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.22), iy(rect, 0.78), rect.w * 0.09, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.78), iy(rect, 0.78), rect.w * 0.09, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        line(cr, rect, 0.46, 0.30, 0.26, 0.69);
        line(cr, rect, 0.54, 0.30, 0.74, 0.69);
        line(cr, rect, 0.31, 0.78, 0.69, 0.78);
        cairo_stroke(cr);
    } else if (strcmp(name, "processes") == 0) {
        for (int i = 0; i < 3; i++) {
            double y = 0.28 + i * 0.22;
            cairo_arc(cr, ix(rect, 0.20), iy(rect, y), rect.w * 0.045, 0, 2 * ICON_PI);
            cairo_fill(cr);
            line(cr, rect, 0.34, y, 0.82, y);
        }
        cairo_stroke(cr);
    } else if (strcmp(name, "sensors") == 0) {
        cairo_move_to(cr, ix(rect, 0.38), iy(rect, 0.16));
        cairo_line_to(cr, ix(rect, 0.62), iy(rect, 0.16));
        cairo_line_to(cr, ix(rect, 0.62), iy(rect, 0.62));
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.72), rect.w * 0.21,
                  -ICON_PI / 6, ICON_PI + ICON_PI / 6);
        cairo_close_path(cr);
        cairo_stroke(cr);
        line(cr, rect, 0.50, 0.26, 0.50, 0.66);
        cairo_stroke(cr);
    } else if (strcmp(name, "system") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.34, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.34), rect.w * 0.035, 0, 2 * ICON_PI);
        cairo_fill(cr);
        line(cr, rect, 0.50, 0.46, 0.50, 0.68);
        cairo_stroke(cr);
    } else if (strcmp(name, "settings") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.16, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.32, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        for (int i = 0; i < 8; i++) {
            double a = i * 2 * ICON_PI / 8;
            line(cr, rect, 0.50 + cos(a) * 0.32, 0.50 + sin(a) * 0.32,
                 0.50 + cos(a) * 0.43, 0.50 + sin(a) * 0.43);
        }
        cairo_stroke(cr);
    } else if (strcmp(name, "palette") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.52), rect.w * 0.33, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.36), iy(rect, 0.40), rect.w * 0.045, 0, 2 * ICON_PI);
        cairo_fill(cr);
        cairo_arc(cr, ix(rect, 0.55), iy(rect, 0.33), rect.w * 0.045, 0, 2 * ICON_PI);
        cairo_fill(cr);
        cairo_arc(cr, ix(rect, 0.68), iy(rect, 0.48), rect.w * 0.045, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "search") == 0) {
        cairo_arc(cr, ix(rect, 0.44), iy(rect, 0.44), rect.w * 0.25, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        line(cr, rect, 0.62, 0.62, 0.84, 0.84);
        cairo_stroke(cr);
    } else if (strcmp(name, "battery") == 0) {
        rect_path(cr, rect, 0.12, 0.30, 0.68, 0.40, 0.05);
        cairo_stroke(cr);
        line(cr, rect, 0.80, 0.42, 0.88, 0.42);
        line(cr, rect, 0.88, 0.42, 0.88, 0.58);
        line(cr, rect, 0.88, 0.58, 0.80, 0.58);
        cairo_stroke(cr);
        line(cr, rect, 0.24, 0.50, 0.58, 0.50);
        cairo_stroke(cr);
    } else if (strcmp(name, "user") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.34), rect.w * 0.16, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.86), rect.w * 0.30,
                  ICON_PI + 0.35, 2 * ICON_PI - 0.35);
        cairo_stroke(cr);
    } else if (strcmp(name, "service") == 0) {
        for (int i = 0; i < 6; i++) {
            double a = i * 2 * ICON_PI / 6 - ICON_PI / 2;
            double x = 0.50 + cos(a) * 0.32;
            double y = 0.50 + sin(a) * 0.32;
            if (i == 0) {
                cairo_move_to(cr, ix(rect, x), iy(rect, y));
            } else {
                cairo_line_to(cr, ix(rect, x), iy(rect, y));
            }
        }
        cairo_close_path(cr);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.10, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "startup") == 0) {
        rect_path(cr, rect, 0.16, 0.58, 0.68, 0.28, 0.05);
        cairo_stroke(cr);
        line(cr, rect, 0.50, 0.52, 0.50, 0.16);
        line(cr, rect, 0.34, 0.30, 0.50, 0.14);
        line(cr, rect, 0.66, 0.30, 0.50, 0.14);
        cairo_stroke(cr);
    } else if (strcmp(name, "refresh") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.31,
                  -ICON_PI * 0.35, ICON_PI * 1.2);
        cairo_stroke(cr);
        line(cr, rect, 0.80, 0.22, 0.82, 0.44);
        line(cr, rect, 0.80, 0.22, 0.60, 0.25);
        cairo_stroke(cr);
    } else if (strcmp(name, "more") == 0) {
        for (int i = 0; i < 3; i++) {
            cairo_arc(cr, ix(rect, 0.28 + i * 0.22), iy(rect, 0.50),
                      rect.w * 0.055, 0, 2 * ICON_PI);
            cairo_fill(cr);
        }
    } else if (strcmp(name, "close") == 0) {
        line(cr, rect, 0.26, 0.26, 0.74, 0.74);
        line(cr, rect, 0.74, 0.26, 0.26, 0.74);
        cairo_stroke(cr);
    } else if (strcmp(name, "power") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.54), rect.w * 0.30,
                  -ICON_PI * 0.35, ICON_PI * 1.35);
        cairo_stroke(cr);
        line(cr, rect, 0.50, 0.14, 0.50, 0.48);
        cairo_stroke(cr);
    } else if (strcmp(name, "terminate") == 0) {
        rect_path(cr, rect, 0.28, 0.28, 0.44, 0.44, 0.05);
        cairo_stroke(cr);
    } else if (strcmp(name, "pause") == 0) {
        rect_path(cr, rect, 0.30, 0.24, 0.13, 0.52, 0.03);
        rect_path(cr, rect, 0.57, 0.24, 0.13, 0.52, 0.03);
        cairo_fill(cr);
    } else if (strcmp(name, "play") == 0) {
        cairo_move_to(cr, ix(rect, 0.34), iy(rect, 0.22));
        cairo_line_to(cr, ix(rect, 0.78), iy(rect, 0.50));
        cairo_line_to(cr, ix(rect, 0.34), iy(rect, 0.78));
        cairo_close_path(cr);
        cairo_fill(cr);
    } else if (strcmp(name, "check") == 0) {
        line(cr, rect, 0.22, 0.52, 0.43, 0.72);
        line(cr, rect, 0.43, 0.72, 0.80, 0.28);
        cairo_stroke(cr);
    } else if (strcmp(name, "chevron_right") == 0) {
        line(cr, rect, 0.40, 0.26, 0.64, 0.50);
        line(cr, rect, 0.64, 0.50, 0.40, 0.74);
        cairo_stroke(cr);
    } else if (strcmp(name, "chevron_down") == 0) {
        line(cr, rect, 0.26, 0.40, 0.50, 0.64);
        line(cr, rect, 0.50, 0.64, 0.74, 0.40);
        cairo_stroke(cr);
    } else if (strcmp(name, "arrow_up") == 0) {
        line(cr, rect, 0.50, 0.78, 0.50, 0.24);
        line(cr, rect, 0.28, 0.46, 0.50, 0.24);
        line(cr, rect, 0.72, 0.46, 0.50, 0.24);
        cairo_stroke(cr);
    } else if (strcmp(name, "arrow_down") == 0) {
        line(cr, rect, 0.50, 0.22, 0.50, 0.76);
        line(cr, rect, 0.28, 0.54, 0.50, 0.76);
        line(cr, rect, 0.72, 0.54, 0.50, 0.76);
        cairo_stroke(cr);
    } else if (strcmp(name, "alert") == 0) {
        cairo_move_to(cr, ix(rect, 0.50), iy(rect, 0.14));
        cairo_line_to(cr, ix(rect, 0.88), iy(rect, 0.82));
        cairo_line_to(cr, ix(rect, 0.12), iy(rect, 0.82));
        cairo_close_path(cr);
        cairo_stroke(cr);
        line(cr, rect, 0.50, 0.36, 0.50, 0.60);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.70), rect.w * 0.025, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "apps") == 0) {
        rect_path(cr, rect, 0.16, 0.16, 0.30, 0.30, 0.04);
        rect_path(cr, rect, 0.54, 0.16, 0.30, 0.30, 0.04);
        rect_path(cr, rect, 0.16, 0.54, 0.30, 0.30, 0.04);
        rect_path(cr, rect, 0.54, 0.54, 0.30, 0.30, 0.04);
        cairo_stroke(cr);
    } else if (strcmp(name, "chip") == 0) {
        rect_path(cr, rect, 0.26, 0.26, 0.48, 0.48, 0.05);
        cairo_stroke(cr);
        for (int i = 0; i < 4; i++) {
            double p = 0.32 + i * 0.12;
            line(cr, rect, p, 0.12, p, 0.26);
            line(cr, rect, p, 0.74, p, 0.88);
            line(cr, rect, 0.12, p, 0.26, p);
            line(cr, rect, 0.74, p, 0.88, p);
        }
        cairo_stroke(cr);
    } else if (strcmp(name, "gauge") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.56), rect.w * 0.34,
                  ICON_PI, 2 * ICON_PI);
        cairo_stroke(cr);
        line(cr, rect, 0.50, 0.56, 0.68, 0.34);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.56), rect.w * 0.04, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "record") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.30, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.12, 0, 2 * ICON_PI);
        cairo_fill(cr);
    } else if (strcmp(name, "logo") == 0) {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.38, 0, 2 * ICON_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, ix(rect, 0.50), iy(rect, 0.20));
        cairo_line_to(cr, ix(rect, 0.78), iy(rect, 0.72));
        cairo_line_to(cr, ix(rect, 0.22), iy(rect, 0.72));
        cairo_close_path(cr);
        cairo_stroke(cr);
    } else {
        cairo_arc(cr, ix(rect, 0.50), iy(rect, 0.50), rect.w * 0.30, 0, 2 * ICON_PI);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

void draw_logo(cairo_t *cr, Rect rect, const Theme *theme, double glow) {
    cairo_save(cr);
    cairo_new_path(cr);
    double radius = rect.w * 0.46;
    double cx = rect.x + rect.w / 2.0;
    double cy = rect.y + rect.h / 2.0;
    if (glow > 0) {
        cairo_pattern_t *halo = cairo_pattern_create_radial(cx, cy, 1, cx, cy, radius * 1.8);
        cairo_pattern_add_color_stop_rgba(halo, 0.0, theme->accent.r,
                                          theme->accent.g, theme->accent.b, 0.20 * glow);
        cairo_pattern_add_color_stop_rgba(halo, 1.0, theme->accent.r,
                                          theme->accent.g, theme->accent.b, 0.0);
        cairo_arc(cr, cx, cy, radius * 1.8, 0, 2 * ICON_PI);
        cairo_set_source(cr, halo);
        cairo_fill(cr);
        cairo_pattern_destroy(halo);
    }
    cairo_pattern_t *grad = cairo_pattern_create_linear(rect.x, rect.y,
                                                        rect.x + rect.w,
                                                        rect.y + rect.h);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, theme->accent.r,
                                      theme->accent.g, theme->accent.b, 1.0);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, theme->accent2.r,
                                      theme->accent2.g, theme->accent2.b, 1.0);
    cairo_arc(cr, cx, cy, radius, 0, 2 * ICON_PI);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    cairo_set_line_width(cr, rect.w * 0.055);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    set_color(cr, theme->bg, 0.92);
    cairo_move_to(cr, cx, cy - radius * 0.56);
    cairo_line_to(cr, cx + radius * 0.58, cy + radius * 0.48);
    cairo_line_to(cr, cx - radius * 0.58, cy + radius * 0.48);
    cairo_close_path(cr);
    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy - radius * 0.56);
    cairo_line_to(cr, cx + radius * 0.20, cy + radius * 0.48);
    cairo_stroke(cr);
    cairo_restore(cr);
}
