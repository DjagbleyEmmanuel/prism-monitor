#ifndef PRISM_MANAGE_H
#define PRISM_MANAGE_H

#include "ui/app.h"

typedef struct {
    App *app;
    GtkWidget *root;
    GtkWidget *toolbar;
    GtkWidget *search;
    GtkWidget *listbox;
    GtkWidget *list_card;
    GtkWidget *detail;
    GtkWidget *detail_card;
    GtkWidget *count_label;
} ManagePage;

typedef void (*ManageCommandCallback)(App *app, const char *command, gpointer data);

ManagePage *manage_page_new(App *app, const char *placeholder, int detail_width);
GtkWidget *manage_page_root(ManagePage *page);
void manage_list_clear(ManagePage *page);
void manage_add_row(ManagePage *page, int index, const char *title,
                    const char *subtitle, const char *badge, int state,
                    int selected);
int manage_selected(ManagePage *page);
void manage_select_index(ManagePage *page, int index);
void manage_detail_clear(ManagePage *page);
void manage_detail_heading(ManagePage *page, const char *text);
GtkWidget *manage_detail_row(ManagePage *page, const char *key, const char *value);
void manage_detail_note(ManagePage *page, const char *text);
GtkWidget *manage_detail_actions(ManagePage *page);
GtkWidget *manage_toolbar_button(ManagePage *page, const char *label,
                                 const char *css, GCallback callback,
                                 gpointer data);
void manage_command_dialog(App *app, const char *title, const char *hint,
                           const char *initial, ManageCommandCallback callback,
                           gpointer data);
void manage_confirm_dialog(App *app, const char *title, const char *message,
                           const char *accept, GCallback callback, gpointer data);
GtkWidget *manage_stat_strip(App *app, const char *icon, const char *title,
                             const char *value, const char *sub, Color accent,
                             int height);

#endif
