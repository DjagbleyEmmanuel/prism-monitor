#include "core/common.h"
#include "core/format.h"
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
    int refresh_pending;
} ServicesPage;

static void service_action_clicked(GtkButton *button, gpointer data);

static int service_signature(App *app) {
    int signature = app->view->service_count;
    for (int i = 0; i < app->view->service_count; i++) {
        signature = signature * 31 + app->view->services[i].active;
        signature = signature * 31 + app->view->services[i].enabled;
        signature = signature * 31 + (int)strlen(app->view->services[i].name);
    }
    return signature;
}

static int service_matches(const ServiceInfo *service, const char *filter) {
    if (filter[0] == '\0') {
        return 1;
    }
    if (g_strrstr(service->name, filter) != NULL ||
        g_strrstr(service->description, filter) != NULL) {
        return 1;
    }
    return g_strrstr(service->active_state, filter) != NULL ||
           g_strrstr(service->unit_file, filter) != NULL;
}

static void service_detail(ServicesPage *page, int index) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    manage_detail_clear(ui);
    if (index < 0 || index >= app->view->service_count) {
        manage_detail_heading(ui, "Select a service");
        manage_detail_note(ui, "Choose a unit from the list to inspect its state, "
                               "unit-file status and available controls.");
        return;
    }
    const ServiceInfo *service = &app->view->services[index];
    manage_detail_heading(ui, service->name);
    manage_detail_row(ui, "Description", service->description);
    manage_detail_row(ui, "Load state", service->load);
    manage_detail_row(ui, "Active state", service->active_state);
    manage_detail_row(ui, "Sub state", service->sub_state);
    manage_detail_row(ui, "Unit file", service->unit_file[0] ? service->unit_file : "unknown");
    manage_detail_row(ui, "Startup", service->enabled ? "enabled" : "disabled");
    if (service->main_pid > 0) {
        char pid[32];
        snprintf(pid, sizeof(pid), "%d", service->main_pid);
        manage_detail_row(ui, "Main PID", pid);
    }
    {
        char *log = service_journal(service->name, 30);
        if (log != NULL && log[0] != '\0') {
            /* Show last 8 lines as note */
            char *copy = g_strdup(log);
            /* keep last lines */
            char **lines = g_strsplit(copy, "\n", -1);
            int n = g_strv_length(lines);
            GString *tail = g_string_new("");
            for (int i = n > 8 ? n - 8 : 0; i < n; i++) {
                if (lines[i][0] != '\0') {
                    g_string_append_printf(tail, "%s\n", lines[i]);
                }
            }
            manage_detail_heading(ui, "Journal (last lines)");
            GtkWidget *lbl = prism_label(tail->str, "detail-note");
            gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
            gtk_label_set_selectable(GTK_LABEL(lbl), TRUE);
            gtk_box_append(GTK_BOX(ui->detail), lbl);
            g_string_free(tail, TRUE);
            g_strfreev(lines);
            g_free(copy);
        }
        g_free(log);
    }
    manage_detail_note(ui, "Start, stop, restart and boot-time activation are applied "
                           "through systemd. Administrative units may ask for "
                           "authentication through the desktop policy agent.");

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *start = prism_button("Start", NULL);
    GtkWidget *stop = prism_button("Stop", NULL);
    GtkWidget *restart = prism_button("Restart", NULL);
    GtkWidget *boot = prism_button(service->enabled ? "Disable" : "Enable", NULL);
    gtk_widget_add_css_class(stop, "danger");
    gtk_box_append(GTK_BOX(actions), start);
    gtk_box_append(GTK_BOX(actions), stop);
    gtk_box_append(GTK_BOX(actions), restart);
    gtk_box_append(GTK_BOX(actions), boot);
    g_object_set_data(G_OBJECT(start), "service-page", page);
    g_object_set_data(G_OBJECT(stop), "service-page", page);
    g_object_set_data(G_OBJECT(restart), "service-page", page);
    g_object_set_data(G_OBJECT(boot), "service-page", page);
    g_object_set_data(G_OBJECT(start), "verb", "start");
    g_object_set_data(G_OBJECT(stop), "verb", "stop");
    g_object_set_data(G_OBJECT(restart), "verb", "restart");
    g_object_set_data(G_OBJECT(boot), "verb", service->enabled ? "disable" : "enable");
    g_signal_connect(start, "clicked", G_CALLBACK(service_action_clicked), page);
    g_signal_connect(stop, "clicked", G_CALLBACK(service_action_clicked), page);
    g_signal_connect(restart, "clicked", G_CALLBACK(service_action_clicked), page);
    g_signal_connect(boot, "clicked", G_CALLBACK(service_action_clicked), page);
}

