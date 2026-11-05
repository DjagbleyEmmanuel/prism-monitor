#include "ui/manage.h"

#include "core/common.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

static const char *state_class(int state) {
    switch (state) {
        case 1:
            return "state-green";
        case 2:
            return "state-red";
        case 3:
            return "state-yellow";
        case 4:
            return "state-accent";
        case 5:
            return "state-purple";
        default:
            return "state-dim";
    }
}

ManagePage *manage_page_new(App *app, const char *placeholder, int detail_width) {
    ManagePage *page = g_new0(ManagePage, 1);
    page->app = app;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 18);
    gtk_widget_set_margin_end(root, 18);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 18);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    page->search = gtk_search_entry_new();
    gtk_widget_set_size_request(page->search, 330, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(page->search),
                                          placeholder != NULL ? placeholder : "Filter...");
    gtk_box_append(GTK_BOX(toolbar), page->search);
    page->count_label = prism_label("", "faint");
    gtk_widget_add_css_class(page->count_label, "subtitle");
    gtk_box_append(GTK_BOX(toolbar), prism_spacer());
    gtk_box_append(GTK_BOX(toolbar), page->count_label);
    page->toolbar = toolbar;
    gtk_box_append(GTK_BOX(root), toolbar);

    GtkWidget *split = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(split, TRUE);
    gtk_widget_set_vexpand(split, TRUE);

    GtkWidget *list_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(list_card, "settings-card");
    gtk_widget_set_hexpand(list_card, TRUE);
    gtk_widget_set_vexpand(list_card, TRUE);
    gtk_widget_set_size_request(list_card, 420, -1);
    page->list_card = list_card;

    page->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(page->listbox), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(page->listbox, "manage-list");
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page->listbox);
    gtk_box_append(GTK_BOX(list_card), scroller);
    gtk_box_append(GTK_BOX(split), list_card);

    GtkWidget *detail_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(detail_card, "settings-card");
    gtk_widget_set_size_request(detail_card, detail_width, -1);
    gtk_widget_set_hexpand(detail_card, FALSE);
    gtk_widget_set_vexpand(detail_card, TRUE);
    page->detail_card = detail_card;

    GtkWidget *detail_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(detail_scroller, TRUE);
    page->detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(detail_scroller), page->detail);
    gtk_box_append(GTK_BOX(detail_card), detail_scroller);
    gtk_box_append(GTK_BOX(split), detail_card);

    gtk_box_append(GTK_BOX(root), split);
    page->root = root;
    return page;
}

GtkWidget *manage_page_root(ManagePage *page) {
    return page->root;
}

void manage_list_clear(ManagePage *page) {
    GtkWidget *child = gtk_widget_get_first_child(page->listbox);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(page->listbox), child);
        child = next;
    }
}

void manage_add_row(ManagePage *page, int index, const char *title,
                    const char *subtitle, const char *badge, int state,
                    int selected) {
    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "record-index", GINT_TO_POINTER(index));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    gtk_widget_set_margin_start(box, 9);
    gtk_widget_set_margin_end(box, 9);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *dot = prism_label("\xe2\x97\x8f", state_class(state));
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), dot);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *name = prism_label(title != NULL ? title : "", "row-title");
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), name);
    if (subtitle != NULL && subtitle[0] != '\0') {
        GtkWidget *sub = prism_label(subtitle, "row-sub");
        gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(text), sub);
    }
    gtk_box_append(GTK_BOX(box), text);

    if (badge != NULL && badge[0] != '\0') {
        GtkWidget *pill = prism_label(badge, "pill");
        gtk_widget_add_css_class(pill, state_class(state));
        gtk_widget_set_valign(pill, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(box), pill);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(page->listbox), row);
    if (selected) {
        gtk_list_box_select_row(GTK_LIST_BOX(page->listbox),
                                GTK_LIST_BOX_ROW(row));
    }
}

int manage_selected(ManagePage *page) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(page->listbox));
    if (row == NULL) {
        return -1;
    }
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "record-index"));
}

void manage_select_index(ManagePage *page, int index) {
    int rows = 0;
    GtkWidget *child = gtk_widget_get_first_child(page->listbox);
    while (child != NULL) {
        int value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "record-index"));
        if (value == index) {
            gtk_list_box_select_row(GTK_LIST_BOX(page->listbox),
                                    GTK_LIST_BOX_ROW(child));
            return;
        }
        rows++;
        child = gtk_widget_get_next_sibling(child);
    }
    (void)rows;
}

void manage_detail_clear(ManagePage *page) {
    GtkWidget *child = gtk_widget_get_first_child(page->detail);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(page->detail), child);
        child = next;
    }
}

void manage_detail_heading(ManagePage *page, const char *text) {
    GtkWidget *heading = prism_label(text, "detail-title");
    gtk_widget_set_margin_bottom(heading, 2);
    gtk_box_append(GTK_BOX(page->detail), heading);
}

GtkWidget *manage_detail_row(ManagePage *page, const char *key, const char *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *left = prism_label(key, "detail-key");
    gtk_widget_set_size_request(left, 118, -1);
    gtk_widget_set_valign(left, GTK_ALIGN_START);
    GtkWidget *right = prism_label(value != NULL && value[0] != '\0' ? value : "\xe2\x80\x94",
                                   "detail-value");
    gtk_label_set_wrap(GTK_LABEL(right), TRUE);
    gtk_label_set_selectable(GTK_LABEL(right), TRUE);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_label_set_xalign(GTK_LABEL(right), 0.0f);
    gtk_box_append(GTK_BOX(row), left);
    gtk_box_append(GTK_BOX(row), right);
    gtk_box_append(GTK_BOX(page->detail), row);
    return row;
}

