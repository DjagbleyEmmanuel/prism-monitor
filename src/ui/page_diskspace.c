#include "core/common.h"
#include "core/format.h"
#include "sys/systeminfo.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DISK_TOP 32
#define DISK_MAX_ENTRIES 180000

typedef struct {
    char path[512];
    guint64 size;
} DiskItem;

typedef struct {
    char name[32];
    guint64 size;
} TypeItem;

typedef struct {
    char root[512];
    char error[160];
    guint64 total;
    guint64 scanned;
    int truncated;
    dev_t root_device;
    DiskItem files[DISK_TOP];
    int file_count;
    DiskItem dirs[DISK_TOP];
    int dir_count;
    TypeItem types[8];
    int type_count;
} DiskAnalysis;

typedef struct {
    App *app;
    ManagePage *ui;
    GtkWidget *path_entry;
    GtkWidget *analyze_button;
    char filter[128];
    int selected;
    dev_t root_device;
    int running;
} DiskPage;

static void offer_item(DiskItem *items, int *count, const char *path, guint64 size,
                       int max) {
    if (size == 0) {
        return;
    }
    int index = -1;
    if (*count < max) {
        index = (*count)++;
    } else {
        int smallest = 0;
        for (int i = 1; i < max; i++) {
            if (items[i].size < items[smallest].size) {
                smallest = i;
            }
        }
        if (size <= items[smallest].size) {
            return;
        }
        index = smallest;
    }
    g_strlcpy(items[index].path, path, sizeof(items[index].path));
    items[index].size = size;
}

static void type_add(DiskAnalysis *analysis, const char *name, guint64 size) {
    for (int i = 0; i < analysis->type_count; i++) {
        if (strcmp(analysis->types[i].name, name) == 0) {
            analysis->types[i].size += size;
            return;
        }
    }
    if (analysis->type_count < 8) {
        TypeItem *item = &analysis->types[analysis->type_count++];
        g_strlcpy(item->name, name, sizeof(item->name));
        item->size = size;
    }
}

static const char *type_category(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return "Other";
    }
    if (g_ascii_strcasecmp(dot, ".jpg") == 0 || g_ascii_strcasecmp(dot, ".jpeg") == 0 ||
        g_ascii_strcasecmp(dot, ".png") == 0 || g_ascii_strcasecmp(dot, ".webp") == 0 ||
        g_ascii_strcasecmp(dot, ".gif") == 0 || g_ascii_strcasecmp(dot, ".svg") == 0 ||
        g_ascii_strcasecmp(dot, ".bmp") == 0 || g_ascii_strcasecmp(dot, ".heic") == 0) {
        return "Images";
    }
    if (g_ascii_strcasecmp(dot, ".mp4") == 0 || g_ascii_strcasecmp(dot, ".mkv") == 0 ||
        g_ascii_strcasecmp(dot, ".avi") == 0 || g_ascii_strcasecmp(dot, ".mov") == 0 ||
        g_ascii_strcasecmp(dot, ".webm") == 0) {
        return "Video";
    }
    if (g_ascii_strcasecmp(dot, ".mp3") == 0 || g_ascii_strcasecmp(dot, ".flac") == 0 ||
        g_ascii_strcasecmp(dot, ".ogg") == 0 || g_ascii_strcasecmp(dot, ".wav") == 0 ||
        g_ascii_strcasecmp(dot, ".m4a") == 0) {
        return "Audio";
    }
    if (g_ascii_strcasecmp(dot, ".pdf") == 0 || g_ascii_strcasecmp(dot, ".doc") == 0 ||
        g_ascii_strcasecmp(dot, ".docx") == 0 || g_ascii_strcasecmp(dot, ".odt") == 0 ||
        g_ascii_strcasecmp(dot, ".txt") == 0 || g_ascii_strcasecmp(dot, ".md") == 0) {
        return "Documents";
    }
    if (g_ascii_strcasecmp(dot, ".zip") == 0 || g_ascii_strcasecmp(dot, ".tar") == 0 ||
        g_ascii_strcasecmp(dot, ".gz") == 0 || g_ascii_strcasecmp(dot, ".xz") == 0 ||
        g_ascii_strcasecmp(dot, ".7z") == 0 || g_ascii_strcasecmp(dot, ".deb") == 0 ||
        g_ascii_strcasecmp(dot, ".rpm") == 0 || g_ascii_strcasecmp(dot, ".iso") == 0) {
        return "Archives";
    }
    if (g_ascii_strcasecmp(dot, ".c") == 0 || g_ascii_strcasecmp(dot, ".h") == 0 ||
        g_ascii_strcasecmp(dot, ".py") == 0 || g_ascii_strcasecmp(dot, ".rs") == 0 ||
        g_ascii_strcasecmp(dot, ".go") == 0 || g_ascii_strcasecmp(dot, ".js") == 0 ||
        g_ascii_strcasecmp(dot, ".ts") == 0 || g_ascii_strcasecmp(dot, ".json") == 0) {
        return "Code";
    }
    return "Other";
}

