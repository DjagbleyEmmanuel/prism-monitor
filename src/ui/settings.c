#include "ui/app.h"

#include "core/common.h"
#include "core/format.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    App *app;
    int index;
} Choice;

static void queue_group_redraw(GtkWidget *widget) {
    GtkWidget *group = gtk_widget_get_parent(widget);
    if (group == NULL) {
        gtk_widget_queue_draw(widget);
        return;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(group); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        gtk_widget_queue_draw(child);
    }
}

static GtkWidget *dialog_shell(App *app, const char *title, int width, int height) {
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_transient_for(GTK_WINDOW(window), app->window);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *done = gtk_button_new_with_label("Done");
    gtk_widget_add_css_class(done, "suggested-action");
    g_signal_connect_swapped(done, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), done);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    return window;
}

static void choice_theme(GtkButton *button, gpointer data) {
    Choice *choice = data;
    App *app = choice->app;
    app->config.theme = choice->index;
    app->config.accent = -1;
    app_apply_theme(app);
    app_save_config(app);
    app_notify(app, "Theme: %s", theme_name(choice->index));
    queue_group_redraw(GTK_WIDGET(button));
}

static void choice_accent(GtkGestureClick *gesture, int n_press, double x, double y,
                          gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    Choice *choice = data;
    App *app = choice->app;
    app->config.accent = choice->index;
    app_apply_theme(app);
    app_save_config(app);
    app_notify(app, "Accent: %s", accent_name(choice->index));
    queue_group_redraw(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
}

static void theme_swatch_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                              int height, gpointer data) {
    (void)area;
    Choice *choice = data;
    const Theme *base = theme_base((ThemeId)choice->index);
    double size = width < height ? width : height;
    Rect inner = {(width - size) / 2.0 + 7, (height - size) / 2.0 + 7,
                  size - 14, size - 14};
    rounded_rect(cr, inner, 8);
    cairo_set_source_rgb(cr, base->bg.r, base->bg.g, base->bg.b);
    cairo_fill(cr);
    rounded_rect(cr, (Rect){inner.x + 5, inner.y + 5, inner.w - 10, 8}, 4);
    cairo_set_source_rgb(cr, base->accent.r, base->accent.g, base->accent.b);
    cairo_fill(cr);
    rounded_rect(cr, (Rect){inner.x + 5, inner.y + 17, inner.w - 10, 5}, 2.5);
    cairo_set_source_rgba(cr, base->text.r, base->text.g, base->text.b, 0.45);
    cairo_fill(cr);
    if (choice->app->config.theme == choice->index) {
        rounded_rect(cr, (Rect){1, 1, width - 2, height - 2}, 9);
        cairo_set_source_rgb(cr, choice->app->theme.accent.r,
                             choice->app->theme.accent.g,
                             choice->app->theme.accent.b);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }
}

static void accent_swatch_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                               int height, gpointer data) {
    (void)area;
    Choice *choice = data;
    Color c = accent_color(choice->index);
    double cx = width / 2.0;
    double cy = height / 2.0;
    double r = (width < height ? width : height) / 2.0 - 5;
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * 3.141592653589793);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_fill(cr);
    if (choice->app->config.accent == choice->index) {
        cairo_arc(cr, cx, cy, r - 3.5, 0, 2 * 3.141592653589793);
        cairo_set_source_rgb(cr, choice->app->theme.bg.r, choice->app->theme.bg.g,
                             choice->app->theme.bg.b);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }
}

static GtkWidget *section(const char *title, const char *description) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *name = prism_label(title, "section-title");
    GtkWidget *desc = prism_label(description, "section-desc");
    gtk_widget_add_css_class(desc, "faint");
    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), desc);
    return box;
}

static void interval_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.interval = gtk_spin_button_get_value(spin);
    sampler_set_interval(app->sampler, app->config.interval);
    app_save_config(app);
}