void manage_detail_note(ManagePage *page, const char *text) {
    GtkWidget *label = prism_label(text, "detail-note");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 4);
    gtk_box_append(GTK_BOX(page->detail), label);
}

GtkWidget *manage_detail_actions(ManagePage *page) {
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(actions, 10);
    gtk_box_append(GTK_BOX(page->detail), actions);
    return actions;
}

GtkWidget *manage_toolbar_button(ManagePage *page, const char *label,
                                 const char *css, GCallback callback,
                                 gpointer data) {
    GtkWidget *button = prism_button(label, css);
    if (callback != NULL) {
        g_signal_connect(button, "clicked", callback, data);
    }
    gtk_box_append(GTK_BOX(page->toolbar), button);
    return button;
}

typedef struct {
    App *app;
    ManageCommandCallback callback;
    gpointer user_data;
    GtkWidget *entry;
} CommandDialog;

static void command_dialog_run(GtkButton *button, gpointer data) {
    (void)button;
    CommandDialog *state = data;
    GtkWidget *window = g_object_get_data(G_OBJECT(button), "window");
    const char *text = gtk_editable_get_text(GTK_EDITABLE(state->entry));
    if (text != NULL && text[0] != '\0' && state->callback != NULL) {
        state->callback(state->app, text, state->user_data);
    }
    gtk_window_destroy(GTK_WINDOW(window));
}

void manage_command_dialog(App *app, const char *title, const char *hint,
                           const char *initial, ManageCommandCallback callback,
                           gpointer data) {
    CommandDialog *state = g_new0(CommandDialog, 1);
    state->app = app;
    state->callback = callback;
    state->user_data = data;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_transient_for(GTK_WINDOW(window), app->window);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 210);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_object_set_data_full(G_OBJECT(window), "command-state", state, g_free);

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *run = gtk_button_new_with_label("Run");
    gtk_widget_add_css_class(run, "suggested-action");
    g_object_set_data(G_OBJECT(run), "window", window);
    state->entry = gtk_entry_new();
    g_signal_connect(run, "clicked", G_CALLBACK(command_dialog_run), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), run);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 22);
    gtk_widget_set_margin_end(content, 22);
    gtk_widget_set_margin_top(content, 18);
    gtk_widget_set_margin_bottom(content, 18);
    GtkWidget *description = prism_label(hint, "dim");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_box_append(GTK_BOX(content), description);
    GtkWidget *entry = state->entry;
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "command --with arguments");
    gtk_editable_set_text(GTK_EDITABLE(entry), initial != NULL ? initial : "");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(content), entry);
    gtk_window_set_child(GTK_WINDOW(window), content);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
}

typedef struct {
    App *app;
    GCallback callback;
    gpointer data;
} ConfirmState;

static void confirm_accept(GtkButton *button, gpointer data) {
    (void)button;
    ConfirmState *state = data;
    GtkWidget *window = g_object_get_data(G_OBJECT(button), "window");
    if (state->callback != NULL) {
        ((void (*)(GtkButton *, gpointer))state->callback)(button, state->data);
    }
    gtk_window_destroy(GTK_WINDOW(window));
}

void manage_confirm_dialog(App *app, const char *title, const char *message,
                           const char *accept, GCallback callback, gpointer data) {
    ConfirmState *state = g_new0(ConfirmState, 1);
    state->app = app;
    state->callback = callback;
    state->data = data;
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_transient_for(GTK_WINDOW(window), app->window);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 470, 200);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_object_set_data_full(G_OBJECT(window), "confirm-state", state, g_free);

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *ok = gtk_button_new_with_label(accept != NULL ? accept : "Continue");
    gtk_widget_add_css_class(ok, "destructive-action");
    g_object_set_data(G_OBJECT(ok), "window", window);
    g_signal_connect(ok, "clicked", G_CALLBACK(confirm_accept), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), ok);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_widget_set_margin_top(content, 22);
    gtk_widget_set_margin_bottom(content, 22);
    GtkWidget *label = prism_label(message, "dim");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(content), label);
    gtk_window_set_child(GTK_WINDOW(window), content);
    gtk_window_present(GTK_WINDOW(window));
}

typedef struct {
    App *app;
    const char *icon;
    const char *title;
    char value[64];
    char sub[160];
    Color accent;
} StatStrip;

static void stat_strip_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                            gpointer data) {
    (void)area;
    StatStrip *strip = data;
    const Theme *t = &strip->app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    Rect rect = {1, 1, width - 2, height - 2};
    draw_card_accent(cr, t, rect, 13, strip->accent, 0.3);
    draw_icon(cr, strip->icon, (Rect){15, 13, 14, 14}, strip->accent, 1.6);
    text_draw(cr, layout, strip->title, 36, 20, 9.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);
    text_draw_ellipsis(cr, layout, strip->value, 15, 48, 21, PANGO_WEIGHT_BOLD,
                       t->text, TEXT_LEFT, width - 30);
    text_draw_ellipsis(cr, layout, strip->sub, 15, 68, 8.7, PANGO_WEIGHT_NORMAL,
                       t->faint, TEXT_LEFT, width - 30);
    g_object_unref(layout);
}

GtkWidget *manage_stat_strip(App *app, const char *icon, const char *title,
                             const char *value, const char *sub, Color accent,
                             int height) {
    StatStrip *strip = g_new0(StatStrip, 1);
    strip->app = app;
    strip->icon = icon;
    strip->title = title;
    strip->accent = accent;
    g_strlcpy(strip->value, value != NULL ? value : "--", sizeof(strip->value));
    g_strlcpy(strip->sub, sub != NULL ? sub : "", sizeof(strip->sub));
    GtkWidget *area = prism_area(app, height, stat_strip_draw, strip);
    g_object_set_data_full(G_OBJECT(area), "stat-strip", strip, g_free);
    return area;
}
