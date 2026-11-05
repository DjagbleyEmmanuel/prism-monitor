#include "core/common.h"
#include "sys/systeminfo.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char name[128];
    char generic[128];
    char version[32];
    char exec[256];
    char icon[64];
    char source[24];
    char id[128];
    char path[512];
    int no_display;
} AppEntry;

typedef struct {
    App *app;
    ManagePage *ui;
    AppEntry *items;
    int count;
    char filter[128];
    int selected;
    int sort_name;
} AppsPage;

static int compare_apps(const void *a, const void *b) {
    const AppEntry *pa = a;
    const AppEntry *pb = b;
    return strcasecmp(pa->name, pb->name);
}

static void apps_scan_dir(AppsPage *page, const char *directory, const char *source) {
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir == NULL) {
        return;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL && page->count < PRISM_MAX_APPS) {
        if (!g_str_has_suffix(name, ".desktop")) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, name);
        GKeyFile *keyfile = g_key_file_new();
        if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL)) {
            g_key_file_free(keyfile);
            continue;
        }
        char *type = g_key_file_get_string(keyfile, "Desktop Entry", "Type", NULL);
        if (g_strcmp0(type, "Application") != 0) {
            g_free(type);
            g_key_file_free(keyfile);
            continue;
        }
        g_free(type);
        char *display = g_key_file_get_string(keyfile, "Desktop Entry", "Name", NULL);
        char *exec = g_key_file_get_string(keyfile, "Desktop Entry", "Exec", NULL);
        if (display == NULL || exec == NULL) {
            g_free(display);
            g_free(exec);
            g_key_file_free(keyfile);
            continue;
        }
        AppEntry *entry = &page->items[page->count++];
        memset(entry, 0, sizeof(*entry));
        g_strlcpy(entry->name, display, sizeof(entry->name));
        g_strlcpy(entry->exec, exec, sizeof(entry->exec));
        g_strlcpy(entry->source, source, sizeof(entry->source));
        g_strlcpy(entry->path, path, sizeof(entry->path));
        g_strlcpy(entry->id, name, sizeof(entry->id));
        char *value = g_key_file_get_string(keyfile, "Desktop Entry", "GenericName", NULL);
        if (value != NULL) {
            g_strlcpy(entry->generic, value, sizeof(entry->generic));
            g_free(value);
        }
        value = g_key_file_get_string(keyfile, "Desktop Entry", "Version", NULL);
        if (value != NULL) {
            g_strlcpy(entry->version, value, sizeof(entry->version));
            g_free(value);
        }
        value = g_key_file_get_string(keyfile, "Desktop Entry", "Icon", NULL);
        if (value != NULL) {
            g_strlcpy(entry->icon, value, sizeof(entry->icon));
            g_free(value);
        }
        value = g_key_file_get_string(keyfile, "Desktop Entry", "X-Flatpak", NULL);
        if (value != NULL) {
            g_strlcpy(entry->id, value, sizeof(entry->id));
            g_free(value);
        }
        entry->no_display = g_key_file_get_boolean(keyfile, "Desktop Entry",
                                                   "NoDisplay", NULL);
        g_free(display);
        g_free(exec);
        g_key_file_free(keyfile);
    }
    g_dir_close(dir);
}

static void apps_scan(AppsPage *page) {
    page->count = 0;
    apps_scan_dir(page, "/usr/share/applications", "system");
    apps_scan_dir(page, "/usr/local/share/applications", "local");
    char *user_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    apps_scan_dir(page, user_dir, "user");
    g_free(user_dir);
    apps_scan_dir(page, "/var/lib/flatpak/exports/share/applications", "flatpak");
    char *flatpak = g_build_filename(g_get_user_data_dir(), "flatpak", "exports",
                                     "share", "applications", NULL);
    apps_scan_dir(page, flatpak, "flatpak");
    g_free(flatpak);
    apps_scan_dir(page, "/var/lib/snapd/desktop/applications", "snap");
    qsort(page->items, page->count, sizeof(page->items[0]), compare_apps);
}

static int app_matches(const AppEntry *entry, const char *filter) {
    if (filter[0] == '\0') {
        return 1;
    }
    return g_strrstr(entry->name, filter) != NULL ||
           g_strrstr(entry->generic, filter) != NULL ||
           g_strrstr(entry->id, filter) != NULL ||
           g_strrstr(entry->exec, filter) != NULL ||
           g_strrstr(entry->source, filter) != NULL;
}

static void app_launch_command(const char *exec, char *out, size_t out_len) {
    size_t o = 0;
    for (const char *p = exec; *p != '\0' && o + 1 < out_len; p++) {
        if (*p == '%' && p[1] != '\0') {
            p++;
            continue;
        }
        out[o++] = *p;
    }
    out[o] = '\0';
    prism_trim(out);
}