static void switch_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    App *app = data;
    int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sw), "setting"));
    gboolean active = gtk_switch_get_active(sw);
    switch (which) {
        case 0:
            app->config.animations = active;
            break;
        case 1:
            app->config.kernel_threads = active;
            break;
        case 2:
            app->config.compact = active;
            app_apply_theme(app);
            break;
        case 3:
            app->config.popout = active;
            break;
        case 4:
            app->config.graph_fill = active;
            app_apply_theme(app);
            break;
        case 5:
            app->config.graph_grid = active;
            app_apply_theme(app);
            break;
        case 6:
            app->config.graph_glow = active;
            app_apply_theme(app);
            break;
        case 7:
            app->config.confirm_end = active;
            break;
        case 8:
            app->config.auto_record = active;
            break;
        case 9:
            app->config.column_visibility = active;
            break;
        case 10:
            app->config.alerts_enabled = active;
            break;
        default:
            break;
    }
    app_save_config(app);
    page_processes_update(app);
    app_refresh(app);
}

static GtkWidget *make_switch(App *app, int which, int value) {
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), value != 0);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(sw), "setting", GINT_TO_POINTER(which));
    g_signal_connect(sw, "notify::active", G_CALLBACK(switch_changed), app);
    return sw;
}

static GtkWidget *setting_row(GtkWidget *text, GtkWidget *control) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), control);
    return row;
}

static GtkWidget *make_dropdown(const char *const *items, int selected,
                                GCallback callback, gpointer data) {
    GtkWidget *dropdown = gtk_drop_down_new_from_strings(items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),
                               selected < 0 ? 0 : (guint)selected);
    gtk_widget_set_valign(dropdown, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(dropdown, 210, -1);
    if (callback != NULL) {
        g_signal_connect(dropdown, "notify::selected", callback, data);
    }
    return dropdown;
}

static void temp_unit_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    App *app = data;
    app->config.temp_unit = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    app_save_config(app);
    app_refresh(app);
}

static void language_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    App *app = data;
    app->config.language = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    app_save_config(app);
    app_notify(app, "Language preference saved (applies on next launch)");
}

static void quality_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    App *app = data;
    app->config.quality = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    app_apply_theme(app);
    app_save_config(app);
}

static void fps_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.fps = (int)gtk_spin_button_get_value(spin);
    app_update_tick(app);
    app_save_config(app);
}

static void history_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.history_multiplier = gtk_spin_button_get_value(spin);
    app_save_config(app);
    app_refresh(app);
}

static void pixels_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.pixels_per_update = (int)gtk_spin_button_get_value(spin);
    app_apply_theme(app);
    app_save_config(app);
}

static void smoothing_changed(GtkRange *range, gpointer data) {
    App *app = data;
    app->config.smoothing = gtk_range_get_value(range);
    app_save_config(app);
}

static void font_scale_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.font_scale = gtk_spin_button_get_value(spin);
    app_apply_theme(app);
    app_save_config(app);
}

static void font_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    App *app = data;
    PangoFontDescription *desc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(object));
    if (desc != NULL) {
        const char *family = pango_font_description_get_family(desc);
        if (family != NULL && family[0] != '\0') {
            snprintf(app->config.font_family, sizeof(app->config.font_family),
                     "%s", family);
            app_apply_theme(app);
            app_save_config(app);
        }
    }
}

static void process_interval_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.process_interval = (int)gtk_spin_button_get_value(spin);
    sampler_set_process_interval(app->sampler, app->config.process_interval);
    app_save_config(app);
}

static void alert_temp_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.alert_temp_c = gtk_spin_button_get_value(spin);
    app_save_config(app);
}

static void alert_disk_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.alert_disk_pct = gtk_spin_button_get_value(spin);
    app_save_config(app);
}

static void alert_mem_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.alert_mem_pct = gtk_spin_button_get_value(spin);
    app_save_config(app);
}

static void alert_battery_changed(GtkSpinButton *spin, gpointer data) {
    App *app = data;
    app->config.alert_battery_pct = gtk_spin_button_get_value(spin);
    app_save_config(app);
}

static void reset_confirmed(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    config_defaults(&app->config);
    sampler_set_interval(app->sampler, app->config.interval);
    sampler_set_process_interval(app->sampler, app->config.process_interval);
    app_update_tick(app);
    app_apply_theme(app);
    app_save_config(app);
    if (app->settings_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(app->settings_window));
    }
    app_notify(app, "Settings restored to defaults");
}

static void reset_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    manage_confirm_dialog(app, "Reset settings",
                          "Restore every appearance, sampling and performance "
                          "preference to its default value? This also closes the "
                          "settings window so the new values can be inspected.",
                          "Reset", G_CALLBACK(reset_confirmed), app);
}

