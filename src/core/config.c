#include "core/config.h"

#include <glib.h>
#include <stdio.h>

void config_defaults(Config *config) {
    config->theme = 0;
    config->accent = -1;
    config->interval = 1.0;
    config->animations = 0;
    config->kernel_threads = 0;
    config->compact = 0;
    config->language = 0;
    snprintf(config->font_family, sizeof(config->font_family), "Ubuntu");
    config->font_scale = 1.0;
    config->temp_unit = 0;
    config->popout = 1;
    config->fps = 20;
    config->quality = 0;
    config->history_multiplier = 1.0;
    config->pixels_per_update = 2;
    config->smoothing = 0.35;
    config->graph_fill = 1;
    config->graph_grid = 1;
    config->graph_glow = 1;
    config->confirm_end = 0;
    config->process_interval = 1;
    config->auto_record = 0;
    config->column_visibility = 1;
    config->sidebar_collapsed = 0;
    config->alerts_enabled = 1;
    config->alert_temp_c = 85.0;
    config->alert_disk_pct = 90.0;
    config->alert_mem_pct = 90.0;
    config->alert_battery_pct = 20.0;
    config->window_width = 1600;
    config->window_height = 1000;
    config->current_page = 0;
    config->alert_hook[0] = '\0';
    config->tray_enabled = 0;
    config->start_minimized = 0;
    config->autostart = 0;
    config->history_persist = 1;
    config->page_mask = (1u << 18) - 1;
    for (int i = 0; i < 18; i++) {
        config->page_order[i] = i;
    }
    config->plugin_dir[0] = '\0';
}

const char *config_path(void) {
    static char *path = NULL;
    if (path == NULL) {
        path = g_build_filename(g_get_user_config_dir(), "prism-monitor",
                                "config.ini", NULL);
    }
    return path;
}

static double config_double(GKeyFile *keyfile, const char *group, const char *key,
                            double fallback) {
    if (!g_key_file_has_key(keyfile, group, key, NULL)) {
        return fallback;
    }
    return g_key_file_get_double(keyfile, group, key, NULL);
}

static int config_int(GKeyFile *keyfile, const char *group, const char *key,
                      int fallback) {
    if (!g_key_file_has_key(keyfile, group, key, NULL)) {
        return fallback;
    }
    return g_key_file_get_integer(keyfile, group, key, NULL);
}

static int config_bool(GKeyFile *keyfile, const char *group, const char *key,
                       int fallback) {
    if (!g_key_file_has_key(keyfile, group, key, NULL)) {
        return fallback;
    }
    return g_key_file_get_boolean(keyfile, group, key, NULL) ? 1 : 0;
}

