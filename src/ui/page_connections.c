#include "core/common.h"
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
    int mode;
    int selected;
    unsigned int signature;
} ConnectionsPage;

static unsigned int connections_signature(App *app) {
    unsigned int hash = 2166136261u ^ (unsigned int)app->view->connection_count;
    for (int i = 0; i < app->view->connection_count; i++) {
        const ConnectionInfo *conn = &app->view->connections[i];
        hash = (hash ^ (unsigned int)conn->state[0]) * 16777619u;
        hash = (hash ^ (unsigned int)conn->local[0]) * 16777619u;
        hash = (hash ^ (unsigned int)conn->pid) * 16777619u;
        hash = (hash ^ (unsigned int)conn->listening) * 16777619u;
    }
    return hash;
}

static int connection_state_color(const ConnectionInfo *conn) {
    if (strcmp(conn->state, "ESTABLISHED") == 0) {
        return 1;
    }
    if (conn->listening) {
        return 4;
    }
    if (strcmp(conn->state, "CLOSE_WAIT") == 0) {
        return 3;
    }
    if (strcmp(conn->state, "UNCONN") == 0 || strcmp(conn->state, "TIME_WAIT") == 0) {
        return 0;
    }
    return 5;
}

static int connection_matches(const ConnectionInfo *conn, const char *filter, int mode) {
    if (mode == 1 && strcmp(conn->state, "ESTABLISHED") != 0) {
        return 0;
    }
    if (mode == 2 && !conn->listening) {
        return 0;
    }
    if (mode == 3 && strncmp(conn->protocol, "TCP", 3) != 0) {
        return 0;
    }
    if (mode == 4 && strncmp(conn->protocol, "UDP", 3) != 0) {
        return 0;
    }
    if (filter[0] == '\0') {
        return 1;
    }
    char pid[24];
    snprintf(pid, sizeof(pid), "%d", conn->pid);
    return g_strrstr(conn->protocol, filter) != NULL ||
           g_strrstr(conn->local, filter) != NULL ||
           g_strrstr(conn->remote, filter) != NULL ||
           g_strrstr(conn->state, filter) != NULL ||
           g_strrstr(conn->process, filter) != NULL ||
           g_strrstr(pid, filter) != NULL;
}

static void clipboard_copy(App *app, const char *text) {
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(app->window));
    gdk_clipboard_set_text(clipboard, text);
    app_notify(app, "Copied to clipboard");
}

static void connection_copy_local(GtkButton *button, gpointer data) {
    (void)button;
    ConnectionsPage *page = data;
    if (page->selected >= 0 && page->selected < page->app->view->connection_count) {
        clipboard_copy(page->app,
                       page->app->view->connections[page->selected].local);
    }
}

static void connection_copy_remote(GtkButton *button, gpointer data) {
    (void)button;
    ConnectionsPage *page = data;
    if (page->selected >= 0 && page->selected < page->app->view->connection_count) {
        clipboard_copy(page->app,
                       page->app->view->connections[page->selected].remote);
    }
}

static void connection_kill_clicked(GtkButton *button, gpointer data) {
    (void)button;
    ConnectionsPage *page = data;
    if (page->selected < 0 || page->selected >= page->app->view->connection_count) {
        return;
    }
    const ConnectionInfo *conn = &page->app->view->connections[page->selected];
    if (conn->remote[0] == '\0' || strcmp(conn->remote, "0.0.0.0:0") == 0) {
        app_notify(page->app, "No remote endpoint to kill");
        return;
    }
    if (connection_kill(conn) == 0) {
        app_notify(page->app, "Kill requested for %s", conn->remote);
    } else {
        app_notify(page->app, "Cannot kill (need root/cap?)");
    }
    sampler_invalidate_aux(page->app->sampler);
}

static void connection_open_process(GtkButton *button, gpointer data) {
    (void)button;
    ConnectionsPage *page = data;
    if (page->selected < 0 || page->selected >= page->app->view->connection_count) {
        return;
    }
    int pid = page->app->view->connections[page->selected].pid;
    if (pid > 0) {
        page->app->proc_selected_pid = pid;
        app_navigate(page->app, PAGE_PROCESSES);
    }
}

static void connection_detail(ConnectionsPage *page, int index) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    manage_detail_clear(ui);
    if (index < 0 || index >= app->view->connection_count) {
        manage_detail_heading(ui, "Select a socket");
        manage_detail_note(ui, "Inspect local and remote endpoints, owning processes "
                               "and socket state. Filters run locally without "
                               "reading packet contents.");
        return;
    }
    const ConnectionInfo *conn = &app->view->connections[index];
    char title[128];
    snprintf(title, sizeof(title), "%s  %s", conn->protocol, conn->state);
    manage_detail_heading(ui, title);
    manage_detail_row(ui, "Local endpoint", conn->local);
    manage_detail_row(ui, "Remote endpoint", conn->remote);
    manage_detail_row(ui, "Protocol", conn->protocol);
    manage_detail_row(ui, "State", conn->state);
    manage_detail_row(ui, "Listening", conn->listening ? "yes" : "no");
    if (conn->pid > 0) {
        char pid[32];
        snprintf(pid, sizeof(pid), "%d", conn->pid);
        manage_detail_row(ui, "Process", conn->process);
        manage_detail_row(ui, "PID", pid);
    } else {
        manage_detail_row(ui, "Process", "not attributed");
    }
    char inode[32];
    snprintf(inode, sizeof(inode), "%llu", conn->inode);
    manage_detail_row(ui, "Socket inode", inode);
    manage_detail_note(ui, "Connection ownership is resolved from /proc socket inodes "
                           "and is sampled every few seconds to keep overhead low.");

    GtkWidget *actions = manage_detail_actions(ui);
    GtkWidget *local = prism_button("Copy local", NULL);
    GtkWidget *remote = prism_button("Copy remote", NULL);
    GtkWidget *kill = prism_button("Kill connection", "danger");
    gtk_box_append(GTK_BOX(actions), local);
    gtk_box_append(GTK_BOX(actions), remote);
    gtk_box_append(GTK_BOX(actions), kill);
    g_signal_connect(local, "clicked", G_CALLBACK(connection_copy_local), page);
    g_signal_connect(remote, "clicked", G_CALLBACK(connection_copy_remote), page);
    g_signal_connect(kill, "clicked", G_CALLBACK(connection_kill_clicked), page);
    if (conn->pid > 0) {
        GtkWidget *open = prism_button("Open process", NULL);
        gtk_box_append(GTK_BOX(actions), open);
        g_signal_connect(open, "clicked", G_CALLBACK(connection_open_process), page);
    }
}