static GtkWidget *make_spin(double min, double max, double step, double value,
                            int digits, int width, GCallback callback,
                            gpointer data) {
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
    if (width > 0) {
        gtk_widget_set_size_request(spin, width, -1);
    }
    if (callback != NULL) {
        g_signal_connect(spin, "value-changed", callback, data);
    }
    return spin;
}

static GtkWidget *appearance_card(App *app) {
    GtkWidget *theme_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(theme_card, "settings-card");
    gtk_box_append(GTK_BOX(theme_card),
                   section("Theme", "Choose a palette for every surface and chart."));
    GtkWidget *themes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < theme_count(); i++) {
        Choice *choice = g_new0(Choice, 1);
        choice->app = app;
        choice->index = i;
        GtkWidget *button = gtk_button_new();
        gtk_widget_add_css_class(button, "flat");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *swatch = gtk_drawing_area_new();
        gtk_widget_set_size_request(swatch, 52, 52);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), theme_swatch_draw,
                                       choice, NULL);
        GtkWidget *label = prism_label(theme_name((ThemeId)i), "subtitle");
        gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(box), swatch);
        gtk_box_append(GTK_BOX(box), label);
        gtk_button_set_child(GTK_BUTTON(button), box);
        g_object_set_data_full(G_OBJECT(button), "choice", choice, g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(choice_theme), choice);
        gtk_box_append(GTK_BOX(themes), button);
    }
    gtk_box_append(GTK_BOX(theme_card), themes);

    GtkWidget *accent_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < accent_count(); i++) {
        Choice *choice = g_new0(Choice, 1);
        choice->app = app;
        choice->index = i;
        GtkWidget *swatch = gtk_drawing_area_new();
        gtk_widget_set_size_request(swatch, 32, 32);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), accent_swatch_draw,
                                       choice, NULL);
        g_object_set_data_full(G_OBJECT(swatch), "choice", choice, g_free);
        GtkGesture *click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed", G_CALLBACK(choice_accent), choice);
        gtk_widget_add_controller(swatch, GTK_EVENT_CONTROLLER(click));
        gtk_box_append(GTK_BOX(accent_row), swatch);
    }
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Accent", "Tint controls, highlights and chart strokes."),
                               accent_row));

    PangoFontDescription *font_desc =
        pango_font_description_from_string(app->config.font_family);
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font_button = gtk_font_dialog_button_new(font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_button),
                                         font_desc);
    pango_font_description_free(font_desc);
    gtk_widget_set_size_request(font_button, 210, -1);
    g_signal_connect(font_button, "notify::font-desc", G_CALLBACK(font_changed), app);
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Interface font", "Typeface used by labels, lists and controls."),
                               font_button));

    GtkWidget *font_scale = make_spin(0.75, 1.5, 0.05, app->config.font_scale, 2,
                                      110, G_CALLBACK(font_scale_changed), app);
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Font scale", "Scale interface text without changing density."),
                               font_scale));

    const char *temp_units[] = {"Celsius", "Fahrenheit", NULL};
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Temperature unit", "Applies to every thermal reading."),
                               make_dropdown(temp_units, app->config.temp_unit,
                                             G_CALLBACK(temp_unit_changed), app)));

    const char *languages[] = {"English", "Deutsch", "Español", "Français",
                               "Português", "日本語", NULL};
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Language", "Saved now and applied the next time Prism starts."),
                               make_dropdown(languages, app->config.language,
                                             G_CALLBACK(language_changed), app)));

    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Compact density", "Tighter spacing across cards and lists."),
                               make_switch(app, 2, app->config.compact)));
    gtk_box_append(GTK_BOX(theme_card),
                   setting_row(section("Popout details", "Prefer floating detail windows for management pages."),
                               make_switch(app, 3, app->config.popout)));
    return theme_card;
}

