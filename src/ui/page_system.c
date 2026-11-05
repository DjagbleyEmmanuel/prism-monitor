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
    SYS_CARD_HOST,
    SYS_CARD_UPTIME,
    SYS_CARD_SESSIONS,
    SYS_CARD_SERVICES
} SystemCardKind;

typedef struct {
    App *app;
    SystemCardKind kind;
    HoverInfo hover;
    double shown;
} SystemCard;

typedef struct {
    App *app;
    HoverInfo hover;
} SystemPanel;

typedef struct {
    App *app;
    SystemCard cards[4];
    SystemPanel info;
    SystemPanel users;
    SystemPanel startup;
    SystemPanel services;
} SystemPage;

static int active_services(App *app) {
    int count = 0;
    for (int i = 0; i < app->view->service_count; i++) {
        if (app->view->services[i].active) {
            count++;
        }
    }
    return count;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    SystemCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->accent2;
    const char *icon = "system";
    const char *title = "";
    char value[96] = "--";
    char unit[16] = "";
    char sub[192] = "";
    double target = 0;

    switch (card->kind) {
        case SYS_CARD_HOST:
            accent = t->accent;
            title = "HOST";
            snprintf(value, sizeof(value), "%s",
                     app->view->sys.hostname[0] ? app->view->sys.hostname : "localhost");
            double value_size = strlen(value) > 15 ? 11 : (strlen(value) > 10 ? 15 : 22);
            snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s",
                     app->view->sys.distro[0] ? app->view->sys.distro : "Linux",
                     app->view->sys.arch[0] ? app->view->sys.arch : "unknown");
            (void)value_size;
            break;
        case SYS_CARD_UPTIME: {
            accent = t->green;
            icon = "activity";
            title = "UPTIME";
            char duration[64];
            fmt_uptime(app->view->sys.uptime_s, duration, sizeof(duration));
            snprintf(value, sizeof(value), "%s", duration);
            char boot[32];
            fmt_clock_short(app->view->sys.boot_time, boot, sizeof(boot));
            snprintf(sub, sizeof(sub), "booted at %s  \xc2\xb7  %s",
                     boot, app->view->sys.kernel);
            break;
        }
        case SYS_CARD_SESSIONS: {
            accent = t->cyan;
            icon = "users";
            title = "SESSIONS";
            target = app->view->user_count;
            snprintf(value, sizeof(value), "%d", app->view->user_count);
            if (app->view->user_count > 0) {
                snprintf(sub, sizeof(sub), "%s on %s",
                         app->view->users[0].name[0] ? app->view->users[0].name : "user",
                         app->view->users[0].tty[0] ? app->view->users[0].tty : "console");
            } else {
                snprintf(sub, sizeof(sub), "no interactive sessions");
            }
            break;
        }
        case SYS_CARD_SERVICES: {
            accent = t->purple;
            icon = "services";
            title = "SERVICES";
            int active = active_services(app);
            target = app->view->service_count;
            snprintf(value, sizeof(value), "%d", active);
            snprintf(unit, sizeof(unit), "/ %d", app->view->service_count);
            snprintf(sub, sizeof(sub), "%d startup entries  \xc2\xb7  %d TCP sockets",
                     app->view->startup_count, app->view->tcp.total);
            break;
        }
    }

    draw_card_accent(cr, t, rect, 14, accent, card->hover.active ? 0.9 : 0.3);
    draw_icon(cr, icon, (Rect){16, 14, 14, 14}, accent, 1.6);
    text_draw(cr, layout, title, 37, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    double value_size = 25;
    if (card->kind == SYS_CARD_HOST) {
        value_size = strlen(value) > 15 ? 11 : (strlen(value) > 10 ? 15 : 22);
    } else if (strlen(value) > 8) {
        value_size = 16;
    }
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

static void panel_header(cairo_t *cr, PangoLayout *layout, const Theme *t,
                         int width, const char *icon, const char *title,
                         const char *right, Color color) {
    draw_icon(cr, icon, (Rect){17, 15, 15, 15}, color, 1.7);
    text_draw(cr, layout, title, 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    if (right != NULL) {
        text_draw_ellipsis(cr, layout, right, width - 18, 23, 8.6, PANGO_WEIGHT_BOLD,
                           t->faint, TEXT_RIGHT, width * 0.55);
    }
}

static void info_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    SystemPanel *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    panel_header(cr, layout, t, width, "system", "SYSTEM INFORMATION",
                 app->view->sys.desktop[0] ? app->view->sys.desktop : "desktop", t->accent2);

    const char *labels[] = {
        "Hostname", "Operating system", "Kernel", "Architecture", "Desktop session",
        "Processor", "Cores / threads", "Memory", "Boot time", "Uptime"
    };
    char values[10][256];
    snprintf(values[0], sizeof(values[0]), "%s", app->view->sys.hostname);
    snprintf(values[1], sizeof(values[1]), "%s", app->view->sys.distro);
    snprintf(values[2], sizeof(values[2]), "%s", app->view->sys.kernel);
    snprintf(values[3], sizeof(values[3]), "%s", app->view->sys.arch);
    snprintf(values[4], sizeof(values[4]), "%s", app->view->sys.desktop[0] ? app->view->sys.desktop : "unknown");
    snprintf(values[5], sizeof(values[5]), "%s", app->view->sys.cpu_model);
    snprintf(values[6], sizeof(values[6]), "%d / %d", app->view->sys.cpu_cores,
             app->view->sys.cpu_threads);
    fmt_bytes(app->view->sys.total_mem_mb * 1048576.0, values[7], sizeof(values[7]));
    fmt_clock_short(app->view->sys.boot_time, values[8], sizeof(values[8]));
    fmt_uptime(app->view->sys.uptime_s, values[9], sizeof(values[9]));

    double y = 52;
    for (int i = 0; i < 10; i++) {
        text_draw(cr, layout, labels[i], 18, y, 9.5, PANGO_WEIGHT_NORMAL, t->faint,
                  TEXT_LEFT);
        text_draw_ellipsis(cr, layout, values[i], 190, y, 9.4, PANGO_WEIGHT_MEDIUM,
                           t->text, TEXT_LEFT, width - 208);
        y += 27;
    }
    g_object_unref(layout);
}

static void users_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    SystemPanel *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    char count[32];
    snprintf(count, sizeof(count), "%d active", app->view->user_count);
    panel_header(cr, layout, t, width, "users", "SESSIONS", count, t->cyan);

    double y = 54;
    if (app->view->user_count == 0) {
        text_draw(cr, layout, "No interactive logins", width / 2.0, height / 2.0, 11,
                  PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    for (int i = 0; i < app->view->user_count && i < 7; i++) {
        const UserInfo *user = &app->view->users[i];
        if (i > 0) {
            draw_separator(cr, t, 18, y - 9, width - 18, y - 9);
        }
        char initial[2] = {user->name[0] ? user->name[0] : '?', '\0'};
        for (char *p = initial; *p; p++) {
            if (*p >= 'a' && *p <= 'z') {
                *p = (char)(*p - 32);
            }
        }
        cairo_new_path(cr);
        cairo_arc(cr, 30, y + 4, 13, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, t->cyan.r, t->cyan.g, t->cyan.b, 0.18);
        cairo_fill(cr);
        text_draw(cr, layout, initial, 30, y + 4, 10, PANGO_WEIGHT_BOLD, t->cyan,
                  TEXT_CENTER);
        text_draw_ellipsis(cr, layout, user->name, 52, y - 2, 9.8, PANGO_WEIGHT_BOLD,
                           t->text, TEXT_LEFT, width - 80);
        char meta[128];
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s",
                 user->tty[0] ? user->tty : "session",
                 user->host[0] ? user->host : "local");
        text_draw_ellipsis(cr, layout, meta, 52, y + 14, 8, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, width - 80);
        char since[32];
        fmt_duration((double)(g_get_real_time() / 1000000 - user->login_time),
                     since, sizeof(since));
        text_draw(cr, layout, since, width - 18, y + 5, 8.2, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_RIGHT);
        y += 50;
    }
    g_object_unref(layout);
}

static void startup_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    SystemPanel *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    char count[32];
    snprintf(count, sizeof(count), "%d entries", app->view->startup_count);
    panel_header(cr, layout, t, width, "startup", "STARTUP APPLICATIONS", count,
                 t->orange);

    double y = 52;
    int rows = app->view->startup_count < 9 ? app->view->startup_count : 9;
    for (int i = 0; i < rows; i++) {
        const StartupInfo *item = &app->view->startups[i];
        if (i > 0) {
            draw_separator(cr, t, 18, y - 8, width - 18, y - 8);
        }
        Color color = item->enabled ? t->green : t->faint;
        rounded_rect(cr, (Rect){18, y - 3, 7, 7}, 3.5);
        cairo_set_source_rgba(cr, color.r, color.g, color.b, 1);
        cairo_fill(cr);
        text_draw_ellipsis(cr, layout, item->name, 34, y + 2, 9.4,
                           PANGO_WEIGHT_MEDIUM, t->text, TEXT_LEFT, width - 150);
        text_draw(cr, layout, item->enabled ? "enabled" : "disabled", width - 18,
                  y + 2, 8.3, PANGO_WEIGHT_BOLD, color, TEXT_RIGHT);
        y += 35;
    }
    if (rows == 0) {
        text_draw(cr, layout, "No autostart entries", width / 2.0, height / 2.0, 11,
                  PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static void services_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                          gpointer data) {
    (void)area;
    SystemPanel *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    char count[64];
    snprintf(count, sizeof(count), "%d active / %d", active_services(app),
             app->view->service_count);
    panel_header(cr, layout, t, width, "services", "SYSTEM SERVICES", count,
                 t->purple);

    double y = 52;
    int shown = 0;
    for (int i = 0; i < app->view->service_count && shown < 9; i++) {
        const ServiceInfo *service = &app->view->services[i];
        if (!service->active) {
            continue;
        }
        if (shown > 0) {
            draw_separator(cr, t, 18, y - 8, width - 18, y - 8);
        }
        text_draw_ellipsis(cr, layout, service->name, 18, y + 1, 9.2,
                           PANGO_WEIGHT_MEDIUM, t->text, TEXT_LEFT, width - 120);
        char desc[160];
        snprintf(desc, sizeof(desc), "%s", service->description);
        text_draw_ellipsis(cr, layout, desc, 18, y + 16, 7.9, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, width - 120);
        draw_badge(cr, layout, t, width - 62, y + 7, "active", t->green);
        y += 38;
        shown++;
    }
    if (shown == 0) {
        text_draw(cr, layout, "No systemd services detected", width / 2.0,
                  height / 2.0, 11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, SystemPage *page, SystemCardKind kind,
                            int height) {
    SystemCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_system_create(App *app) {
    SystemPage *page = g_new0(SystemPage, 1);
    page->app = app;
    page->info.app = app;
    page->users.app = app;
    page->startup.app = app;
    page->services.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (SystemCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *top = prism_row();
    GtkWidget *info = prism_area(app, 350, info_draw, &page->info);
    prism_hover_attach(info, &page->info.hover);
    gtk_widget_set_hexpand(info, TRUE);
    gtk_widget_set_size_request(info, 560, 350);
    gtk_box_append(GTK_BOX(top), info);
    GtkWidget *users = prism_area(app, 350, users_draw, &page->users);
    gtk_widget_set_size_request(users, 380, 350);
    gtk_widget_set_hexpand(users, FALSE);
    prism_hover_attach(users, &page->users.hover);
    gtk_box_append(GTK_BOX(top), users);
    gtk_box_append(GTK_BOX(content), top);

    GtkWidget *bottom = prism_row();
    GtkWidget *startup = prism_area(app, 390, startup_draw, &page->startup);
    prism_hover_attach(startup, &page->startup.hover);
    gtk_widget_set_hexpand(startup, TRUE);
    gtk_widget_set_size_request(startup, 460, 390);
    gtk_box_append(GTK_BOX(bottom), startup);
    GtkWidget *services = prism_area(app, 390, services_draw, &page->services);
    prism_hover_attach(services, &page->services.hover);
    gtk_widget_set_hexpand(services, TRUE);
    gtk_widget_set_size_request(services, 460, 390);
    gtk_box_append(GTK_BOX(bottom), services);
    gtk_box_append(GTK_BOX(content), bottom);

    g_object_set_data_full(G_OBJECT(content), "system-page", page, g_free);
    app->pages[PAGE_SYSTEM].root = content;
    return prism_page(content);
}

void page_system_update(App *app) {
    (void)app;
}