static void disk_scan_dir(DiskAnalysis *analysis, const char *path, int depth,
                          int top_level) {
    if (analysis->scanned >= DISK_MAX_ENTRIES) {
        analysis->truncated = 1;
        return;
    }
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (analysis->scanned >= DISK_MAX_ENTRIES) {
            analysis->truncated = 1;
            break;
        }
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat info;
        if (lstat(child, &info) != 0) {
            continue;
        }
        if (S_ISLNK(info.st_mode)) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            if (info.st_dev != analysis->root_device) {
                continue;
            }
            if (strcmp(child, "/proc") == 0 || strcmp(child, "/sys") == 0 ||
                strcmp(child, "/dev") == 0 || strcmp(child, "/run") == 0) {
                continue;
            }
            guint64 before = analysis->total;
            if (depth < 7) {
                disk_scan_dir(analysis, child, depth + 1, 0);
            }
            if (top_level) {
                offer_item(analysis->dirs, &analysis->dir_count, child,
                           analysis->total - before, DISK_TOP);
            }
        } else if (S_ISREG(info.st_mode)) {
            analysis->scanned++;
            if (info.st_size > 0) {
                analysis->total += (guint64)info.st_size;
                offer_item(analysis->files, &analysis->file_count, child,
                           (guint64)info.st_size, DISK_TOP);
                type_add(analysis, type_category(entry->d_name), (guint64)info.st_size);
            }
        }
    }
    closedir(dir);
}

static void disk_analyze_thread(GTask *task, gpointer source, gpointer data,
                                GCancellable *cancellable) {
    (void)source;
    (void)data;
    (void)cancellable;
    const char *path = g_task_get_task_data(task);
    DiskAnalysis *analysis = g_new0(DiskAnalysis, 1);
    g_strlcpy(analysis->root, path, sizeof(analysis->root));
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) {
        g_strlcpy(analysis->error, "The path is not a readable directory.",
                  sizeof(analysis->error));
        g_task_return_pointer(task, analysis, g_free);
        return;
    }
    analysis->root_device = info.st_dev;
    disk_scan_dir(analysis, path, 0, 1);
    g_task_return_pointer(task, analysis, g_free);
}

static void disk_item_size(guint64 size, char *buf, size_t len) {
    fmt_bytes((double)size, buf, len);
}

static void disk_analysis_show(DiskPage *page, DiskAnalysis *analysis) {
    ManagePage *ui = page->ui;
    manage_detail_clear(ui);
    char title[600];
    snprintf(title, sizeof(title), "Disk usage");
    manage_detail_heading(ui, title);
    if (analysis->error[0] != '\0') {
        manage_detail_note(ui, analysis->error);
        return;
    }
    char size[32];
    disk_item_size(analysis->total, size, sizeof(size));
    manage_detail_row(ui, "Analyzed path", analysis->root);
    manage_detail_row(ui, "Total size", size);
    char count[64];
    snprintf(count, sizeof(count), "%llu files", analysis->scanned);
    manage_detail_row(ui, "Files scanned", count);
    if (analysis->truncated) {
        manage_detail_note(ui, "The scan reached its safety limit. Results are a "
                               "partial snapshot and the interface stayed responsive.");
    }

    manage_detail_heading(ui, "Large files");
    for (int i = 0; i < analysis->file_count; i++) {
        DiskItem *item = &analysis->files[i];
        char value[600];
        disk_item_size(item->size, size, sizeof(size));
        snprintf(value, sizeof(value), "%s  (%s)", item->path, size);
        manage_detail_row(ui, "File", value);
    }
    if (analysis->file_count == 0) {
        manage_detail_note(ui, "No regular files were found in the scanned range.");
    }

    manage_detail_heading(ui, "Largest folders");
    for (int i = 0; i < analysis->dir_count; i++) {
        DiskItem *item = &analysis->dirs[i];
        char value[600];
        disk_item_size(item->size, size, sizeof(size));
        snprintf(value, sizeof(value), "%s  (%s)", item->path, size);
        manage_detail_row(ui, "Folder", value);
    }

    manage_detail_heading(ui, "File types");
    for (int i = 0; i < analysis->type_count; i++) {
        char value[64];
        double pct = analysis->total > 0
            ? (double)analysis->types[i].size / (double)analysis->total * 100.0 : 0;
        disk_item_size(analysis->types[i].size, size, sizeof(size));
        snprintf(value, sizeof(value), "%s  \xc2\xb7  %.1f%%", size, pct);
        manage_detail_row(ui, analysis->types[i].name, value);
    }
}