void app_open_settings(App *app) {
    if (app->settings_window != NULL) {
        gtk_window_present(GTK_WINDOW(app->settings_window));
        return;
    }
    GtkWidget *window = dialog_shell(app, "Prism Settings", 760, 840);
    app->settings_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->settings_window);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller),
                                                     TRUE);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(content, 22);
    gtk_widget_set_margin_end(content, 22);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 22);

    gtk_box_append(GTK_BOX(content), appearance_card(app));

    GtkWidget *behavior = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(behavior, "settings-card");
    gtk_box_append(GTK_BOX(behavior), section("Sampling",
        "Control refresh cadence and how much work each sample performs."));

    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Refresh interval", "Seconds between system samples."),
                               make_spin(0.25, 10.0, 0.25, app->config.interval, 2,
                                         110, G_CALLBACK(interval_changed), app)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Process interval", "Seconds between process table scans."),
                               make_spin(1, 10, 1, app->config.process_interval, 0,
                                         110, G_CALLBACK(process_interval_changed), app)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Animated transitions", "Smooth gauge and value easing."),
                               make_switch(app, 0, app->config.animations)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Show kernel threads", "Include kernel tasks in process views."),
                               make_switch(app, 1, app->config.kernel_threads)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Confirm destructive actions", "Ask before ending processes or deleting data."),
                               make_switch(app, 7, app->config.confirm_end)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Auto-start flight recorder", "Begin capturing as soon as Prism starts."),
                               make_switch(app, 8, app->config.auto_record)));
    gtk_box_append(GTK_BOX(behavior),
                   setting_row(section("Remember column layout", "Persist list column visibility between sessions."),
                               make_switch(app, 9, app->config.column_visibility)));
    gtk_box_append(GTK_BOX(content), behavior);

    GtkWidget *alerts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(alerts, "settings-card");
    gtk_box_append(GTK_BOX(alerts), section("Alerts",
        "Toast notification when a threshold is crossed (60s cooldown each)."));
    gtk_box_append(GTK_BOX(alerts),
                   setting_row(section("Enable alerts", "Temperature, disk, memory and battery warnings."),
                               make_switch(app, 10, app->config.alerts_enabled)));
    gtk_box_append(GTK_BOX(alerts),
                   setting_row(section("Temperature limit (°C)", "CPU/GPU package temperature."),
                               make_spin(50, 110, 1, app->config.alert_temp_c, 0,
                                         110, G_CALLBACK(alert_temp_changed), app)));
    gtk_box_append(GTK_BOX(alerts),
                   setting_row(section("Disk full limit (%)", "Fullest mount by used percent."),
                               make_spin(50, 100, 1, app->config.alert_disk_pct, 0,
                                         110, G_CALLBACK(alert_disk_changed), app)));
    gtk_box_append(GTK_BOX(alerts),
                   setting_row(section("Memory limit (%)", "Physical memory used percent."),
                               make_spin(50, 100, 1, app->config.alert_mem_pct, 0,
                                         110, G_CALLBACK(alert_mem_changed), app)));
    gtk_box_append(GTK_BOX(alerts),
                   setting_row(section("Battery low limit (%)", "Warn at or below this charge."),
                               make_spin(5, 60, 1, app->config.alert_battery_pct, 0,
                                         110, G_CALLBACK(alert_battery_changed), app)));
    gtk_box_append(GTK_BOX(content), alerts);

    GtkWidget *graphics = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(graphics, "settings-card");
    gtk_box_append(GTK_BOX(graphics), section("Graphics & Performance",
        "Trade detail for lower CPU and GPU usage."));

    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Animation frame rate", "Frames per second for eased widgets."),
                               make_spin(10, 60, 1, app->config.fps, 0, 110,
                                         G_CALLBACK(fps_changed), app)));
    const char *quality[] = {"Balanced", "High detail", "Performance", "Battery saver", NULL};
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Visual quality", "Controls chart glow, shadows and line detail."),
                               make_dropdown(quality, app->config.quality,
                                             G_CALLBACK(quality_changed), app)));
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("History window", "Multiplier for visible chart history."),
                               make_spin(0.25, 4.0, 0.25, app->config.history_multiplier, 2,
                                         110, G_CALLBACK(history_changed), app)));
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Pixels per update", "Higher values draw smoother, slower chart lines."),
                               make_spin(1, 8, 1, app->config.pixels_per_update, 0, 110,
                                         G_CALLBACK(pixels_changed), app)));

    GtkWidget *smoothing = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                    0.0, 0.9, 0.05);
    gtk_range_set_value(GTK_RANGE(smoothing), app->config.smoothing);
    gtk_scale_set_draw_value(GTK_SCALE(smoothing), FALSE);
    gtk_widget_set_size_request(smoothing, 210, -1);
    g_signal_connect(smoothing, "value-changed", G_CALLBACK(smoothing_changed), app);
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Smoothing", "How quickly gauges settle on new values."),
                               smoothing));

    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Graph gradient fill", "Shade the area below chart lines."),
                               make_switch(app, 4, app->config.graph_fill)));
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Chart grid", "Draw horizontal guide lines behind data."),
                               make_switch(app, 5, app->config.graph_grid)));
    gtk_box_append(GTK_BOX(graphics),
                   setting_row(section("Chart glow", "Soften chart strokes with a low-alpha halo."),
                               make_switch(app, 6, app->config.graph_glow)));
    gtk_box_append(GTK_BOX(content), graphics);

    GtkWidget *storage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(storage, "settings-card");
    gtk_box_append(GTK_BOX(storage), section("Configuration",
        "Preferences are written atomically to disk."));
    char path[512];
    snprintf(path, sizeof(path), "%s", config_path());
    GtkWidget *path_label = prism_label(path, "dim");
    gtk_label_set_selectable(GTK_LABEL(path_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(path_label), TRUE);
    gtk_box_append(GTK_BOX(storage), path_label);
    GtkWidget *reset_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(reset_row), prism_spacer());
    GtkWidget *reset = prism_button("Restore defaults", "danger");
    g_signal_connect(reset, "clicked", G_CALLBACK(reset_clicked), app);
    gtk_box_append(GTK_BOX(reset_row), reset);
    gtk_box_append(GTK_BOX(storage), reset_row);
    gtk_box_append(GTK_BOX(content), storage);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
    gtk_window_set_child(GTK_WINDOW(window), scroller);
    gtk_window_present(GTK_WINDOW(window));
}