void config_load(Config *config) {
    config_defaults(config);
    GKeyFile *keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, config_path(), G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(keyfile);
        return;
    }
    config->theme = config_int(keyfile, "appearance", "theme", config->theme);
    config->accent = config_int(keyfile, "appearance", "accent", config->accent);
    if (!g_key_file_has_key(keyfile, "appearance", "accent", NULL)) {
        config->accent = -1;
    }
    config->language = config_int(keyfile, "appearance", "language", config->language);
    char *font = g_key_file_get_string(keyfile, "appearance", "font", NULL);
    if (font != NULL && font[0] != '\0') {
        snprintf(config->font_family, sizeof(config->font_family), "%s", font);
    }
    g_free(font);
    config->font_scale = config_double(keyfile, "appearance", "font_scale",
                                       config->font_scale);
    config->temp_unit = config_int(keyfile, "appearance", "temp_unit",
                                   config->temp_unit);
    config->interval = config_double(keyfile, "behavior", "interval",
                                     config->interval);
    config->animations = config_bool(keyfile, "behavior", "animations",
                                     config->animations);
    config->kernel_threads = config_bool(keyfile, "behavior", "kernel_threads",
                                         config->kernel_threads);
    config->compact = config_bool(keyfile, "behavior", "compact", config->compact);
    config->popout = config_bool(keyfile, "behavior", "popout", config->popout);
    config->confirm_end = config_bool(keyfile, "behavior", "confirm_end",
                                      config->confirm_end);
    config->process_interval = config_int(keyfile, "behavior", "process_interval",
                                          config->process_interval);
    config->auto_record = config_bool(keyfile, "behavior", "auto_record",
                                      config->auto_record);
    config->column_visibility = config_bool(keyfile, "behavior", "column_visibility",
                                            config->column_visibility);
    config->sidebar_collapsed = config_bool(keyfile, "appearance", "sidebar_collapsed",
                                            config->sidebar_collapsed);
    config->alerts_enabled = config_bool(keyfile, "alerts", "enabled",
                                         config->alerts_enabled);
    config->alert_temp_c = config_double(keyfile, "alerts", "temp_c",
                                         config->alert_temp_c);
    config->alert_disk_pct = config_double(keyfile, "alerts", "disk_pct",
                                           config->alert_disk_pct);
    config->alert_mem_pct = config_double(keyfile, "alerts", "mem_pct",
                                          config->alert_mem_pct);
    config->alert_battery_pct = config_double(keyfile, "alerts", "battery_pct",
                                              config->alert_battery_pct);
    config->window_width = config_int(keyfile, "window", "width",
                                      config->window_width);
    config->window_height = config_int(keyfile, "window", "height",
                                       config->window_height);
    config->current_page = config_int(keyfile, "window", "page",
                                      config->current_page);
    {
        char *hook = g_key_file_get_string(keyfile, "alerts", "hook", NULL);
        if (hook != NULL && hook[0] != '\0') {
            g_strlcpy(config->alert_hook, hook, sizeof(config->alert_hook));
        }
        g_free(hook);
        char *pdir = g_key_file_get_string(keyfile, "plugins", "dir", NULL);
        if (pdir != NULL && pdir[0] != '\0') {
            g_strlcpy(config->plugin_dir, pdir, sizeof(config->plugin_dir));
        }
        g_free(pdir);
    }
    config->tray_enabled = config_bool(keyfile, "tray", "enabled",
                                       config->tray_enabled);
    config->start_minimized = config_bool(keyfile, "tray", "start_minimized",
                                          config->start_minimized);
    config->autostart = config_bool(keyfile, "tray", "autostart",
                                    config->autostart);
    config->history_persist = config_bool(keyfile, "performance",
                                          "history_persist",
                                          config->history_persist);
    config->page_mask = (unsigned int)config_int(keyfile, "dashboard",
                                                 "page_mask",
                                                 (int)config->page_mask);
    {
        char *order = g_key_file_get_string(keyfile, "dashboard", "page_order",
                                            NULL);
        if (order != NULL) {
            char *copy = g_strdup(order);
            char *save = NULL;
            char *tok = strtok_r(copy, ",", &save);
            int idx = 0;
            while (tok != NULL && idx < 18) {
                int v = atoi(tok);
                if (v >= 0 && v < 18) {
                    config->page_order[idx++] = v;
                }
                tok = strtok_r(NULL, ",", &save);
            }
            g_free(copy);
        }
        g_free(order);
    }
    config->fps = config_int(keyfile, "performance", "fps", config->fps);
    config->quality = config_int(keyfile, "performance", "quality", config->quality);
    config->history_multiplier = config_double(keyfile, "performance",
                                               "history_multiplier",
                                               config->history_multiplier);
    config->pixels_per_update = config_int(keyfile, "performance",
                                           "pixels_per_update",
                                           config->pixels_per_update);
    config->smoothing = config_double(keyfile, "performance", "smoothing",
                                      config->smoothing);
    config->graph_fill = config_bool(keyfile, "graphs", "fill", config->graph_fill);
    config->graph_grid = config_bool(keyfile, "graphs", "grid", config->graph_grid);
    config->graph_glow = config_bool(keyfile, "graphs", "glow", config->graph_glow);

    config->interval = CLAMP(config->interval, 0.25, 10.0);
    config->theme = CLAMP(config->theme, 0, 15);
    if (config->accent >= 64) {
        config->accent = -1;
    }
    config->language = CLAMP(config->language, 0, 5);
    config->font_scale = CLAMP(config->font_scale, 0.75, 1.5);
    config->temp_unit = CLAMP(config->temp_unit, 0, 1);
    config->fps = CLAMP(config->fps, 10, 60);
    config->quality = CLAMP(config->quality, 0, 3);
    config->history_multiplier = CLAMP(config->history_multiplier, 0.25, 4.0);
    config->pixels_per_update = CLAMP(config->pixels_per_update, 1, 8);
    config->smoothing = CLAMP(config->smoothing, 0.0, 0.9);
    config->process_interval = CLAMP(config->process_interval, 1, 10);
    config->alert_temp_c = CLAMP(config->alert_temp_c, 50.0, 110.0);
    config->alert_disk_pct = CLAMP(config->alert_disk_pct, 50.0, 100.0);
    config->alert_mem_pct = CLAMP(config->alert_mem_pct, 50.0, 100.0);
    config->alert_battery_pct = CLAMP(config->alert_battery_pct, 5.0, 60.0);
    config->window_width = CLAMP(config->window_width, 1120, 7680);
    config->window_height = CLAMP(config->window_height, 720, 4320);
    config->current_page = CLAMP(config->current_page, 0, 17);
    if (config->page_mask == 0) {
        config->page_mask = (1u << 18) - 1;
    }
    config->page_mask &= (1u << 18) - 1;
    /* validate page_order is a permutation-ish list; fill missing */
    {
        int seen[18] = {0};
        int out[18];
        int n = 0;
        for (int i = 0; i < 18; i++) {
            int v = config->page_order[i];
            if (v >= 0 && v < 18 && !seen[v]) {
                seen[v] = 1;
                out[n++] = v;
            }
        }
        for (int v = 0; v < 18 && n < 18; v++) {
            if (!seen[v]) {
                out[n++] = v;
            }
        }
        for (int i = 0; i < 18; i++) {
            config->page_order[i] = out[i];
        }
    }
    g_key_file_free(keyfile);
}

