#include "core/plugins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void plugins_dir(char *buf, size_t len) {
    char *dir = g_build_filename(g_get_user_config_dir(), "prism-monitor",
                                 "plugins", NULL);
    g_mkdir_with_parents(dir, 0755);
    g_strlcpy(buf, dir, len);
    g_free(dir);
}

gboolean plugins_parse_output(const char *text, PluginInfo *out) {
    if (text == NULL || out == NULL) {
        return FALSE;
    }
    /* Expected: name=value unit  OR  name value */
    char *copy = g_strdup(text);
    g_strstrip(copy);
    /* strip trailing newline */
    char *nl = strchr(copy, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    char *eq = strchr(copy, '=');
    if (eq != NULL) {
        *eq = '\0';
        g_strlcpy(out->name, copy, sizeof(out->name));
        g_strlcpy(out->value, eq + 1, sizeof(out->value));
        /* split unit */
        char *sp = strrchr(out->value, ' ');
        if (sp != NULL && sp[1] != '\0' && strlen(sp) < 16) {
            g_strlcpy(out->unit, sp + 1, sizeof(out->unit));
            *sp = '\0';
        }
        g_free(copy);
        return out->name[0] != '\0' && out->value[0] != '\0';
    }
    g_free(copy);
    return FALSE;
}

int plugins_run(Snapshot *snap, const char *custom_dir) {
    if (snap == NULL) {
        return 0;
    }
    snap->plugin_count = 0;
    char dir_path[512];
    if (custom_dir != NULL && custom_dir[0] != '\0') {
        g_strlcpy(dir_path, custom_dir, sizeof(dir_path));
    } else {
        plugins_dir(dir_path, sizeof(dir_path));
    }
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (dir == NULL) {
        return 0;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL &&
           snap->plugin_count < PRISM_MAX_PLUGINS) {
        if (name[0] == '.' || name[0] == '_') {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
        if (!g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) {
            continue;
        }
        PluginInfo info;
        memset(&info, 0, sizeof(info));
        /* Default name from filename without extension */
        g_strlcpy(info.name, name, sizeof(info.name));
        char *dot = strrchr(info.name, '.');
        if (dot != NULL) {
            *dot = '\0';
        }
        gchar *argv[] = {path, NULL};
        gchar *out = NULL;
        GError *err = NULL;
        if (!g_spawn_sync(NULL, argv, NULL,
                          G_SPAWN_SEARCH_PATH,
                          NULL, NULL, &out, NULL, NULL, &err)) {
            g_free(out);
            g_clear_error(&err);
            continue;
        }
        if (out != NULL && out[0] != '\0') {
            PluginInfo parsed;
            memset(&parsed, 0, sizeof(parsed));
            if (plugins_parse_output(out, &parsed)) {
                info = parsed;
            } else {
                /* Use first line as value */
                char *copy = g_strdup(out);
                g_strstrip(copy);
                char *nl = strchr(copy, '\n');
                if (nl != NULL) {
                    *nl = '\0';
                }
                g_strlcpy(info.value, copy, sizeof(info.value));
                g_free(copy);
            }
        }
        g_free(out);
        snap->plugins[snap->plugin_count++] = info;
    }
    g_dir_close(dir);
    return snap->plugin_count;
}
