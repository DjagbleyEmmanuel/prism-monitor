#ifndef PRISM_CONFIG_H
#define PRISM_CONFIG_H

typedef struct {
    int theme;
    int accent;
    double interval;
    int animations;
    int kernel_threads;
    int compact;
    int language;
    char font_family[64];
    double font_scale;
    int temp_unit;
    int popout;
    int fps;
    int quality;
    double history_multiplier;
    int pixels_per_update;
    double smoothing;
    int graph_fill;
    int graph_grid;
    int graph_glow;
    int confirm_end;
    int process_interval;
    int auto_record;
    int column_visibility;
    int sidebar_collapsed;
    int alerts_enabled;
    double alert_temp_c;
    double alert_disk_pct;
    double alert_mem_pct;
    double alert_battery_pct;
    int window_width;
    int window_height;
    int current_page;
} Config;

void config_defaults(Config *config);
void config_load(Config *config);
void config_save(const Config *config);
const char *config_path(void);

#endif
