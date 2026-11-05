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
    MEM_CARD_USED,
    MEM_CARD_AVAILABLE,
    MEM_CARD_CACHE,
    MEM_CARD_SWAP
} MemCardKind;

typedef struct {
    App *app;
    MemCardKind kind;
    HoverInfo hover;
    double shown;
} MemCard;

typedef struct {
    App *app;
    HoverInfo hover;
} MemChart;

typedef struct {
    App *app;
    HoverInfo hover;
} MemComposition;

typedef struct {
    App *app;
    MemCard cards[4];
    MemChart chart;
    MemComposition composition;
} MemoryPage;

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    MemCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->purple;
    const char *icon = "memory";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[160] = "";
    double target = 0;

    switch (card->kind) {
        case MEM_CARD_USED: {
            accent = t->purple;
            title = "MEMORY USED";
            target = app->view->mem.used_pct;
            snprintf(value, sizeof(value), "%.1f", card->shown);
            snprintf(unit, sizeof(unit), "%%");
            char used[32], total[32];
            fmt_bytes(app->view->mem.used_mb * 1048576.0, used, sizeof(used));
            fmt_bytes(app->view->mem.total_mb * 1048576.0, total, sizeof(total));
            snprintf(sub, sizeof(sub), "%s of %s", used, total);
            break;
        }
        case MEM_CARD_AVAILABLE: {
            accent = t->green;
            icon = "activity";
            title = "AVAILABLE";
            target = app->view->mem.available_mb;
            fmt_bytes(app->view->mem.available_mb * 1048576.0, value, sizeof(value));
            double pct = app->view->mem.total_mb > 0 ?
                         app->view->mem.available_mb / app->view->mem.total_mb * 100.0 : 0;
            snprintf(sub, sizeof(sub), "%.0f%% ready for allocation", pct);
            break;
        }
        case MEM_CARD_CACHE: {
            accent = t->cyan;
            icon = "storage";
            title = "CACHE + BUFFERS";
            target = app->view->mem.cached_mb;
            char cache[32], buffers[32];
            fmt_bytes(app->view->mem.cached_mb * 1048576.0, cache, sizeof(cache));
            fmt_bytes(app->view->mem.buffers_mb * 1048576.0, buffers, sizeof(buffers));
            snprintf(value, sizeof(value), "%s", cache);
            snprintf(sub, sizeof(sub), "%s buffers  \xc2\xb7  reclaimable", buffers);
            break;
        }
        case MEM_CARD_SWAP: {
            accent = app->view->mem.swap_pct > 60 ? t->orange : t->yellow;
            icon = "disk";
            title = "SWAP USED";
            target = app->view->mem.swap_pct;
            snprintf(value, sizeof(value), "%.1f", card->shown);
            snprintf(unit, sizeof(unit), "%%");
            char used[32], total[32];
            fmt_bytes(app->view->mem.swap_used_mb * 1048576.0, used, sizeof(used));
            fmt_bytes(app->view->mem.swap_total_mb * 1048576.0, total, sizeof(total));
            if (app->view->mem.swap_total_mb <= 0) {
                snprintf(sub, sizeof(sub), "swap disabled");
            } else {
                snprintf(sub, sizeof(sub), "%s of %s", used, total);
            }
            break;
        }
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
    }
    if (card->kind == MEM_CARD_USED || card->kind == MEM_CARD_SWAP) {
        snprintf(value, sizeof(value), "%.1f", card->shown);
    }

    draw_card_accent(cr, t, rect, 14, accent, card->hover.active ? 0.9 : 0.3);
    draw_icon(cr, icon, (Rect){16, 14, 14, 14}, accent, 1.6);
    text_draw(cr, layout, title, 37, 22, 10.5, PANGO_WEIGHT_BOLD, t->dim, TEXT_LEFT);
    double value_size = 25;
    if (strlen(value) > 8) {
        value_size = 18;
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

static void chart_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    MemChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "memory", (Rect){17, 15, 15, 15}, t->purple, 1.7);
    text_draw(cr, layout, "MEMORY OVER TIME", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "used  \xc2\xb7  cache  \xc2\xb7  swap", 39, 40, 8.5,
              PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT);
    char used[32];
    fmt_bytes(app->view->mem.used_mb * 1048576.0, used, sizeof(used));
    text_draw(cr, layout, used, width - 18, 25, 23, PANGO_WEIGHT_BOLD, t->purple,
              TEXT_RIGHT);
    char swap_total[64];
    fmt_bytes(app->view->mem.swap_total_mb * 1048576.0, swap_total, sizeof(swap_total));
    char sub[96];
    snprintf(sub, sizeof(sub), "%d%% swap capacity  \xc2\xb7  %s",
             (int)(app->view->mem.swap_pct + 0.5), swap_total);
    text_draw(cr, layout, sub, width - 18, 47, 8.7, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_RIGHT);

    ChartSeries series[] = {
        {app->history.mem_used, t->purple, "Used"},
        {app->history.mem_cache, t->cyan, "Cache"},
        {app->history.swap, t->yellow, "Swap"},
    };
    double lx = 18;
    for (int i = 0; i < 3; i++) {
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
        .series_count = 3,
        .ymax = app->view->mem.total_mb,
        .format = CHART_MB,
        .seconds_per_sample = app->config.interval,
        .window = app_chart_window(app),
        .stacked = 1,
        .hover = state->hover.active,
        .hover_x = state->hover.x,
        .hover_y = state->hover.y,
    };
    draw_chart(cr, layout, t, &spec);
    g_object_unref(layout);
}

