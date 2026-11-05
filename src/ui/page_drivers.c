#include "core/common.h"
#include "sys/systeminfo.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

typedef struct {
    char name[64];
    char size[32];
    char refcount[16];
    char deps[320];
    char state[24];
    int built_in;
} DriverEntry;

typedef struct {
    App *app;
    ManagePage *ui;
    DriverEntry *items;
    int count;
    int loaded;
    char filter[128];
    int selected;
} DriversPage;

static int driver_compare(const void *a, const void *b) {
    const DriverEntry *pa = a;
    const DriverEntry *pb = b;
    if (pa->built_in != pb->built_in) {
        return pa->built_in - pb->built_in;
    }
    return strcasecmp(pa->name, pb->name);
}

static int driver_exists(DriversPage *page, const char *name) {
    for (int i = 0; i < page->count; i++) {
        if (strcmp(page->items[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void drivers_read_loaded(DriversPage *page) {
    char *text = prism_read_file("/proc/modules");
    if (text == NULL) {
        return;
    }
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line != NULL && page->count < PRISM_MAX_DRIVERS) {
        char name[64];
        char size[32];
        char refcount[16];
        char deps[320];
        char state[24];
        if (sscanf(line, "%63s %31s %15s %319s %23s", name, size, refcount,
                   deps, state) >= 4) {
            DriverEntry *entry = &page->items[page->count++];
            memset(entry, 0, sizeof(*entry));
            g_strlcpy(entry->name, name, sizeof(entry->name));
            g_strlcpy(entry->size, size, sizeof(entry->size));
            g_strlcpy(entry->refcount, refcount, sizeof(entry->refcount));
            g_strlcpy(entry->deps, strcmp(deps, "-") == 0 ? "" : deps,
                      sizeof(entry->deps));
            g_strlcpy(entry->state, state, sizeof(entry->state));
            page->loaded++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(text);
}

static void drivers_read_builtin(DriversPage *page) {
    struct utsname info;
    if (uname(&info) != 0) {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "/lib/modules/%s/modules.builtin", info.release);
    char *text = prism_read_file(path);
    if (text == NULL) {
        return;
    }
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    while (line != NULL && page->count < PRISM_MAX_DRIVERS) {
        char *base = strrchr(line, '/');
        base = base != NULL ? base + 1 : line;
        char name[64];
        g_strlcpy(name, base, sizeof(name));
        char *suffix = g_strrstr(name, ".ko");
        if (suffix != NULL) {
            *suffix = '\0';
        }
        if (name[0] != '\0' && !driver_exists(page, name)) {
            DriverEntry *entry = &page->items[page->count++];
            memset(entry, 0, sizeof(*entry));
            g_strlcpy(entry->name, name, sizeof(entry->name));
            g_strlcpy(entry->state, "built-in", sizeof(entry->state));
            g_strlcpy(entry->deps, "compiled into kernel", sizeof(entry->deps));
            entry->built_in = 1;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(text);
}

static void drivers_scan(DriversPage *page) {
    page->count = 0;
    page->loaded = 0;
    drivers_read_loaded(page);
    drivers_read_builtin(page);
    qsort(page->items, page->count, sizeof(page->items[0]), driver_compare);
}

static int driver_matches(const DriverEntry *entry, const char *filter) {
    if (filter[0] == '\0') {
        return 1;
    }
    return g_strrstr(entry->name, filter) != NULL ||
           g_strrstr(entry->deps, filter) != NULL ||
           g_strrstr(entry->state, filter) != NULL ||
           g_strrstr(entry->refcount, filter) != NULL;
}

static void driver_load_dialog_cb(App *app, const char *command, gpointer data) {
    (void)data;
    char *quoted = g_shell_quote(command);
    char *line = g_strdup_printf("pkexec modprobe %s", quoted);
    system_run_command(line);
    g_free(line);
    g_free(quoted);
    app_notify(app, "Module load requested");
    sampler_invalidate_aux(app->sampler);
}

static void driver_load(GtkButton *button, gpointer data) {
    (void)button;
    DriversPage *page = data;
    manage_command_dialog(page->app, "Load kernel module",
                          "Enter a module name such as uvcvideo. Loading a module "
                          "requires administrative authentication.",
                          "", driver_load_dialog_cb, page);
}

static void driver_unload(GtkButton *button, gpointer data) {
    (void)button;
    DriversPage *page = data;
    if (page->selected < 0 || page->selected >= page->count) {
        return;
    }
    const DriverEntry *entry = &page->items[page->selected];
    if (entry->built_in) {
        app_notify(page->app, "%s is built into the kernel", entry->name);
        return;
    }
    char *quoted = g_shell_quote(entry->name);
    char *command = g_strdup_printf("pkexec modprobe -r %s", quoted);
    system_run_command(command);
    g_free(command);
    g_free(quoted);
    app_notify(page->app, "Module unload requested for %s", entry->name);
    sampler_invalidate_aux(page->app->sampler);
}

static void driver_detail(DriversPage *page, int index) {
    ManagePage *ui = page->ui;
    manage_detail_clear(ui);
    if (index < 0 || index >= page->count) {
        manage_detail_heading(ui, "Select a kernel module");
        manage_detail_note(ui, "Loaded modules come from /proc/modules. Built-in "
                               "drivers are read from modules.builtin for the "
                               "running kernel.");
        return;
    }
    const DriverEntry *entry = &page->items[index];
    manage_detail_heading(ui, entry->name);
    manage_detail_row(ui, "Status", entry->built_in ? "Built into kernel" : "Loaded module");
    manage_detail_row(ui, "State", entry->state);
    manage_detail_row(ui, "Reference count", entry->refcount);
    manage_detail_row(ui, "Dependencies", entry->deps);
    if (entry->size[0] != '\0') {
        char size[48];
        unsigned long long bytes = strtoull(entry->size, NULL, 10);
        if (bytes >= 1048576) {
            snprintf(size, sizeof(size), "%.1f MiB", bytes / 1048576.0);
        } else if (bytes >= 1024) {
            snprintf(size, sizeof(size), "%.1f KiB", bytes / 1024.0);
        } else {
            snprintf(size, sizeof(size), "%llu bytes", bytes);
        }
        manage_detail_row(ui, "Module size", size);
    }
    manage_detail_note(ui, entry->built_in
        ? "Built-in drivers cannot be unloaded without rebuilding or rebooting "
          "with different kernel parameters."
        : "Unloading a module that is in use can disrupt devices. Prism asks for "
          "administrative authentication before making changes.");

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *load = prism_button("Load module...", NULL);
    GtkWidget *unload = prism_button("Unload", NULL);
    gtk_box_append(GTK_BOX(actions), load);
    if (!entry->built_in) {
        gtk_box_append(GTK_BOX(actions), unload);
    }
    g_signal_connect(load, "clicked", G_CALLBACK(driver_load), page);
    g_signal_connect(unload, "clicked", G_CALLBACK(driver_unload), page);
}

static void driver_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    DriversPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    driver_detail(page, page->selected);
}

static void drivers_rebuild(DriversPage *page, int keep) {
    ManagePage *ui = page->ui;
    int previous = page->selected;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < page->count; i++) {
        const DriverEntry *entry = &page->items[i];
        if (!driver_matches(entry, page->filter)) {
            continue;
        }
        char subtitle[360];
        if (entry->built_in) {
            snprintf(subtitle, sizeof(subtitle), "compiled into kernel");
        } else {
            snprintf(subtitle, sizeof(subtitle), "%s refs  \xc2\xb7  %s",
                     entry->refcount, entry->state);
        }
        manage_add_row(ui, i, entry->name, subtitle,
                       entry->built_in ? "built-in" : "loaded",
                       entry->built_in ? 5 : 1, 0);
        if (i == previous && keep) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d of %d modules  \xc2\xb7  %d loaded",
             shown, page->count, page->loaded);
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else {
        page->selected = -1;
        driver_detail(page, -1);
    }
}

static void driver_filter(GtkSearchEntry *entry, gpointer data) {
    DriversPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    drivers_rebuild(page, 0);
}

static void drivers_refresh(GtkButton *button, gpointer data) {
    (void)button;
    DriversPage *page = data;
    drivers_scan(page);
    drivers_rebuild(page, 0);
    app_notify(page->app, "Kernel module inventory refreshed");
}

static void drivers_page_free(gpointer data) {
    DriversPage *page = data;
    g_free(page->items);
    g_free(page);
}

GtkWidget *page_drivers_create(App *app) {
    DriversPage *page = g_new0(DriversPage, 1);
    page->app = app;
    page->items = g_new0(DriverEntry, PRISM_MAX_DRIVERS);
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter kernel modules...", 380);
    manage_toolbar_button(page->ui, "Load module", NULL,
                          G_CALLBACK(driver_load), page);
    manage_toolbar_button(page->ui, "Refresh", NULL,
                          G_CALLBACK(drivers_refresh), page);
    g_signal_connect(page->ui->search, "search-changed",
                     G_CALLBACK(driver_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected",
                     G_CALLBACK(driver_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "drivers-page", page,
                           drivers_page_free);
    drivers_scan(page);
    drivers_rebuild(page, 0);
    app->pages[PAGE_DRIVERS].root = page->ui->root;
    return page->ui->root;
}

void page_drivers_update(App *app) {
    (void)app;
}
