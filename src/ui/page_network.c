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
    NET_CARD_RX,
    NET_CARD_TX,
    NET_CARD_LINKS,
    NET_CARD_SOCKETS
} NetCardKind;

typedef struct {
    App *app;
    NetCardKind kind;
    HoverInfo hover;
    double shown;
} NetCard;

typedef struct {
    App *app;
    HoverInfo hover;
} NetChart;

typedef struct {
    App *app;
    HoverInfo hover;
} NetList;

typedef struct {
    App *app;
    NetCard cards[4];
    NetChart chart;
    NetList interfaces;
    NetList sockets;
} NetworkPage;

static int up_links(App *app) {
    int count = 0;
    for (int i = 0; i < app->view->nic_count; i++) {
        if (strcmp(app->view->nics[i].state, "up") == 0) {
            count++;
        }
    }
    return count;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    NetCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->blue;
    const char *icon = "network";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[160] = "";
    double target = 0;

    switch (card->kind) {
        case NET_CARD_RX: {
            title = "DOWNLOAD";
            double rate = app->view->net_rx_bps;
            target = rate;
            char parts[16];
            fmt_rate_parts(rate, value, sizeof(value), parts, sizeof(parts));
            snprintf(unit, sizeof(unit), "%s", parts);
            unsigned long long total = 0;
            for (int i = 0; i < app->view->nic_count; i++) {
                total += app->view->nics[i].rx_total;
            }
            char total_text[32];
            fmt_bytes((double)total, total_text, sizeof(total_text));
            snprintf(sub, sizeof(sub), "%s received  \xc2\xb7  %d links", total_text,
                     up_links(app));
            break;
        }
        case NET_CARD_TX: {
            accent = t->cyan;
            title = "UPLOAD";
            double rate = app->view->net_tx_bps;
            target = rate;
            char parts[16];
            fmt_rate_parts(rate, value, sizeof(value), parts, sizeof(parts));
            snprintf(unit, sizeof(unit), "%s", parts);
            unsigned long long total = 0;
            for (int i = 0; i < app->view->nic_count; i++) {
                total += app->view->nics[i].tx_total;
            }
            char total_text[32];
            fmt_bytes((double)total, total_text, sizeof(total_text));
            snprintf(sub, sizeof(sub), "%s transmitted  \xc2\xb7  %d links", total_text,
                     up_links(app));
            break;
        }
        case NET_CARD_LINKS: {
            accent = t->green;
            title = "ACTIVE LINKS";
            target = up_links(app);
            snprintf(value, sizeof(value), "%d", up_links(app));
            int wireless = 0;
            for (int i = 0; i < app->view->nic_count; i++) {
                if (app->view->nics[i].wireless) {
                    wireless++;
                }
            }
            snprintf(sub, sizeof(sub), "%d interfaces  \xc2\xb7  %d wireless",
                     app->view->nic_count, wireless);
            break;
        }
        case NET_CARD_SOCKETS: {
            accent = t->purple;
            icon = "connections";
            title = "TCP SOCKETS";
            target = app->view->tcp.total;
            snprintf(value, sizeof(value), "%d", app->view->tcp.total);
            snprintf(sub, sizeof(sub), "%d established  \xc2\xb7  %d listening",
                     app->view->tcp.established, app->view->tcp.listen);
            break;
        }
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
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

static void chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    NetChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "network", (Rect){17, 15, 15, 15}, t->blue, 1.7);
    text_draw(cr, layout, "NETWORK TRAFFIC", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "receive  \xc2\xb7  transmit", 39, 40, 8.5,
              PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT);
    char rx[32], tx[32];
    fmt_rate(app->view->net_rx_bps, rx, sizeof(rx));
    fmt_rate(app->view->net_tx_bps, tx, sizeof(tx));
    char current[128];
    snprintf(current, sizeof(current), "\xe2\x86\x93 %s   \xe2\x86\x91 %s", rx, tx);
    text_draw(cr, layout, current, width - 18, 25, 11, PANGO_WEIGHT_BOLD,
              t->text, TEXT_RIGHT);

    ChartSeries series[] = {
        {app->history.net_rx, t->blue, "Receive"},
        {app->history.net_tx, t->cyan, "Transmit"},
    };
    double lx = 18;
    for (int i = 0; i < 2; i++) {
        cairo_new_path(cr);
        cairo_arc(cr, lx + 4, 63, 3.2, 0, 2 * 3.141592653589793);
        cairo_set_source_rgba(cr, series[i].color.r, series[i].color.g,
                              series[i].color.b, 1);
        cairo_fill(cr);
        text_draw(cr, layout, series[i].label, lx + 12, 63, 8.7,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        lx += 16 + text_width(cr, layout, series[i].label, 8.7, PANGO_WEIGHT_NORMAL);
    }
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 76, width - 28, height - 88},
        .series = series,
        .series_count = 2,
        .format = CHART_RATE,
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

static void interfaces_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                            gpointer data) {
    (void)area;
    NetList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "network", (Rect){17, 15, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "INTERFACES", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim,
              TEXT_LEFT);
    text_draw(cr, layout, "RATE IN / OUT", width - 126, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);
    text_draw(cr, layout, "TOTAL", width - 18, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);

    double y = 46;
    int rows = app->view->nic_count < 5 ? app->view->nic_count : 5;
    for (int i = 0; i < rows; i++) {
        const NicInfo *nic = &app->view->nics[i];
        gboolean up = strcmp(nic->state, "up") == 0;
        Color color = up ? (nic->wireless ? t->cyan : t->blue) : t->faint;
        if (i > 0) {
            draw_separator(cr, t, 18, y - 6, width - 18, y - 6);
        }
        text_draw(cr, layout, nic->name, 18, y + 8, 10.5, PANGO_WEIGHT_BOLD,
                  t->text, TEXT_LEFT);
        char state_text[64];
        snprintf(state_text, sizeof(state_text), "%s  \xc2\xb7  %s  \xc2\xb7  %s",
                 nic->state, nic->wireless ? "wireless" : "wired",
                 nic->ipv4[0] ? nic->ipv4 : "no IPv4");
        text_draw_ellipsis(cr, layout, state_text, 18, y + 24, 8.4,
                           PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT, width - 320);
        char rates[96];
        char r[32], tx[32];
        fmt_rate(nic->rx_bps, r, sizeof(r));
        fmt_rate(nic->tx_bps, tx, sizeof(tx));
        snprintf(rates, sizeof(rates), "\xe2\x86\x93 %s  \xe2\x86\x91 %s", r, tx);
        text_draw(cr, layout, rates, width - 126, y + 14, 8.8, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        char total[32];
        fmt_bytes((double)(nic->rx_total + nic->tx_total), total, sizeof(total));
        text_draw(cr, layout, total, width - 18, y + 14, 8.8, PANGO_WEIGHT_NORMAL,
                  t->dim, TEXT_RIGHT);
        y += 44;
    }
    if (rows == 0) {
        text_draw(cr, layout, "No network interfaces detected", width / 2.0,
                  height / 2.0, 11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static void sockets_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    NetList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "connections", (Rect){17, 15, 15, 15}, t->purple, 1.7);
    text_draw(cr, layout, "TCP STATES", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim,
              TEXT_LEFT);
    char total[32];
    snprintf(total, sizeof(total), "%d", app->view->tcp.total);
    text_draw(cr, layout, total, width - 18, 23, 14, PANGO_WEIGHT_BOLD, t->purple,
              TEXT_RIGHT);

    const char *labels[] = {"ESTABLISHED", "LISTEN", "TIME WAIT", "CLOSE WAIT"};
    int values[] = {app->view->tcp.established, app->view->tcp.listen,
                    app->view->tcp.time_wait, app->view->tcp.close_wait};
    Color colors[] = {t->green, t->blue, t->yellow, t->orange};
    double y = 56;
    for (int i = 0; i < 4; i++) {
        text_draw(cr, layout, labels[i], 18, y, 9, PANGO_WEIGHT_BOLD,
                  t->dim, TEXT_LEFT);
        char count[32];
        snprintf(count, sizeof(count), "%d", values[i]);
        text_draw(cr, layout, count, width - 18, y, 10, PANGO_WEIGHT_BOLD,
                  colors[i], TEXT_RIGHT);
        double pct = app->view->tcp.total > 0 ?
                     values[i] * 100.0 / app->view->tcp.total : 0;
        draw_progress(cr, t, (Rect){18, y + 10, width - 36, 5}, pct, colors[i], 2.5);
        y += 40;
    }
    char tcp_info[128];
    snprintf(tcp_info, sizeof(tcp_info), "%d total descriptors", app->view->tcp.total);
    text_draw(cr, layout, tcp_info, 18, height - 18, 8.6, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_LEFT);
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, NetworkPage *page, NetCardKind kind,
                            int height) {
    NetCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_network_create(App *app) {
    NetworkPage *page = g_new0(NetworkPage, 1);
    page->app = app;
    page->chart.app = app;
    page->interfaces.app = app;
    page->sockets.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (NetCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *chart = prism_area(app, 300, chart_draw, &page->chart);
    prism_hover_attach(chart, &page->chart.hover);
    gtk_widget_set_hexpand(chart, TRUE);
    gtk_widget_set_size_request(chart, 540, 300);
    gtk_box_append(GTK_BOX(row), chart);
    GtkWidget *sockets = prism_area(app, 300, sockets_draw, &page->sockets);
    gtk_widget_set_size_request(sockets, 360, 300);
    gtk_widget_set_hexpand(sockets, FALSE);
    prism_hover_attach(sockets, &page->sockets.hover);
    gtk_box_append(GTK_BOX(row), sockets);
    gtk_box_append(GTK_BOX(content), row);

    GtkWidget *interfaces = prism_area(app, 240, interfaces_draw, &page->interfaces);
    prism_hover_attach(interfaces, &page->interfaces.hover);
    gtk_box_append(GTK_BOX(content), interfaces);

    g_object_set_data_full(G_OBJECT(content), "network-page", page, g_free);
    app->pages[PAGE_NETWORK].root = content;
    return prism_page(content);
}

void page_network_update(App *app) {
    (void)app;
}
