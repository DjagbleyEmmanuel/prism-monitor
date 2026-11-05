#include "core/common.h"
#include "sys/systeminfo.h"
#include "ui/app.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    App *app;
    ManagePage *ui;
    char filter[128];
    int selected;
    int signature;
} StartupPage;

static void startup_toggle(GtkButton *button, gpointer data);
static void startup_run(GtkButton *button, gpointer data);

static int startup_signature(App *app) {
    int signature = app->view->startup_count;
    for (int i = 0; i < app->view->startup_count; i++) {
        signature = signature * 31 + app->view->startups[i].enabled;
        signature = signature * 31 + (int)strlen(app->view->startups[i].name);
    }
    return signature;
}

static int startup_matches(const StartupInfo *item, const char *filter) {
    if (filter[0] == '\0') {
        return 1;
    }
    return g_strrstr(item->name, filter) != NULL ||
           g_strrstr(item->exec, filter) != NULL ||
           g_strrstr(item->source, filter) != NULL;
}

static void startup_detail(StartupPage *page, int index) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    manage_detail_clear(ui);
    if (index < 0 || index >= app->view->startup_count) {
        manage_detail_heading(ui, "Select an autostart entry");
        manage_detail_note(ui, "Autostart entries launch applications when your "
                               "desktop session starts.");
        return;
    }
    const StartupInfo *item = &app->view->startups[index];
    manage_detail_heading(ui, item->name);
    manage_detail_row(ui, "Source", strcmp(item->source, "user") == 0
                                       ? "User override" : "System package");
    manage_detail_row(ui, "Launch command", item->exec);
    manage_detail_row(ui, "Desktop file", item->file);
    manage_detail_row(ui, "At login", item->enabled ? "Enabled" : "Disabled");
    manage_detail_note(ui, "Disabling a system entry creates a user override in "
                           "~/.config/autostart so the original file stays "
                           "untouched.");

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *toggle = prism_button(item->enabled ? "Disable at login"
                                                   : "Enable at login", NULL);
    GtkWidget *run = prism_button("Run now", NULL);
    gtk_box_append(GTK_BOX(actions), toggle);
    gtk_box_append(GTK_BOX(actions), run);
    g_object_set_data(G_OBJECT(toggle), "startup-page", page);
    g_object_set_data(G_OBJECT(run), "startup-page", page);
    g_signal_connect(toggle, "clicked", G_CALLBACK(startup_toggle), page);
    g_signal_connect(run, "clicked", G_CALLBACK(startup_run), page);
}

static void startup_toggle(GtkButton *button, gpointer data) {
    StartupPage *page = data;
    int index = page->selected;
    if (index < 0 || index >= page->app->view->startup_count) {
        return;
    }
    StartupInfo *item = &page->app->view->startups[index];
    startup_set_enabled(item, !item->enabled);
    item->enabled = !item->enabled;
    app_notify(page->app, "%s at login", item->enabled ? "Enabled" : "Disabled");
    sampler_invalidate_aux(page->app->sampler);
    page->signature = -1;
    startup_detail(page, index);
}

static void startup_run(GtkButton *button, gpointer data) {
    (void)button;
    StartupPage *page = data;
    int index = page->selected;
    if (index < 0 || index >= page->app->view->startup_count) {
        return;
    }
    char command[256];
    g_strlcpy(command, page->app->view->startups[index].exec, sizeof(command));
    char *percent = strchr(command, '%');
    if (percent != NULL) {
        *percent = '\0';
    }
    system_run_command(command);
    app_notify(page->app, "Launching %s", page->app->view->startups[index].name);
}

static void startup_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    StartupPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    startup_detail(page, page->selected);
}

static void startup_rebuild(StartupPage *page, int keep) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    int previous = page->selected;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < app->view->startup_count; i++) {
        const StartupInfo *item = &app->view->startups[i];
        if (!startup_matches(item, page->filter)) {
            continue;
        }
        char subtitle[192];
        snprintf(subtitle, sizeof(subtitle), "%s  \xc2\xb7  %s",
                 strcmp(item->source, "user") == 0 ? "user override" : "system",
                 item->exec);
        manage_add_row(ui, i, item->name, subtitle,
                       item->enabled ? "enabled" : "disabled",
                       item->enabled ? 1 : 0, 0);
        if (i == previous && keep) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d of %d entries", shown, app->view->startup_count);
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else {
        page->selected = -1;
        startup_detail(page, -1);
    }
}

static void startup_filter(GtkSearchEntry *entry, gpointer data) {
    StartupPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    startup_rebuild(page, 0);
}

static void startup_refresh(GtkButton *button, gpointer data) {
    (void)button;
    StartupPage *page = data;
    sampler_invalidate_aux(page->app->sampler);
    page->signature = -1;
    app_notify(page->app, "Autostart inventory refresh queued");
}

GtkWidget *page_startup_create(App *app) {
    StartupPage *page = g_new0(StartupPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter autostart entries...", 370);
    manage_toolbar_button(page->ui, "Refresh", NULL, G_CALLBACK(startup_refresh), page);
    g_signal_connect(page->ui->search, "search-changed", G_CALLBACK(startup_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected", G_CALLBACK(startup_select), page);
    g_object_set_data_full(G_OBJECT(page->ui->root), "startup-page", page, g_free);
    app->pages[PAGE_STARTUP].root = page->ui->root;
    startup_rebuild(page, 0);
    return page->ui->root;
}

void page_startup_update(App *app) {
    StartupPage *page = g_object_get_data(G_OBJECT(app->pages[PAGE_STARTUP].root),
                                          "startup-page");
    if (page == NULL || app->current_page != PAGE_STARTUP) {
        return;
    }
    int signature = startup_signature(app);
    if (signature != page->signature) {
        page->signature = signature;
        startup_rebuild(page, 1);
    }
}
