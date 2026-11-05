#ifndef PRISM_ICONS_H
#define PRISM_ICONS_H

#include "ui/draw.h"

void draw_icon(cairo_t *cr, const char *name, Rect rect, Color color,
               double line_width);
void draw_logo(cairo_t *cr, Rect rect, const Theme *theme, double glow);

#endif