static void disk_analysis_done(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    DiskPage *page = data;
    DiskAnalysis *analysis = g_task_propagate_pointer(G_TASK(result), NULL);
    page->running = 0;
    if (analysis == NULL) {
        manage_detail_clear(page->ui);
        manage_detail_heading(page->ui, "Disk usage");
        manage_detail_note(page->ui, "The analysis could not be completed.");
        return;
    }
    disk_analysis_show(page, analysis);
    app_notify(page->app, "Disk analysis complete");
    g_free(analysis);
}

static void disk_analyze_path(DiskPage *page, const char *path) {
    if (path == NULL || path[0] == '\0' || page->running) {
        return;
    }
    page->running = 1;
    manage_detail_clear(page->ui);
    manage_detail_heading(page->ui, "Analyzing disk usage");
    manage_detail_note(page->ui, "Prism is walking the directory tree in the "
                                 "background. Large or remote filesystems may take "
                                 "a moment.");
    GTask *task = g_task_new(NULL, NULL, disk_analysis_done, page);
    g_task_set_task_data(task, g_strdup(path), g_free);
    g_task_run_in_thread(task, disk_analyze_thread);
    g_object_unref(task);
}

static void disk_analyze_clicked(GtkButton *button, gpointer data) {
    (void)button;
    DiskPage *page = data;
    const char *path = gtk_editable_get_text(GTK_EDITABLE(page->path_entry));
    disk_analyze_path(page, path);
}

static void disk_open_clicked(GtkButton *button, gpointer data) {
    (void)button;
    DiskPage *page = data;
    const char *path = gtk_editable_get_text(GTK_EDITABLE(page->path_entry));
    if (path != NULL && path[0] != '\0') {
        char *quoted = g_shell_quote(path);
        char *command = g_strdup_printf("xdg-open %s", quoted);
        g_spawn_command_line_async(command, NULL);
        g_free(command);
        g_free(quoted);
    }
}

static void disk_analyze_mount(GtkButton *button, gpointer data) {
    (void)button;
    DiskPage *page = data;
    if (page->selected < 0 || page->selected >= page->app->view->mount_count) {
        return;
    }
    const MountInfo *mount = &page->app->view->mounts[page->selected];
    gtk_editable_set_text(GTK_EDITABLE(page->path_entry), mount->mount);
    disk_analyze_path(page, mount->mount);
}