static void app_launch(GtkButton *button, gpointer data) {
    (void)button;
    AppsPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        return;
    }
    char command[256];
    app_launch_command(page->items[page->selected].exec, command, sizeof(command));
    system_run_command(command);
    app_notify(page->app, "Launching %s", page->items[page->selected].name);
}

static void app_open_folder(GtkButton *button, gpointer data) {
    (void)button;
    AppsPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        return;
    }
    char *dir = g_path_get_dirname(page->items[page->selected].path);
    char *quoted = g_shell_quote(dir);
    char *command = g_strdup_printf("xdg-open %s", quoted);
    g_spawn_command_line_async(command, NULL);
    g_free(command);
    g_free(quoted);
    g_free(dir);
}

static void app_copy_exec(GtkButton *button, gpointer data) {
    (void)button;
    AppsPage *page = data;
    if (page->selected >= 0 && page->selected < page->count) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(page->app->window));
        gdk_clipboard_set_text(clipboard, page->items[page->selected].exec);
        app_notify(page->app, "Launch command copied");
    }
}

static void app_detail(AppsPage *page, int index) {
    ManagePage *ui = page->ui;
    manage_detail_clear(ui);
    if (index < 0 || index >= page->count) {
        manage_detail_heading(ui, "Select an application");
        manage_detail_note(ui, "Installed applications are discovered from desktop "
                               "entries in system, user, Flatpak and Snap locations.");
        return;
    }
    const AppEntry *entry = &page->items[index];
    manage_detail_heading(ui, entry->name);
    manage_detail_row(ui, "Description", entry->generic);
    manage_detail_row(ui, "Version", entry->version);
    manage_detail_row(ui, "Source", entry->source);
    manage_detail_row(ui, "Application ID", entry->id);
    manage_detail_row(ui, "Launch command", entry->exec);
    manage_detail_row(ui, "Desktop file", entry->path);
    if (entry->no_display) {
        manage_detail_note(ui, "This entry is hidden from application menus but is "
                               "still available to launch.");
    }

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *launch = prism_button("Launch", NULL);
    GtkWidget *folder = prism_button("Show file", NULL);
    GtkWidget *copy = prism_button("Copy command", NULL);
    gtk_box_append(GTK_BOX(actions), launch);
    gtk_box_append(GTK_BOX(actions), folder);
    gtk_box_append(GTK_BOX(actions), copy);
    g_signal_connect(launch, "clicked", G_CALLBACK(app_launch), page);
    g_signal_connect(folder, "clicked", G_CALLBACK(app_open_folder), page);
    g_signal_connect(copy, "clicked", G_CALLBACK(app_copy_exec), page);
}

static void app_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    AppsPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    app_detail(page, page->selected);
}

static void apps_rebuild(AppsPage *page, int keep) {
    ManagePage *ui = page->ui;
    int previous = page->selected;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    int hidden = 0;
    for (int i = 0; i < page->count; i++) {
        const AppEntry *entry = &page->items[i];
        if (!app_matches(entry, page->filter)) {
            continue;
        }
        if (entry->no_display) {
            hidden++;
        }
        manage_add_row(ui, i, entry->name, entry->generic[0] ? entry->generic : entry->id,
                       entry->source, entry->no_display ? 0 : 4, 0);
        if (i == previous && keep) {
            selected_row = i;
        }
        shown++;
    }
    (void)hidden;
    char text[160];
    snprintf(text, sizeof(text), "%d of %d applications", shown, page->count);
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else {
        page->selected = -1;
        app_detail(page, -1);
    }
}

static void app_filter(GtkSearchEntry *entry, gpointer data) {
    AppsPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    apps_rebuild(page, 0);
}

static void apps_refresh(GtkButton *button, gpointer data) {
    (void)button;
    AppsPage *page = data;
    apps_scan(page);
    apps_rebuild(page, 0);
    app_notify(page->app, "Application inventory refreshed");
}

static void apps_page_free(gpointer data) {
    AppsPage *page = data;
    g_free(page->items);
    g_free(page);
}

GtkWidget *page_apps_create(App *app) {
    AppsPage *page = g_new0(AppsPage, 1);
    page->app = app;
    page->items = g_new0(AppEntry, PRISM_MAX_APPS);
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter installed applications...", 380);
    manage_toolbar_button(page->ui, "Rescan", NULL, G_CALLBACK(apps_refresh), page);
    g_signal_connect(page->ui->search, "search-changed", G_CALLBACK(app_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected", G_CALLBACK(app_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "apps-page", page,
                           apps_page_free);
    apps_scan(page);
    apps_rebuild(page, 0);
    app->pages[PAGE_APPS].root = page->ui->root;
    return page->ui->root;
}

void page_apps_update(App *app) {
    (void)app;
}
