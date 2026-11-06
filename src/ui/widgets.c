#include "ui/widgets.h"

#include <glib.h>

typedef struct {
    GtkDrawingAreaDrawFunc fn;
    gpointer data;
} AreaWrap;

static void area_wrap_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
    AreaWrap *wrap = data;
    wrap->fn(area, cr, width, height, wrap->data);
}

static void hover_motion(GtkEventControllerMotion *controller, double x, double y,
                         gpointer data) {
    (void)controller;
    HoverInfo *hover = data;
    hover->x = x;
    hover->y = y;
    hover->active = 1;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    gtk_widget_queue_draw(widget);
}

static void hover_leave(GtkEventControllerMotion *controller, gpointer data) {
    (void)controller;
    HoverInfo *hover = data;
    hover->active = 0;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    gtk_widget_queue_draw(widget);
}

void prism_hover_attach(GtkWidget *area, HoverInfo *hover) {
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(hover_motion), hover);
    g_signal_connect(motion, "leave", G_CALLBACK(hover_leave), hover);
    gtk_widget_add_controller(area, motion);
}

void prism_set_a11y(GtkWidget *widget, const char *name, const char *desc) {
    if (widget == NULL) {
        return;
    }
    GtkAccessible *accessible = GTK_ACCESSIBLE(widget);
    if (accessible != NULL) {
        gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       name != NULL ? name : "Prism widget", -1);
        if (desc != NULL) {
            gtk_accessible_update_property(accessible,
                                           GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                           desc, -1);
        }
    }
    gtk_widget_set_focusable(widget, TRUE);
}

GtkWidget *prism_area(App *app, int height, GtkDrawingAreaDrawFunc draw,
                      gpointer data) {
    GtkWidget *area = gtk_drawing_area_new();
    prism_set_a11y(area, "Prism chart", "Live telemetry chart. Values are also shown as text in status bar.");
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_size_request(area, 80, height);
    AreaWrap *wrap = g_new0(AreaWrap, 1);
    wrap->fn = draw;
    wrap->data = data;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), area_wrap_draw, wrap,
                                   g_free);
    app_track_draw(app, area);
    return area;
}

GtkWidget *prism_card_area(App *app, int height, GtkDrawingAreaDrawFunc draw,
                           gpointer data) {
    GtkWidget *area = prism_area(app, height, draw, data);
    gtk_widget_set_vexpand(area, FALSE);
    gtk_widget_set_valign(area, GTK_ALIGN_START);
    return area;
}

GtkWidget *prism_page(GtkWidget *content) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
    return scroller;
}

GtkWidget *prism_column(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_hexpand(box, TRUE);
    return box;
}

GtkWidget *prism_row(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(box, TRUE);
    return box;
}

GtkWidget *prism_grid(void) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_hexpand(grid, TRUE);
    return grid;
}

void prism_grid_put(GtkGrid *grid, GtkWidget *widget, int col, int row,
                    int width, int height) {
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    gtk_grid_attach(grid, widget, col, row, width, height);
}

GtkWidget *prism_label(const char *text, const char *css_class) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (css_class != NULL) {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

GtkWidget *prism_button(const char *text, const char *css_class) {
    GtkWidget *button = gtk_button_new_with_label(text);
    if (css_class != NULL) {
        gtk_widget_add_css_class(button, css_class);
    }
    return button;
}

GtkWidget *prism_title(const char *text, const char *subtitle) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = prism_label(text, "title");
    gtk_widget_add_css_class(title, "accent");
    GtkWidget *sub = prism_label(subtitle, "dim");
    gtk_widget_add_css_class(sub, "subtitle");
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), sub);
    return box;
}

GtkWidget *prism_spacer(void) {
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    return spacer;
}
