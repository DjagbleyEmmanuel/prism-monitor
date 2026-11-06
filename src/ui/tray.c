#include "ui/tray.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static App *g_app = NULL;
static GDBusConnection *g_bus = NULL;
static guint g_own_id = 0;
static guint g_item_reg = 0;
static guint g_menu_reg = 0;
static gboolean g_active = FALSE;

static const char *menu_labels[] = {
    "Open Prism Monitor",
    "Pause / Resume",
    "Export Snapshot JSON",
    "Export Chart PNG",
    "Diagnostics Bundle",
    "Settings",
    "Exit Prism Monitor",
};
static const int menu_count = 7;

static void tray_activate_action(int id) {
    {
        char *dir = g_build_filename(g_get_user_data_dir(), "prism-monitor", NULL);
        g_mkdir_with_parents(dir, 0700);
        char *logp = g_build_filename(dir, "tray.log", NULL);
        FILE *f = fopen(logp, "a");
        if (f != NULL) {
            fprintf(f, "tray action id=%d\n", id);
            fclose(f);
        }
        g_free(logp);
        g_free(dir);
    }
    if (g_app == NULL) {
        return;
    }
    switch (id) {
        case 1:
            tray_present(g_app);
            break;
        case 2:
            app_set_paused(g_app, !g_app->paused);
            break;
        case 3:
            app_export_snapshot(g_app);
            tray_present(g_app);
            break;
        case 4:
            app_export_chart_png(g_app);
            tray_present(g_app);
            break;
        case 5:
            app_export_diagnostics(g_app);
            break;
        case 6:
            app_open_settings(g_app);
            tray_present(g_app);
            break;
        case 7:
            g_application_quit(G_APPLICATION(g_app->application));
            break;
        default:
            break;
    }
}

void tray_present(App *app) {
    if (app == NULL || app->window == NULL) {
        return;
    }
    gtk_widget_set_visible(app->window, TRUE);
    gtk_window_present(app->window);
    gtk_widget_set_sensitive(app->window, TRUE);
}

gboolean tray_is_active(void) {
    return g_active;
}

/* ---------- DBusMenu ---------- */

static GVariant *menu_props_for(int id) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    if (id == 0) {
        g_variant_builder_add(&b, "{sv}", "children-display", g_variant_new_string("submenu"));
    } else if (id >= 1 && id <= menu_count) {
        g_variant_builder_add(&b, "{sv}", "label",
                              g_variant_new_string(menu_labels[id - 1]));
        g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&b, "{sv}", "visible", g_variant_new_boolean(TRUE));
    }
    return g_variant_builder_end(&b);
}

static GVariant *build_layout(int id, int depth) {
    /* DBusMenu layout: (id, props, children) where children is av of variants
     * each containing a layout tuple (ia{sv}av). See qdbusmenuadaptor_p.h */
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    if (depth > 0 && id == 0) {
        for (int i = 1; i <= menu_count; i++) {
            GVariant *child = build_layout(i, depth - 1);
            GVariant *wrapped = g_variant_new_variant(child);
            g_variant_builder_add_value(&children, wrapped);
        }
    }
    GVariant *props = menu_props_for(id);
    GVariant *children_var = g_variant_builder_end(&children);
    GVariantBuilder tup;
    g_variant_builder_init(&tup, G_VARIANT_TYPE("(ia{sv}av)"));
    g_variant_builder_add(&tup, "i", id);
    g_variant_builder_add_value(&tup, props);
    g_variant_builder_add_value(&tup, children_var);
    GVariant *result = g_variant_builder_end(&tup);
    return result;
}