void app_open_appearance(App *app) {
    if (app->appearance_window != NULL) {
        gtk_window_present(GTK_WINDOW(app->appearance_window));
        return;
    }
    GtkWidget *window = dialog_shell(app, "Appearance", 720, 780);
    app->appearance_window = window;
    g_object_add_weak_pointer(G_OBJECT(window),
                              (gpointer *)&app->appearance_window);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller),
                                                     TRUE);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(content, 22);
    gtk_widget_set_margin_end(content, 22);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 22);
    gtk_box_append(GTK_BOX(content), appearance_card(app));
    GtkWidget *hint = prism_label(
        "Theme, accent and density changes apply instantly and are saved to "
        "your preferences.", "faint");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(content), hint);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
    gtk_window_set_child(GTK_WINDOW(window), scroller);
    gtk_window_present(GTK_WINDOW(window));
}

static GtkWidget *shortcut_row(GtkGrid *grid, int row, const char *keys,
                               const char *description) {
    GtkWidget *key = prism_label(keys, "keycap");
    GtkWidget *label = prism_label(description, "dim");
    gtk_widget_set_halign(key, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(grid, key, 0, row, 1, 1);
    gtk_grid_attach(grid, label, 1, row, 1, 1);
    return key;
}

void app_show_shortcuts(App *app) {
    if (app->shortcuts_window != NULL) {
        gtk_window_present(GTK_WINDOW(app->shortcuts_window));
        return;
    }
    GtkWidget *window = dialog_shell(app, "Keyboard Shortcuts", 580, 650);
    app->shortcuts_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->shortcuts_window);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(content, 26);
    gtk_widget_set_margin_end(content, 26);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);

    GtkWidget *heading = prism_title("Move faster", "Navigate Prism without leaving the keyboard.");
    gtk_box_append(GTK_BOX(content), heading);

    const char *sections[] = {"Navigation", "Workspace", "Processes"};
    const char *keys[][8] = {
        {"Ctrl + 1 ... 9", "Ctrl + Tab", "Ctrl + Shift + Tab", "Ctrl + F", "Esc", NULL},
        {"Ctrl + ,", "Ctrl + Q", "F5", "Ctrl + B", "Ctrl + K", "Ctrl + P", NULL},
        {"Ctrl + F", "Enter", "Delete", "Ctrl + E", NULL, NULL, NULL},
    };
    const char *descriptions[][8] = {
        {"Open a page directly", "Next page", "Previous page", "Focus process search",
         "Clear the current filter", NULL},
        {"Open settings", "Quit Prism Monitor", "Show sampling status",
         "Collapse or expand the sidebar", "Command palette", "Pause / resume sampling", NULL},
        {"Search by name, user or PID", "Open the selected process", "End the selected process",
         "Export snapshot JSON", NULL, NULL, NULL},
    };
    for (int s = 0; s < 3; s++) {
        GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_add_css_class(card, "settings-card");
        gtk_box_append(GTK_BOX(card), section(sections[s], ""));
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 22);
        int row = 0;
        for (int i = 0; i < 8 && keys[s][i] != NULL; i++) {
            shortcut_row(GTK_GRID(grid), row++, keys[s][i], descriptions[s][i]);
        }
        gtk_box_append(GTK_BOX(card), grid);
        gtk_box_append(GTK_BOX(content), card);
    }
    gtk_window_set_child(GTK_WINDOW(window), content);
    gtk_window_present(GTK_WINDOW(window));
}

