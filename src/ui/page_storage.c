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
    STOR_CARD_CAPACITY,
    STOR_CARD_READ,
    STOR_CARD_WRITE,
    STOR_CARD_DEVICES
} StorageCardKind;

typedef struct {
    App *app;
    StorageCardKind kind;
    HoverInfo hover;
    double shown;
} StorageCard;

typedef struct {
    App *app;
    HoverInfo hover;
} StorageChart;

typedef struct {
    App *app;
    HoverInfo hover;
} StorageList;

typedef struct {
    App *app;
    StorageCard cards[4];
    StorageChart chart;
    StorageList disks;
    StorageList mounts;
} StoragePage;

static const MountInfo *root_mount(App *app) {
    const MountInfo *best = NULL;
    for (int i = 0; i < app->view->mount_count; i++) {
        const MountInfo *mount = &app->view->mounts[i];
        if (strcmp(mount->mount, "/") == 0) {
            return mount;
        }
        if (best == NULL || mount->used_pct > best->used_pct) {
            best = mount;
        }
    }
    return best;
}

static void card_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                      gpointer data) {
    (void)area;
    StorageCard *card = data;
    App *app = card->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const MountInfo *root = root_mount(app);
    Rect rect = {1, 1, width - 2, height - 2};
    Color accent = t->orange;
    const char *icon = "disk";
    const char *title = "";
    char value[32] = "--";
    char unit[16] = "";
    char sub[160] = "";
    double target = 0;

    switch (card->kind) {
        case STOR_CARD_CAPACITY:
            title = "ROOT CAPACITY";
            target = root != NULL ? root->used_pct : 0;
            snprintf(value, sizeof(value), "%.0f", card->shown);
            snprintf(unit, sizeof(unit), "%%");
            if (root != NULL) {
                char used[32], total[32];
                fmt_bytes(root->used_mb * 1048576.0, used, sizeof(used));
                fmt_bytes(root->total_mb * 1048576.0, total, sizeof(total));
                snprintf(sub, sizeof(sub), "%s of %s  \xc2\xb7  %s", used, total, root->fs);
            } else {
                snprintf(sub, sizeof(sub), "no root mount");
            }
            break;
        case STOR_CARD_READ: {
            accent = t->cyan;
            icon = "activity";
            title = "READ THROUGHPUT";
            double rate = 0;
            for (int i = 0; i < app->view->disk_count; i++) {
                rate += app->view->disks[i].read_bps;
            }
            target = rate;
            char parts[16];
            fmt_rate_parts(rate, value, sizeof(value), parts, sizeof(parts));
            snprintf(unit, sizeof(unit), "%s", parts);
            snprintf(sub, sizeof(sub), "across %d block devices", app->view->disk_count);
            break;
        }
        case STOR_CARD_WRITE: {
            accent = t->red;
            icon = "activity";
            title = "WRITE THROUGHPUT";
            double rate = 0;
            for (int i = 0; i < app->view->disk_count; i++) {
                rate += app->view->disks[i].write_bps;
            }
            target = rate;
            char parts[16];
            fmt_rate_parts(rate, value, sizeof(value), parts, sizeof(parts));
            snprintf(unit, sizeof(unit), "%s", parts);
            snprintf(sub, sizeof(sub), "%d mount points observed", app->view->mount_count);
            break;
        }
        case STOR_CARD_DEVICES: {
            accent = t->purple;
            icon = "storage";
            title = "BLOCK DEVICES";
            target = app->view->disk_count;
            snprintf(value, sizeof(value), "%d", app->view->disk_count);
            int rotated = 0;
            double temp = -1;
            for (int i = 0; i < app->view->disk_count; i++) {
                if (app->view->disks[i].rotational) {
                    rotated++;
                }
                if (app->view->disks[i].temp_c > temp) {
                    temp = app->view->disks[i].temp_c;
                }
            }
            if (temp > 0) {
                snprintf(sub, sizeof(sub), "%d rotational  \xc2\xb7  %.0f\xc2\xb0" "C peak",
                         rotated, temp);
            } else {
                snprintf(sub, sizeof(sub), "%d rotational  \xc2\xb7  no temperature sensor", rotated);
            }
            break;
        }
    }

    if (app->config.animations) {
        card->shown = prism_ease(card->shown, target, app_ease_factor(app));
    } else {
        card->shown = target;
    }
    if (card->kind == STOR_CARD_CAPACITY) {
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
    StorageChart *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "disk", (Rect){17, 15, 15, 15}, t->orange, 1.7);
    text_draw(cr, layout, "DISK THROUGHPUT", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "read  \xc2\xb7  write", 39, 40, 8.5, PANGO_WEIGHT_NORMAL,
              t->faint, TEXT_LEFT);
    double read = 0;
    double write = 0;
    for (int i = 0; i < app->view->disk_count; i++) {
        read += app->view->disks[i].read_bps;
        write += app->view->disks[i].write_bps;
    }
    char rr[32], wr[32], current[128];
    fmt_rate(read, rr, sizeof(rr));
    fmt_rate(write, wr, sizeof(wr));
    snprintf(current, sizeof(current), "R %s   W %s", rr, wr);
    text_draw(cr, layout, current, width - 18, 25, 12, PANGO_WEIGHT_BOLD,
              t->text, TEXT_RIGHT);
    ChartSeries series[] = {
        {app->history.disk_r, t->orange, "Read"},
        {app->history.disk_w, t->red, "Write"},
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

static void disks_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    StorageList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "storage", (Rect){17, 15, 15, 15}, t->orange, 1.7);
    text_draw(cr, layout, "DEVICES", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim,
              TEXT_LEFT);
    text_draw(cr, layout, "READ", width - 126, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);
    text_draw(cr, layout, "WRITE", width - 62, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);
    text_draw(cr, layout, "TEMP", width - 18, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);

    double y = 46;
    int rows = app->view->disk_count < 4 ? app->view->disk_count : 4;
    for (int i = 0; i < rows; i++) {
        const DiskInfo *disk = &app->view->disks[i];
        Color color = i == 0 ? t->orange : (i == 1 ? t->yellow : t->cyan);
        if (i > 0) {
            draw_separator(cr, t, 18, y - 6, width - 18, y - 6);
        }
        char title[128];
        snprintf(title, sizeof(title), "%s", disk->model[0] ? disk->model : disk->device);
        text_draw_ellipsis(cr, layout, title, 18, y + 7, 10, PANGO_WEIGHT_BOLD,
                           t->text, TEXT_LEFT, width - 300);
        char meta[160];
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %.1f GB  \xc2\xb7  %s",
                 disk->device, disk->size_gb, disk->rotational ? "HDD" : "SSD");
        text_draw_ellipsis(cr, layout, meta, 18, y + 23, 8.4, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, width - 300);
        char rbuf[32], wbuf[32], temp[32];
        fmt_rate(disk->read_bps, rbuf, sizeof(rbuf));
        fmt_rate(disk->write_bps, wbuf, sizeof(wbuf));
        if (disk->temp_c > 0) {
            snprintf(temp, sizeof(temp), "%.0f\xc2\xb0", disk->temp_c);
        } else {
            snprintf(temp, sizeof(temp), "--");
        }
        text_draw(cr, layout, rbuf, width - 126, y + 14, 9, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        text_draw(cr, layout, wbuf, width - 62, y + 14, 9, PANGO_WEIGHT_BOLD,
                  t->red, TEXT_RIGHT);
        text_draw(cr, layout, temp, width - 18, y + 14, 9, PANGO_WEIGHT_BOLD,
                  disk->temp_c >= 60 ? t->red : t->dim, TEXT_RIGHT);
        y += 44;
    }
    if (rows == 0) {
        text_draw(cr, layout, "No block devices detected", width / 2.0,
                  height / 2.0, 11, PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static void mounts_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                        gpointer data) {
    (void)area;
    StorageList *state = data;
    App *app = state->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "system", (Rect){17, 15, 15, 15}, t->purple, 1.7);
    text_draw(cr, layout, "MOUNT POINTS", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw(cr, layout, "USED", width - 68, 23, 8.3, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_RIGHT);
    double y = 48;
    int rows = app->view->mount_count < 5 ? app->view->mount_count : 5;
    for (int i = 0; i < rows; i++) {
        const MountInfo *mount = &app->view->mounts[i];
        Color color = mount->used_pct >= 90 ? t->red :
                      (mount->used_pct >= 75 ? t->yellow : t->purple);
        if (i > 0) {
            draw_separator(cr, t, 18, y - 7, width - 18, y - 7);
        }
        text_draw_ellipsis(cr, layout, mount->mount, 18, y + 5, 9.8,
                           PANGO_WEIGHT_BOLD, t->text, TEXT_LEFT, width - 190);
        char meta[192];
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s  \xc2\xb7  %s",
                 mount->device, mount->fs,
                 mount->read_only ? "read-only" : "read-write");
        text_draw_ellipsis(cr, layout, meta, 18, y + 21, 8.2, PANGO_WEIGHT_NORMAL,
                           t->faint, TEXT_LEFT, width - 120);
        char pct[32];
        snprintf(pct, sizeof(pct), "%.0f%%", mount->used_pct);
        text_draw(cr, layout, pct, width - 18, y + 10, 10.5, PANGO_WEIGHT_BOLD,
                  color, TEXT_RIGHT);
        draw_progress(cr, t, (Rect){width - 68, y + 23, 50, 4}, mount->used_pct,
                      color, 2);
        y += 42;
    }
    if (rows == 0) {
        text_draw(cr, layout, "No mounts detected", width / 2.0, height / 2.0, 11,
                  PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER);
    }
    g_object_unref(layout);
}

static GtkWidget *make_card(App *app, StoragePage *page, StorageCardKind kind,
                            int height) {
    StorageCard *card = &page->cards[kind];
    card->app = app;
    card->kind = kind;
    GtkWidget *area = prism_card_area(app, height, card_draw, card);
    app_track_anim(app, area);
    prism_hover_attach(area, &card->hover);
    return area;
}

GtkWidget *page_storage_create(App *app) {
    StoragePage *page = g_new0(StoragePage, 1);
    page->app = app;
    page->chart.app = app;
    page->disks.app = app;
    page->mounts.app = app;

    GtkWidget *content = prism_column();
    GtkWidget *cards = prism_grid();
    gtk_grid_set_column_homogeneous(GTK_GRID(cards), TRUE);
    for (int i = 0; i < 4; i++) {
        prism_grid_put(GTK_GRID(cards), make_card(app, page, (StorageCardKind)i, 104),
                       i, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(content), cards);

    GtkWidget *row = prism_row();
    GtkWidget *chart = prism_area(app, 300, chart_draw, &page->chart);
    prism_hover_attach(chart, &page->chart.hover);
    gtk_widget_set_hexpand(chart, TRUE);
    gtk_widget_set_size_request(chart, 540, 300);
    gtk_box_append(GTK_BOX(row), chart);
    GtkWidget *mounts = prism_area(app, 300, mounts_draw, &page->mounts);
    gtk_widget_set_size_request(mounts, 430, 300);
    gtk_widget_set_hexpand(mounts, FALSE);
    prism_hover_attach(mounts, &page->mounts.hover);
    gtk_box_append(GTK_BOX(row), mounts);
    gtk_box_append(GTK_BOX(content), row);

    GtkWidget *disks = prism_area(app, 230, disks_draw, &page->disks);
    prism_hover_attach(disks, &page->disks.hover);
    gtk_box_append(GTK_BOX(content), disks);

    g_object_set_data_full(G_OBJECT(content), "storage-page", page, g_free);
    app->pages[PAGE_STORAGE].root = content;
    return prism_page(content);
}

void page_storage_update(App *app) {
    (void)app;
}