static void dbusmenu_method_call(GDBusConnection *connection,
                                 const gchar *sender,
                                 const gchar *path,
                                 const gchar *interface_name,
                                 const gchar *method_name,
                                 GVariant *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface_name;
    (void)user_data;
    if (strcmp(method_name, "GetLayout") == 0) {
        gint id = 0;
        gint depth = 0;
        GVariant *prop_names = NULL;
        g_variant_get(parameters, "(iias)", &id, &depth, &prop_names);
        if (depth < 0) {
            depth = 2;
        }
        if (depth > 2) {
            depth = 2;
        }
        GVariant *layout = build_layout(id, depth);
        GVariantBuilder outer;
        g_variant_builder_init(&outer, G_VARIANT_TYPE("(u(ia{sv}av))"));
        g_variant_builder_add(&outer, "u", 1u);
        g_variant_builder_add_value(&outer, layout);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_builder_end(&outer));
    } else if (strcmp(method_name, "GetGroupProperties") == 0) {
        GVariant *ids = g_variant_get_child_value(parameters, 0);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
        GVariantIter iter;
        g_variant_iter_init(&iter, ids);
        gint id;
        while (g_variant_iter_next(&iter, "i", &id)) {
            GVariant *props = menu_props_for(id);
            GVariantBuilder item;
            g_variant_builder_init(&item, G_VARIANT_TYPE("(ia{sv})"));
            g_variant_builder_add(&item, "i", id);
            g_variant_builder_add_value(&item, props);
            g_variant_builder_add_value(&b, g_variant_builder_end(&item));
        }
        g_variant_unref(ids);
        {
            GVariantBuilder outer;
            g_variant_builder_init(&outer, G_VARIANT_TYPE("(a(ia{sv}))"));
            g_variant_builder_add_value(&outer, g_variant_builder_end(&b));
            g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&outer));
        }
    } else if (strcmp(method_name, "Event") == 0) {
        gint id = 0;
        const gchar *event_id = "";
        GVariant *data = NULL;
        guint timestamp = 0;
        g_variant_get(parameters, "(isvu)", &id, &event_id, &data, &timestamp);
        (void)timestamp;
        if (strcmp(event_id, "clicked") == 0) {
            tray_activate_action(id);
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (strcmp(method_name, "GetProperty") == 0) {
        gint id = 0;
        const gchar *name = "";
        g_variant_get(parameters, "(is)", &id, &name);
        GVariant *props = menu_props_for(id);
        GVariant *val = g_variant_lookup_value(props, name, NULL);
        GVariantBuilder outer;
        g_variant_builder_init(&outer, G_VARIANT_TYPE("(v)"));
        if (val != NULL) {
            GVariant *wrapped = g_variant_new_variant(val);
            g_variant_builder_add_value(&outer, wrapped);
        } else {
            GVariant *empty = g_variant_new_string("");
            GVariant *wrapped = g_variant_new_variant(empty);
            g_variant_builder_add_value(&outer, wrapped);
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_builder_end(&outer));
    } else if (strcmp(method_name, "EventGroup") == 0) {
        /* Events: a(isvu) — dispatch each clicked event like Event */
        GVariant *events = g_variant_get_child_value(parameters, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, events);
        gint id;
        const gchar *event_id;
        GVariant *data;
        guint ts;
        while (g_variant_iter_next(&iter, "(isvu)", &id, &event_id, &data, &ts)) {
            if (strcmp(event_id, "clicked") == 0) {
                tray_activate_action(id);
            }
        }
        g_variant_unref(events);
        GVariantBuilder eb;
        g_variant_builder_init(&eb, G_VARIANT_TYPE("ai"));
        GVariantBuilder outer;
        g_variant_builder_init(&outer, G_VARIANT_TYPE("(ai)"));
        g_variant_builder_add_value(&outer, g_variant_builder_end(&eb));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&outer));
    } else if (strcmp(method_name, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)", FALSE));
    } else if (strcmp(method_name, "AboutToShowGroup") == 0) {
        GVariantBuilder e1, e2, outer;
        g_variant_builder_init(&e1, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&e2, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&outer, G_VARIANT_TYPE("(aiai)"));
        g_variant_builder_add_value(&outer, g_variant_builder_end(&e1));
        g_variant_builder_add_value(&outer, g_variant_builder_end(&e2));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&outer));
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method");
    }
}

static GVariant *dbusmenu_get_property(GDBusConnection *connection,
                                       const gchar *sender,
                                       const gchar *path,
                                       const gchar *interface_name,
                                       const gchar *property_name,
                                       GError **error,
                                       gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface_name;
    (void)user_data;
    (void)error;
    if (strcmp(property_name, "Version") == 0) {
        return g_variant_new_uint32(3);
    }
    if (strcmp(property_name, "Status") == 0) {
        return g_variant_new_string("normal");
    }
    if (strcmp(property_name, "TextDirection") == 0) {
        return g_variant_new_string("ltr");
    }
    if (strcmp(property_name, "IconThemePath") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&b, "s", "/usr/share/icons");
        return g_variant_builder_end(&b);
    }
    return NULL;
}

/* ---------- StatusNotifierItem ---------- */