void config_save(const Config *config) {
    GKeyFile *keyfile = g_key_file_new();
    g_key_file_set_comment(keyfile, NULL, NULL,
                           "Prism Monitor preferences", NULL);
    g_key_file_set_integer(keyfile, "appearance", "theme", config->theme);
    g_key_file_set_integer(keyfile, "appearance", "accent", config->accent);
    g_key_file_set_integer(keyfile, "appearance", "language", config->language);
    g_key_file_set_string(keyfile, "appearance", "font", config->font_family);
    g_key_file_set_double(keyfile, "appearance", "font_scale", config->font_scale);
    g_key_file_set_integer(keyfile, "appearance", "temp_unit", config->temp_unit);
    g_key_file_set_double(keyfile, "behavior", "interval", config->interval);
    g_key_file_set_boolean(keyfile, "behavior", "animations", config->animations);
    g_key_file_set_boolean(keyfile, "behavior", "kernel_threads", config->kernel_threads);
    g_key_file_set_boolean(keyfile, "behavior", "compact", config->compact);
    g_key_file_set_boolean(keyfile, "behavior", "popout", config->popout);
    g_key_file_set_boolean(keyfile, "behavior", "confirm_end", config->confirm_end);
    g_key_file_set_integer(keyfile, "behavior", "process_interval",
                           config->process_interval);
    g_key_file_set_boolean(keyfile, "behavior", "auto_record", config->auto_record);
    g_key_file_set_boolean(keyfile, "behavior", "column_visibility",
                           config->column_visibility);
    g_key_file_set_boolean(keyfile, "appearance", "sidebar_collapsed",
                           config->sidebar_collapsed);
    g_key_file_set_integer(keyfile, "performance", "fps", config->fps);
    g_key_file_set_integer(keyfile, "performance", "quality", config->quality);
    g_key_file_set_double(keyfile, "performance", "history_multiplier",
                          config->history_multiplier);
    g_key_file_set_integer(keyfile, "performance", "pixels_per_update",
                           config->pixels_per_update);
    g_key_file_set_double(keyfile, "performance", "smoothing", config->smoothing);
    g_key_file_set_boolean(keyfile, "graphs", "fill", config->graph_fill);
    g_key_file_set_boolean(keyfile, "graphs", "grid", config->graph_grid);
    g_key_file_set_boolean(keyfile, "graphs", "glow", config->graph_glow);
    g_key_file_set_boolean(keyfile, "alerts", "enabled", config->alerts_enabled);
    g_key_file_set_double(keyfile, "alerts", "temp_c", config->alert_temp_c);
    g_key_file_set_double(keyfile, "alerts", "disk_pct", config->alert_disk_pct);
    g_key_file_set_double(keyfile, "alerts", "mem_pct", config->alert_mem_pct);
    g_key_file_set_double(keyfile, "alerts", "battery_pct",
                          config->alert_battery_pct);
    g_key_file_set_integer(keyfile, "window", "width", config->window_width);
    g_key_file_set_integer(keyfile, "window", "height", config->window_height);
    g_key_file_set_integer(keyfile, "window", "page", config->current_page);
    g_key_file_set_string(keyfile, "alerts", "hook", config->alert_hook);
    g_key_file_set_boolean(keyfile, "tray", "enabled", config->tray_enabled);
    g_key_file_set_boolean(keyfile, "tray", "start_minimized",
                           config->start_minimized);
    g_key_file_set_boolean(keyfile, "tray", "autostart", config->autostart);
    g_key_file_set_boolean(keyfile, "performance", "history_persist",
                           config->history_persist);
    g_key_file_set_integer(keyfile, "dashboard", "page_mask",
                           (int)config->page_mask);
    {
        char order[128] = "";
        for (int i = 0; i < 18; i++) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%s%d", i ? "," : "",
                     config->page_order[i]);
            g_strlcat(order, tmp, sizeof(order));
        }
        g_key_file_set_string(keyfile, "dashboard", "page_order", order);
    }
    g_key_file_set_string(keyfile, "plugins", "dir", config->plugin_dir);
    char *directory = g_path_get_dirname(config_path());
    g_mkdir_with_parents(directory, 0700);
    g_free(directory);
    g_key_file_save_to_file(keyfile, config_path(), NULL);
    g_key_file_free(keyfile);
}