static void connection_select(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    ConnectionsPage *page = data;
    if (row == NULL) {
        return;
    }
    page->selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
    connection_detail(page, page->selected);
}

static void connection_rebuild(ConnectionsPage *page, int keep) {
    ManagePage *ui = page->ui;
    App *app = page->app;
    int previous = page->selected;
    manage_list_clear(ui);
    int shown = 0;
    int selected_row = -1;
    for (int i = 0; i < app->view->connection_count; i++) {
        const ConnectionInfo *conn = &app->view->connections[i];
        if (!connection_matches(conn, page->filter, page->mode)) {
            continue;
        }
        char title[256];
        snprintf(title, sizeof(title), "%s  %s", conn->protocol, conn->state);
        char subtitle[320];
        if (conn->process[0] != '\0') {
            snprintf(subtitle, sizeof(subtitle), "%s  \xe2\x86\x92  %s  \xc2\xb7  %s [%d]",
                     conn->local, conn->remote, conn->process, conn->pid);
        } else {
            snprintf(subtitle, sizeof(subtitle), "%s  \xe2\x86\x92  %s",
                     conn->local, conn->remote);
        }
        manage_add_row(ui, i, title, subtitle, conn->listening ? "listen" : conn->state,
                       connection_state_color(conn), 0);
        if (i == previous && keep) {
            selected_row = i;
        }
        shown++;
    }
    char text[160];
    snprintf(text, sizeof(text), "%d of %d sockets", shown, app->view->connection_count);
    gtk_label_set_text(GTK_LABEL(ui->count_label), text);
    if (selected_row >= 0) {
        manage_select_index(ui, selected_row);
    } else if (shown > 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui->listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->listbox), 0));
    } else {
        page->selected = -1;
        connection_detail(page, -1);
    }
}

static void connection_filter(GtkSearchEntry *entry, gpointer data) {
    ConnectionsPage *page = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_strlcpy(page->filter, text != NULL ? text : "", sizeof(page->filter));
    g_strstrip(page->filter);
    page->signature = 0;
    connection_rebuild(page, 0);
}

static void connection_mode_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)object;
    (void)pspec;
    ConnectionsPage *page = data;
    page->mode = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    page->signature = 0;
    connection_rebuild(page, 0);
}

static void connection_refresh(GtkButton *button, gpointer data) {
    (void)button;
    ConnectionsPage *page = data;
    sampler_invalidate_aux(page->app->sampler);
    page->signature = 0;
    app_notify(page->app, "Socket inventory refresh queued");
}

GtkWidget *page_connections_create(App *app) {
    ConnectionsPage *page = g_new0(ConnectionsPage, 1);
    page->app = app;
    page->selected = -1;
    page->ui = manage_page_new(app, "Filter address, port, state or process...", 380);

    const char *modes[] = {"All states", "Established", "Listening", "TCP", "UDP", NULL};
    GtkWidget *dropdown = gtk_drop_down_new_from_strings(modes);
    gtk_widget_set_valign(dropdown, GTK_ALIGN_CENTER);
    gtk_box_insert_child_after(GTK_BOX(page->ui->toolbar), dropdown, page->ui->search);
    g_signal_connect(dropdown, "notify::selected",
                     G_CALLBACK(connection_mode_changed), page);
    manage_toolbar_button(page->ui, "Refresh", NULL,
                          G_CALLBACK(connection_refresh), page);
    g_signal_connect(page->ui->search, "search-changed",
                     G_CALLBACK(connection_filter), page);
    g_signal_connect(page->ui->listbox, "row-selected",
                     G_CALLBACK(connection_select), page);

    g_object_set_data_full(G_OBJECT(page->ui->root), "connections-page", page, g_free);
    app->pages[PAGE_CONNECTIONS].root = page->ui->root;
    connection_rebuild(page, 0);
    return page->ui->root;
}

void page_connections_update(App *app) {
    ConnectionsPage *page = g_object_get_data(
        G_OBJECT(app->pages[PAGE_CONNECTIONS].root), "connections-page");
    if (page == NULL || app->current_page != PAGE_CONNECTIONS) {
        return;
    }
    unsigned int signature = connections_signature(app);
    if (signature != page->signature) {
        page->signature = signature;
        connection_rebuild(page, 1);
    }
}