static void sni_method_call(GDBusConnection *connection,
                            const gchar *sender,
                            const gchar *path,
                            const gchar *interface_name,
                            const gchar *method_name,
                            GVariant *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface_name;
    (void)parameters;
    (void)user_data;
    if (strcmp(method_name, "Activate") == 0 ||
        strcmp(method_name, "SecondaryActivate") == 0) {
        if (g_app != NULL) {
            tray_present(g_app);
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (strcmp(method_name, "ContextMenu") == 0) {
        /* Host will query dbusmenu; nothing to do */
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (strcmp(method_name, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method");
    }
}

static GVariant *sni_get_property(GDBusConnection *connection,
                                  const gchar *sender,
                                  const gchar *path,
                                  const gchar *interface_name,
                                  const gchar *property_name,
                                  GError **error,
                                  gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface_name;
    (void)user_data;
    (void)error;
    if (strcmp(property_name, "Category") == 0) {
        return g_variant_new_string("ApplicationStatus");
    }
    if (strcmp(property_name, "Id") == 0) {
        return g_variant_new_string("prism-monitor");
    }
    if (strcmp(property_name, "Title") == 0) {
        return g_variant_new_string("Prism Monitor");
    }
    if (strcmp(property_name, "Status") == 0) {
        return g_variant_new_string("Active");
    }
    if (strcmp(property_name, "WindowId") == 0) {
        return g_variant_new_int32(0);
    }
    if (strcmp(property_name, "IconName") == 0) {
        return g_variant_new_string("io.prism.Monitor");
    }
    if (strcmp(property_name, "IconThemePath") == 0) {
        return g_variant_new_string("/usr/share/icons:/usr/local/share/icons:~/.local/share/icons");
    }
    if (strcmp(property_name, "Menu") == 0) {
        return g_variant_new_object_path("/StatusNotifierItem/Menu");
    }
    if (strcmp(property_name, "IsMenuActive") == 0) {
        return g_variant_new_boolean(FALSE);
    }
    if (strcmp(property_name, "ToolTip") == 0) {
        /* (sa(ss)): title, icon array, subtitle */
        GVariantBuilder icons;
        g_variant_builder_init(&icons, G_VARIANT_TYPE("a(ss)"));
        g_variant_builder_add(&icons, "(ss)", "io.prism.Monitor", "");
        GVariantBuilder tb;
        g_variant_builder_init(&tb, G_VARIANT_TYPE("(sa(ss))"));
        g_variant_builder_add(&tb, "s", "Prism Monitor");
        g_variant_builder_add_value(&tb, g_variant_builder_end(&icons));
        return g_variant_builder_end(&tb);
    }
    return NULL;
}

static const char *sni_xml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='Activate'><arg type='i' name='x'/><arg type='i' name='y'/><arg type='u' name='flags'/></method>"
    "    <method name='Scroll'><arg type='i' name='delta'/><arg type='u' name='orientation'/></method>"
    "    <method name='ContextMenu'><arg type='i' name='x'/><arg type='i' name='y'/></method>"
    "    <method name='SecondaryActivate'><arg type='i' name='x'/><arg type='i' name='y'/><arg type='u' name='flags'/></method>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='IsMenuActive' type='b' access='read'/>"
    "    <property name='ToolTip' type='(sa(ss))' access='read'/>"
    "  </interface>"
    "</node>";

static const char *menu_xml =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'><arg type='i' name='parentId'/><arg type='i' name='recursionDepth'/><arg type='as' name='propertyNames'/><arg name='revision' type='u' direction='out'/><arg name='layout' type='(ia{sv}av)' direction='out'/></method>"
    "    <method name='GetGroupProperties'><arg type='ai' name='ids'/><arg type='as' name='propertyNames'/><arg name='properties' type='a(ia{sv})' direction='out'/></method>"
    "    <method name='GetProperty'><arg type='i' name='id'/><arg type='s' name='name'/><arg name='value' type='v' direction='out'/></method>"
    "    <method name='Event'><arg type='i' name='id'/><arg type='s' name='eventId'/><arg type='v' name='data'/><arg type='u' name='timestamp'/></method>"
    "    <method name='EventGroup'><arg type='a(isvu)' name='events'/><arg name='idErrors' type='ai' direction='out'/></method>"
    "    <method name='AboutToShow'><arg type='i' name='id'/><arg name='needUpdate' type='b' direction='out'/></method>"
    "    <method name='AboutToShowGroup'><arg type='ai' name='ids'/><arg name='updatesNeeded' type='ai' direction='out'/><arg name='idErrors' type='ai' direction='out'/></method>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='TextDirection' type='s' access='read'/>"
    "    <property name='IconThemePath' type='as' access='read'/>"
    "    <signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})' name='updatedProps'/><arg type='a(ias)' name='removedProps'/></signal>"
    "    <signal name='LayoutUpdated'><arg type='u' name='revision'/><arg type='i' name='parent'/></signal>"
    "    <signal name='ItemActivationRequested'><arg type='i' name='id'/><arg type='u' name='timestamp'/></signal>"
    "  </interface>"
    "</node>";

static void on_bus_acquired(GDBusConnection *connection, const gchar *name,
                            gpointer user_data) {
    (void)name;
    (void)user_data;
    g_bus = g_object_ref(connection);
    GError *err = NULL;
    GDBusNodeInfo *sni_info = g_dbus_node_info_new_for_xml(sni_xml, &err);
    if (sni_info == NULL) {
        g_clear_error(&err);
        return;
    }
    GDBusInterfaceVTable sni_vtable = {0};
    sni_vtable.method_call = sni_method_call;
    sni_vtable.get_property = sni_get_property;
    g_item_reg = g_dbus_connection_register_object(
        connection, "/StatusNotifierItem", sni_info->interfaces[0],
        &sni_vtable, NULL, NULL, &err);
    if (g_item_reg == 0) {
        g_clear_error(&err);
    }
    g_dbus_node_info_unref(sni_info);

    GDBusNodeInfo *menu_info = g_dbus_node_info_new_for_xml(menu_xml, &err);
    if (menu_info == NULL) {
        g_clear_error(&err);
        return;
    }
    GDBusInterfaceVTable menu_vtable = {0};
    menu_vtable.method_call = dbusmenu_method_call;
    menu_vtable.get_property = dbusmenu_get_property;
    g_menu_reg = g_dbus_connection_register_object(
        connection, "/StatusNotifierItem/Menu", menu_info->interfaces[0],
        &menu_vtable, NULL, NULL, &err);
    if (g_menu_reg == 0) {
        g_clear_error(&err);
    }
    /* Also register as org.freedesktop.DBus.Menu (alias used by some hosts) */
    GDBusNodeInfo *alias_info = g_dbus_node_info_new_for_xml(menu_xml, &err);
    if (alias_info != NULL) {
        /* Rename interface in-place for second registration */
        alias_info->interfaces[0]->name = g_strdup("org.freedesktop.DBus.Menu");
        GDBusInterfaceVTable alias_vtable = {0};
        alias_vtable.method_call = dbusmenu_method_call;
        alias_vtable.get_property = dbusmenu_get_property;
        guint alias_reg = g_dbus_connection_register_object(
            connection, "/StatusNotifierItem/Menu", alias_info->interfaces[0],
            &alias_vtable, NULL, NULL, &err);
        if (alias_reg == 0) {
            g_clear_error(&err);
        }
        g_dbus_node_info_unref(alias_info);
    } else {
        g_clear_error(&err);
    }
    g_dbus_node_info_unref(menu_info);
    g_active = TRUE;
    /* Register with StatusNotifierWatcher (required by Cinnamon/MATE/GNOME
     * xsystray hosts — owning the bus name alone is not enough). */
    {
        char service[128];
        snprintf(service, sizeof(service),
                 "org.kde.StatusNotifierItem-%d-1", (int)getpid());
        GError *call_err = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            connection, "org.kde.StatusNotifierWatcher",
            "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
            "RegisterStatusNotifierItem", g_variant_new("(s)", service),
            NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &call_err);
        if (result != NULL) {
            g_variant_unref(result);
        }
        g_clear_error(&call_err);
    }
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name,
                             gpointer user_data) {
    (void)connection;
    (void)name;
    (void)user_data;
}

static void on_name_lost(GDBusConnection *connection, const gchar *name,
                         gpointer user_data) {
    (void)connection;
    (void)name;
    (void)user_data;
    g_active = FALSE;
}

void tray_init(App *app) {
    g_app = app;
    char bus_name[128];
    snprintf(bus_name, sizeof(bus_name),
             "org.kde.StatusNotifierItem-%d-1", (int)getpid());
    g_own_id = g_bus_own_name(G_BUS_TYPE_SESSION, bus_name,
                              G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus_acquired, on_name_acquired, on_name_lost,
                              NULL, NULL);
}

void tray_shutdown(App *app) {
    (void)app;
    if (g_bus != NULL && g_item_reg != 0) {
        g_dbus_connection_unregister_object(g_bus, g_item_reg);
        g_item_reg = 0;
    }
    if (g_bus != NULL && g_menu_reg != 0) {
        g_dbus_connection_unregister_object(g_bus, g_menu_reg);
        g_menu_reg = 0;
    }
    if (g_own_id != 0) {
        g_bus_unown_name(g_own_id);
        g_own_id = 0;
    }
    g_clear_object(&g_bus);
    g_active = FALSE;
    g_app = NULL;
}