static void composition_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                             gpointer data) {
    (void)area;
    MemComposition *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "activity", (Rect){17, 15, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "COMPOSITION", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);

    double donut_radius = width < height ? (width * 0.22) : (height * 0.25);
    if (donut_radius < 52) {
        donut_radius = 52;
    }
    if (donut_radius > 78) {
        donut_radius = 78;
    }
    double cx = 24 + donut_radius;
    double cy = 58 + donut_radius;
    DonutSegment segments[] = {
        {app->view->mem.used_mb, t->purple, "Used"},
        {app->view->mem.cached_mb + app->view->mem.buffers_mb, t->cyan, "Cache"},
        {app->view->mem.available_mb, t->green, "Free"},
    };
    char center[32], center_label[32];
    snprintf(center, sizeof(center), "%.0f%%", app->view->mem.used_pct);
    fmt_bytes(app->view->mem.total_mb * 1048576.0, center_label, sizeof(center_label));
    draw_donut(cr, layout, t, cx, cy, donut_radius, 13, segments, 3,
               center, center_label);

    double legend_x = cx + donut_radius + 32;
    double legend_y = cy - 40;
    const char *names[] = {"Used", "Cache", "Available"};
    double values[] = {app->view->mem.used_mb,
                       app->view->mem.cached_mb + app->view->mem.buffers_mb,
                       app->view->mem.available_mb};
    Color colors[] = {t->purple, t->cyan, t->green};
    for (int i = 0; i < 3; i++) {
        rounded_rect(cr, (Rect){legend_x, legend_y - 4, 8, 8}, 2);
        cairo_set_source_rgb(cr, colors[i].r, colors[i].g, colors[i].b);
        cairo_fill(cr);
        text_draw(cr, layout, names[i], legend_x + 15, legend_y, 9.5,
                  PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT);
        char bytes[32];
        fmt_bytes(values[i] * 1048576.0, bytes, sizeof(bytes));
        text_draw(cr, layout, bytes, width - 18, legend_y, 9.5,
                  PANGO_WEIGHT_BOLD, t->text, TEXT_RIGHT);
        legend_y += 26;
    }

    double bottom = cy + donut_radius + 18;
    if (bottom > height - 32) {
        bottom = height - 32;
    }
    draw_separator(cr, t, 18, bottom, width - 18, bottom);
    char detail[128];
    snprintf(detail, sizeof(detail), "shared %s   \xc2\xb7   slab %s   \xc2\xb7   dirty %s",
             "", "", "");
    char shared[32], slab[32], dirty[32];
    fmt_bytes(app->view->mem.shared_mb * 1048576.0, shared, sizeof(shared));
    fmt_bytes(app->view->mem.slab_mb * 1048576.0, slab, sizeof(slab));
    fmt_bytes(app->view->mem.dirty_mb * 1048576.0, dirty, sizeof(dirty));
    snprintf(detail, sizeof(detail), "shared %s   \xc2\xb7   slab %s   \xc2\xb7   dirty %s",
             shared, slab, dirty);
    text_draw_ellipsis(cr, layout, detail, 18, bottom + 16, 8.8,
                       PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT, width - 36);
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, MemoryPage *page, MemCardKind kind,
                            int height) {
    MemCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_memory_create(App *app) {
    MemoryPage *page = g_new0(MemoryPage, 1);
    page->app = app;
    page->chart.app = app;
    page->composition.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (MemCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *chart = prism_area(app, 320, chart_draw, &page->chart);
    prism_hover_attach(chart, &page->chart.hover);
    gtk_widget_set_hexpand(chart, TRUE);
    gtk_widget_set_size_request(chart, 520, 320);
    gtk_box_append(GTK_BOX(row), chart);
    GtkWidget *composition = prism_area(app, 320, composition_draw, &page->composition);
    gtk_widget_set_size_request(composition, 400, 320);
    gtk_widget_set_hexpand(composition, FALSE);
    prism_hover_attach(composition, &page->composition.hover);
    gtk_box_append(GTK_BOX(row), composition);
    gtk_box_append(GTK_BOX(content), row);

    g_object_set_data_full(G_OBJECT(content), "memory-page", page, g_free);
    app->pages[PAGE_MEMORY].root = content;
    return prism_page(content);
}

void page_memory_update(App *app) {
    (void)app;
}