static void service_action_clicked(GtkButton *button, gpointer data) {
    ServicesPage *page = data;
    int index = page->selected;
    if (index < 0 || index >= page->app->view->service_count) {
        return;
    }
    const char *verb = g_object_get_data(G_OBJECT(button), "verb");
    const ServiceInfo *service = &page->app->view->services[index];
    service_action(service->name, verb);
    app_notify(page->app, "%s %s requested", service->name, verb);
    sampler_invalidate_aux(page->app->sampler);
    page->refresh_pending = 1;
}

static void service_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    ServicesPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    service_detail(page, page->selected);
}

static void service_rebuild(ServicesPage *page, int keep_selection) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    int previous = page->selected;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < app->view->service_count; i++) {
        const ServiceInfo *service = &app->view->services[i];
        if (!service_matches(service, page->filter)) {
            continue;
        }
        int state = service->active ? 1 : (service->enabled ? 3 : 0);
        const char *badge = service->active ? "running" :
                            (service->enabled ? "enabled" : "stopped");
        manage_add_row(ui, i, service->name, service->description, badge, state, 0);
        if (i == previous && keep_selection) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d of %d units%s%s", shown,
             app->view->service_count,
             page->filter[0] ? "  \xc2\xb7  filter " : "",
             page->filter[0] ? page->filter : "");
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0);
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox), first);
    } else {
        page->selected = -1;
        service_detail(page, -1);
    }
}

static void service_filter_changed(GtkSearchEntry *entry, gpointer data) {
    ServicesPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    page->signature = -1;
    service_rebuild(page, 0);
}

static void service_run_clicked(GtkButton *button, gpointer data) {
    (void)button;
    ServicesPage *page = data;
    manage_command_dialog(page->app, "Run a new task",
                          "The command runs in your session through the system shell.",
                          "", NULL, NULL);
}

static void service_refresh_clicked(GtkButton *button, gpointer data) {
    (void)button;
    ServicesPage *page = data;
    page->signature = -1;
    sampler_invalidate_aux(page->app->sampler);
    app_notify(page->app, "Service inventory refresh queued");
}

GtkWidget *page_services_create(App *app) {
    ServicesPage *page = g_new0(ServicesPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter services...", 370);

    GtkWidget *run = manage_toolbar_button(page->ui, "Run task", NULL,
                                           G_CALLBACK(service_run_clicked), page);
    (void)run;
    manage_toolbar_button(page->ui, "Refresh", NULL,
                          G_CALLBACK(service_refresh_clicked), page);
    g_signal_connect(page->ui->search, "search-changed",
                     G_CALLBACK(service_filter_changed), page);
    g_signal_connect(page->ui->listbox, "row-selected",
                     G_CALLBACK(service_select), page);

    g_object_set_data_full(G_OBJECT(page->ui->root), "services-page", page, g_free);
    app->pages[PAGE_SERVICES].root = page->ui->root;
    service_rebuild(page, 0);
    return page->ui->root;
}

void page_services_update(App *app) {
    ServicesPage *page = g_object_get_data(
        G_OBJECT(app->pages[PAGE_SERVICES].root), "services-page");
    if (page == NULL || app->current_page != PAGE_SERVICES) {
        return;
    }
    int signature = service_signature(app);
    if (signature != page->signature || page->refresh_pending) {
        page->signature = signature;
        page->refresh_pending = 0;
        service_rebuild(page, 1);
    }
}