static void about_logo_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                            gpointer data) {
    (void)area;
    App *app = data;
    draw_logo(cr, (Rect){4, 4, width - 8, height - 8}, &app->theme, 1.0);
}

void app_show_about(App *app) {
    GtkWidget *window = dialog_shell(app, "About Prism Monitor", 500, 560);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 32);
    gtk_widget_set_margin_end(content, 32);
    gtk_widget_set_margin_top(content, 28);
    gtk_widget_set_margin_bottom(content, 28);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);

    GtkWidget *logo = gtk_drawing_area_new();
    gtk_widget_set_size_request(logo, 84, 84);
    gtk_widget_set_halign(logo, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(logo), about_logo_draw, app, NULL);
    gtk_box_append(GTK_BOX(content), logo);

    GtkWidget *name = prism_label("PRISM MONITOR", "brand");
    gtk_widget_set_halign(name, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(name, "accent");
    gtk_box_append(GTK_BOX(content), name);

    char version[64];
    snprintf(version, sizeof(version), "Version %s  \xc2\xb7  GTK 4 / Cairo", PRISM_VERSION);
    GtkWidget *sub = prism_label(version, "dim");
    gtk_widget_set_halign(sub, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), sub);

    GtkWidget *description = prism_label(
        "A fast, native system monitor for Linux. Live CPU, memory, GPU, storage, "
        "network, process, sensor and host telemetry rendered with custom Cairo "
        "visualizations.",
        "dim");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_justify(GTK_LABEL(description), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(description, 10);
    gtk_box_append(GTK_BOX(content), description);

    GtkWidget *grid = gtk_grid_new();
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    const char *labels[] = {"Host", "Kernel", "Processor", "Memory",
                            "Maintainer", "License"};
    char host[128], kernel[128], cpu[128], mem[64];
    snprintf(host, sizeof(host), "%s", app->view->sys.hostname);
    snprintf(kernel, sizeof(kernel), "%s", app->view->sys.kernel);
    snprintf(cpu, sizeof(cpu), "%s", app->view->sys.cpu_model);
    fmt_bytes(app->view->mem.total_mb * 1048576.0, mem, sizeof(mem));
    const char *values[] = {host, kernel, cpu, mem, PRISM_MAINTAINER, "MIT"};
    for (int i = 0; i < 6; i++) {
        GtkWidget *key = prism_label(labels[i], "faint");
        GtkWidget *value = prism_label(values[i], "dim");
        gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(value, TRUE);
        gtk_grid_attach(GTK_GRID(grid), key, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), value, 1, i, 1, 1);
    }
    gtk_widget_set_margin_top(grid, 12);
    gtk_box_append(GTK_BOX(content), grid);

    GtkWidget *footer = prism_label(
        "\xc2\xa9 2026 " PRISM_MAINTAINER
        "  \xc2\xb7  Built with C, GTK4, Cairo and Pango.", "faint");
    gtk_widget_set_halign(footer, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(footer, 14);
    gtk_box_append(GTK_BOX(content), footer);

    gtk_window_set_child(GTK_WINDOW(window), content);
    gtk_window_present(GTK_WINDOW(window));
}