static void disk_detail(DiskPage *page, int index) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    manage_detail_clear(ui);
    if (index < 0 || index >= app->view->mount_count) {
        manage_detail_heading(ui, "Select a filesystem");
        manage_detail_note(ui, "Mount points are read from /proc/self/mounts. Use the "
                               "path field above to analyze any folder for large "
                               "files, folders and file types.");
        return;
    }
    const MountInfo *mount = &app->view->mounts[index];
    manage_detail_heading(ui, mount->mount);
    char total[32], used[32], avail[32];
    fmt_bytes(mount->total_mb * 1048576.0, total, sizeof(total));
    fmt_bytes(mount->used_mb * 1048576.0, used, sizeof(used));
    fmt_bytes(mount->avail_mb * 1048576.0, avail, sizeof(avail));
    manage_detail_row(ui, "Device", mount->device);
    manage_detail_row(ui, "Filesystem", mount->fs);
    manage_detail_row(ui, "Capacity", total);
    manage_detail_row(ui, "Used", used);
    manage_detail_row(ui, "Available", avail);
    char pct[32];
    snprintf(pct, sizeof(pct), "%.1f%%", mount->used_pct);
    manage_detail_row(ui, "Utilization", pct);
    manage_detail_row(ui, "Mode", mount->read_only ? "read-only" : "read-write");

    GtkWidget *bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), mount->used_pct / 100.0);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), FALSE);
    gtk_widget_set_margin_top(bar, 6);
    gtk_box_append(GTK_BOX(ui->detail), bar);

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *analyze = prism_button("Analyze usage", NULL);
    GtkWidget *open = prism_button("Open folder", NULL);
    gtk_box_append(GTK_BOX(actions), analyze);
    gtk_box_append(GTK_BOX(actions), open);
    g_signal_connect(analyze, "clicked", G_CALLBACK(disk_analyze_mount), page);
    g_signal_connect(open, "clicked", G_CALLBACK(disk_open_clicked), page);
    gtk_editable_set_text(GTK_EDITABLE(page->path_entry), mount->mount);
}

static void disk_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    DiskPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    disk_detail(page, page->selected);
}

static void disk_rebuild(DiskPage *page) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < app->view->mount_count; i++) {
        const MountInfo *mount = &app->view->mounts[i];
        if (page->filter[0] != '\0' &&
            g_strrstr(mount->mount, page->filter) == NULL &&
            g_strrstr(mount->device, page->filter) == NULL &&
            g_strrstr(mount->fs, page->filter) == NULL) {
            continue;
        }
        char sub[360];
        char used[24], total[24];
        fmt_bytes(mount->used_mb * 1048576.0, used, sizeof(used));
        fmt_bytes(mount->total_mb * 1048576.0, total, sizeof(total));
        snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s of %s  \xc2\xb7  %s",
                 mount->device, used, total, mount->fs);
        char badge[24];
        snprintf(badge, sizeof(badge), "%.0f%%", mount->used_pct);
        int state = mount->used_pct >= 90 ? 2 : (mount->used_pct >= 75 ? 3 : 1);
        manage_add_row(ui, i, mount->mount, sub, badge, state, 0);
        if (i == page->selected) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d mounts  \xc2\xb7  root %s",
             shown, app->view->mount_count > 0 ? "tracked" : "unknown");
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else {
        page->selected = -1;
        disk_detail(page, -1);
    }
}

static void disk_filter(GtkSearchEntry *entry, gpointer data) {
    DiskPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    disk_rebuild(page);
}

static void disk_scan_now(GtkButton *button, gpointer data) {
    (void)button;
    DiskPage *page = data;
    sampler_invalidate_aux(page->app->sampler);
    disk_rebuild(page);
    app_notify(page->app, "Mount inventory refresh queued");
}

GtkWidget *page_diskspace_create(App *app) {
    DiskPage *page = g_new0(DiskPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter mounts...", 430);

    page->path_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(page->path_entry),
                                   "Folder to analyze");
    char *home = g_strdup(g_get_home_dir());
    gtk_editable_set_text(GTK_EDITABLE(page->path_entry), home != NULL ? home : "/");
    g_free(home);
    gtk_widget_set_size_request(page->path_entry, 230, -1);
    gtk_box_insert_child_after(GTK_BOX(page->ui->toolbar), page->path_entry,
                               page->ui->search);
    page->analyze_button = manage_toolbar_button(page->ui, "Analyze", NULL,
                                                 G_CALLBACK(disk_analyze_clicked),
                                                 page);
    manage_toolbar_button(page->ui, "Refresh", NULL,
                          G_CALLBACK(disk_scan_now), page);
    g_signal_connect(page->ui->search, "search-changed",
                     G_CALLBACK(disk_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected",
                     G_CALLBACK(disk_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "disk-page", page, g_free);
    disk_rebuild(page);
    app->pages[PAGE_DISKSPACE].root = page->ui->root;
    return page->ui->root;
}

void page_diskspace_update(App *app) {
    DiskPage *page = g_object_get_data(G_OBJECT(app->pages[PAGE_DISKSPACE].root),
                                       "disk-page");
    if (page == NULL || app->current_page != PAGE_DISKSPACE) {
        return;
    }
    if (page->selected < 0 && app->view->mount_count > 0) {
        disk_rebuild(page);
    }
}
