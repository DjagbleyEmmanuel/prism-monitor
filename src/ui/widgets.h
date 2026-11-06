#ifndef PRISM_WIDGETS_H
#define PRISM_WIDGETS_H

#include "ui/app.h"

typedef struct {
    double x;
    double y;
    int active;
} HoverInfo;

void prism_set_a11y(GtkWidget *widget, const char *name, const char *desc);
GtkWidget *prism_area(App *app, int height, GtkDrawingAreaDrawFunc draw,
                      gpointer data);
GtkWidget *prism_card_area(App *app, int height, GtkDrawingAreaDrawFunc draw,
                           gpointer data);
void prism_hover_attach(GtkWidget *area, HoverInfo *hover);
GtkWidget *prism_page(GtkWidget *content);
GtkWidget *prism_column(void);
GtkWidget *prism_row(void);
GtkWidget *prism_grid(void);
void prism_grid_put(GtkGrid *grid, GtkWidget *widget, int col, int row,
                    int width, int height);
GtkWidget *prism_label(const char *text, const char *css_class);
GtkWidget *prism_button(const char *text, const char *css_class);
GtkWidget *prism_title(const char *text, const char *subtitle);
GtkWidget *prism_spacer(void);

#endif
