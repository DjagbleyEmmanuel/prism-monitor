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
    GPU_CARD_UTIL,
    GPU_CARD_VRAM,
    GPU_CARD_TEMP,
    GPU_CARD_POWER
} GpuCardKind;

typedef struct {
    App *app;
    GpuCardKind kind;
    HoverInfo hover;
    double shown;
} GpuCard;

typedef struct {
    App *app;
    HoverInfo hover;
} GpuChart;

typedef struct {
    App *app;
    HoverInfo hover;
} GpuList;

typedef struct {
    App *app;
    GpuCard cards[4];
    GpuChart chart;
    GpuList list;
} GpuPage;

static const GpuInfo *best_gpu(App *app) {
    for (int i = 0; i < app->view->gpu_count; i++) {
        if (app->view->gpus[i].present &&
            strcmp(app->view->gpus[i].kind, "nvidia") == 0) {
            return &app->view->gpus[i];
        }
    }
    for (int i = 0; i < app->view->gpu_count; i++) {
        if (app->view->gpus[i].present) {
            return &app->view->gpus[i];
        }
    }
    return NULL;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    GpuCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    const GpuInfo *gpu = best_gpu(app);
    Color accent = t->green;
    const char *icon = "gpu";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[160] = "";
    double target = 0;

    switch (card->kind) {
        case GPU_CARD_UTIL:
            title = "GPU UTILIZATION";
            target = gpu != NULL ? gpu->util_pct : 0;
            snprintf(value, sizeof(value), "%.0f", card->shown);
            snprintf(unit, sizeof(unit), "%%");
            snprintf(sub, sizeof(sub), "%s", gpu != NULL ? gpu->name : "No graphics device");
            break;
        case GPU_CARD_VRAM:
            accent = t->cyan;
            title = "VIDEO MEMORY";
            target = gpu != NULL ? gpu->vram_used_mb : 0;
            if (gpu != NULL && gpu->vram_total_mb > 0) {
                char used[32], total[32];
                fmt_bytes(gpu->vram_used_mb * 1048576.0, used, sizeof(used));
                fmt_bytes(gpu->vram_total_mb * 1048576.0, total, sizeof(total));
                snprintf(value, sizeof(value), "%s", used);
                double pct = gpu->vram_used_mb / gpu->vram_total_mb * 100.0;
                snprintf(sub, sizeof(sub), "of %s  \xc2\xb7  %.0f%%", total, pct);
            } else {
                snprintf(sub, sizeof(sub), "telemetry unavailable");
            }
            break;
        case GPU_CARD_TEMP:
            accent = gpu != NULL && gpu->temp_c >= 80 ? t->red :
                     (gpu != NULL && gpu->temp_c >= 65 ? t->yellow : t->orange);
            icon = "sensors";
            title = "GPU TEMPERATURE";
            target = gpu != NULL ? gpu->temp_c : 0;
            snprintf(value, sizeof(value), "%.0f", card->shown);
            snprintf(unit, sizeof(unit), "\xc2\xb0" "C");
            snprintf(sub, sizeof(sub), "%s", gpu != NULL ? gpu->kind : "no sensor");
            break;
        case GPU_CARD_POWER:
            accent = t->yellow;
            icon = "activity";
            title = "BOARD POWER";
            target = gpu != NULL ? gpu->power_w : 0;
            if (gpu != NULL && gpu->power_w > 0) {
                snprintf(value, sizeof(value), "%.0f", card->shown);
                snprintf(unit, sizeof(unit), "W");
                snprintf(sub, sizeof(sub), "limit %.0f W  \xc2\xb7  core %.0f MHz",
                         gpu->power_limit_w, gpu->clock_core_mhz);
            } else {
                snprintf(value, sizeof(value), "--");
                snprintf(sub, sizeof(sub), "power telemetry unavailable");
            }
            break;
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
    }
    if (card->kind == GPU_CARD_UTIL || card->kind == GPU_CARD_TEMP) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
    } else if (card->kind == GPU_CARD_POWER && gpu != NULL && gpu->power_w > 0) {
        snprintf(value, sizeof(value), "%.0f", card->shown);
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
    GpuChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "gpu", (Rect){17, 15, 15, 15}, t->green, 1.7);
    text_draw(cr, layout, "GPU ACTIVITY", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "utilization over time", 39, 40, 8.5, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_LEFT);
    const GpuInfo *gpu = best_gpu(app);
    char current[32];
    snprintf(current, sizeof(current), "%.0f%%", gpu != NULL ? gpu->util_pct : 0);
    text_draw(cr, layout, current, width - 18, 25, 26, PANGO_WEIGHT_BOLD,
              t->green, TEXT_RIGHT);
    char detail[128];
    if (gpu != NULL && gpu->vram_total_mb > 0) {
        snprintf(detail, sizeof(detail), "VRAM %.1f / %.1f GB  \xc2\xb7  %.0f MHz",
                 gpu->vram_used_mb / 1024.0, gpu->vram_total_mb / 1024.0,
                 gpu->clock_core_mhz);
    } else {
        snprintf(detail, sizeof(detail), "NVIDIA telemetry via nvidia-smi");
    }
    text_draw(cr, layout, detail, width - 18, 48, 8.7, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_RIGHT);

    ChartSeries series[] = {
        {app->history.gpu_util, t->green, "GPU utilization"},
    };
    cairo_new_path(cr);
    cairo_arc(cr, 22, 65, 3.2, 0, 2 * 3.141592653589793);
    cairo_set_source_rgba(cr, t->green.r, t->green.g, t->green.b, 1);
    cairo_fill(cr);
    text_draw(cr, layout, "GPU utilization", 30, 65, 8.7, PANGO_WEIGHT_NORMAL,
              t->dim, TEXT_LEFT);
    ChartSpec spec = {
        .history = &app->history,
        .rect = {14, 78, width - 28, height - 90},
        .series = series,
        .series_count = 1,
        .ymax = 100,
        .format = CHART_PERCENT,
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

static void list_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    GpuList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "system", (Rect){17, 15, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "GRAPHICS DEVICES", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    char count[32];
    snprintf(count, sizeof(count), "%d detected", app->view->gpu_count);
    text_draw(cr, layout, count, width - 18, 23, 8.7, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);

    double y = 48;
    int shown = 0;
    for (int i = 0; i < app->view->gpu_count && i < 4; i++) {
        const GpuInfo *gpu = &app->view->gpus[i];
        if (!gpu->present) {
            continue;
        }
        Color color = strcmp(gpu->kind, "nvidia") == 0 ? t->green : t->cyan;
        if (shown > 0) {
            draw_separator(cr, t, 18, y - 8, width - 18, y - 8);
        }
        text_draw_ellipsis(cr, layout, gpu->name, 18, y, 10, PANGO_WEIGHT_BOLD,
                           t->text, TEXT_LEFT, width - 36);
        char meta[160];
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s  \xc2\xb7  %s",
                 gpu->kind, gpu->vendor, gpu->uuid);
        text_draw_ellipsis(cr, layout, meta, 18, y + 16, 8.4,
                           PANGO_WEIGHT_NORMAL, t->faint, TEXT_LEFT, width - 36);

        double bar_y = y + 29;
        char vram[96];
        if (gpu->vram_total_mb > 0) {
            snprintf(vram, sizeof(vram), "VRAM %.1f / %.1f GB", gpu->vram_used_mb / 1024.0,
                     gpu->vram_total_mb / 1024.0);
        } else {
            snprintf(vram, sizeof(vram), "VRAM telemetry unavailable");
        }
        text_draw(cr, layout, vram, 18, bar_y + 13, 8.5, PANGO_WEIGHT_NORMAL,
                  t->dim, TEXT_LEFT);
        if (gpu->vram_total_mb > 0) {
            draw_progress(cr, t, (Rect){18, bar_y + 21, width - 36, 4},
                          gpu->vram_used_mb / gpu->vram_total_mb * 100.0,
                          color, 2);
        }
        char right[128];
        if (gpu->fan_rpm > 0) {
            snprintf(right, sizeof(right), "%.0f%%  \xc2\xb7  %.0f\xc2\xb0" "C  \xc2\xb7  %.0f W  \xc2\xb7  fan %.0f%% (%.0f rpm)",
                     gpu->util_pct, gpu->temp_c, gpu->power_w, gpu->fan_pct, gpu->fan_rpm);
        } else if (gpu->fan_pct > 0) {
            snprintf(right, sizeof(right), "%.0f%%  \xc2\xb7  %.0f\xc2\xb0" "C  \xc2\xb7  %.0f W  \xc2\xb7  fan %.0f%%",
                     gpu->util_pct, gpu->temp_c, gpu->power_w, gpu->fan_pct);
        } else {
            snprintf(right, sizeof(right), "%.0f%%  \xc2\xb7  %.0f\xc2\xb0" "C  \xc2\xb7  %.0f W",
                     gpu->util_pct, gpu->temp_c, gpu->power_w);
        }
        text_draw(cr, layout, right, width - 18, y + 2, 9, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        y += 74;
        shown++;
    }
    if (shown == 0) {
        text_draw(cr, layout, "No GPU detected", width / 2.0, height / 2.0, 12,
                  PANGO_WEIGHT_BOLD, t->dim, TEXT_CENTER);
        text_draw(cr, layout, "Intel and AMD devices appear through DRM sysfs.",
                  width / 2.0, height / 2.0 + 20, 9, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, GpuPage *page, GpuCardKind kind, int height) {
    GpuCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_gpu_create(App *app) {
    GpuPage *page = g_new0(GpuPage, 1);
    page->app = app;
    page->chart.app = app;
    page->list.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (GpuCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *chart = prism_area(app, 340, chart_draw, &page->chart);
    prism_hover_attach(chart, &page->chart.hover);
    gtk_widget_set_hexpand(chart, TRUE);
    gtk_widget_set_size_request(chart, 520, 340);
    gtk_box_append(GTK_BOX(row), chart);
    GtkWidget *list = prism_area(app, 340, list_draw, &page->list);
    gtk_widget_set_size_request(list, 440, 340);
    gtk_widget_set_hexpand(list, FALSE);
    prism_hover_attach(list, &page->list.hover);
    gtk_box_append(GTK_BOX(row), list);
    gtk_box_append(GTK_BOX(content), row);

    g_object_set_data_full(G_OBJECT(content), "gpu-page", page, g_free);
    app->pages[PAGE_GPU].root = content;
    return prism_page(content);
}

void page_gpu_update(App *app) {
    (void)app;
}
