#include "sys/gpu.h"

#include "core/common.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gpu_reader_store(GpuReader *reader, Snapshot *snap) {
    reader->count = snap->gpu_count;
    for (int i = 0; i < snap->gpu_count; i++) {
        reader->cache[i] = snap->gpus[i];
    }
}

static void gpu_reader_restore(GpuReader *reader, Snapshot *snap) {
    snap->gpu_count = reader->count;
    for (int i = 0; i < reader->count; i++) {
        snap->gpus[i] = reader->cache[i];
    }
}

void gpu_reader_init(GpuReader *reader) {
    memset(reader, 0, sizeof(*reader));
    reader->last_probe = -100;
}

static double parse_csv_number(const char *text) {
    if (text == NULL) {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text == '[' || *text == 'N' || *text == '\0') {
        return 0;
    }
    return strtod(text, NULL);
}

static int gpu_probe_nvidia(Snapshot *snap) {
    gchar *argv[] = {
        "nvidia-smi",
        "--query-gpu=name,utilization.gpu,memory.used,memory.total,"
        "temperature.gpu,power.draw,power.limit,clocks.gr,clocks.mem,"
        "fan.speed",
        "--format=csv,noheader,nounits", NULL};
    gchar *output = NULL;
    GError *err = NULL;
    if (!g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &output, NULL, NULL, &err) ||
        output == NULL) {
        g_free(output);
        g_clear_error(&err);
        return 0;
    }
    int added = 0;
    char *save = NULL;
    char *line = strtok_r(output, "\n", &save);
    while (line != NULL && snap->gpu_count < PRISM_MAX_GPUS) {
        g_strstrip(line);
        if (line[0] == '\0') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char **fields = g_strsplit(line, ",", -1);
        if (fields[0] == NULL) {
            g_strfreev(fields);
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        GpuInfo *gpu = &snap->gpus[snap->gpu_count];
        memset(gpu, 0, sizeof(*gpu));
        gpu->present = 1;
        g_strlcpy(gpu->name, fields[0], sizeof(gpu->name));
        g_strlcpy(gpu->vendor, "NVIDIA", sizeof(gpu->vendor));
        g_strlcpy(gpu->kind, "nvidia", sizeof(gpu->kind));
        gpu->util_pct = parse_csv_number(g_strv_length(fields) > 1 ? fields[1] : NULL);
        gpu->vram_used_mb = parse_csv_number(g_strv_length(fields) > 2 ? fields[2] : NULL);
        gpu->vram_total_mb = parse_csv_number(g_strv_length(fields) > 3 ? fields[3] : NULL);
        gpu->temp_c = parse_csv_number(g_strv_length(fields) > 4 ? fields[4] : NULL);
        gpu->power_w = parse_csv_number(g_strv_length(fields) > 5 ? fields[5] : NULL);
        gpu->power_limit_w = parse_csv_number(g_strv_length(fields) > 6 ? fields[6] : NULL);
        gpu->clock_core_mhz = parse_csv_number(g_strv_length(fields) > 7 ? fields[7] : NULL);
        gpu->clock_mem_mhz = parse_csv_number(g_strv_length(fields) > 8 ? fields[8] : NULL);
        gpu->fan_pct = parse_csv_number(g_strv_length(fields) > 9 ? fields[9] : NULL);
        g_strfreev(fields);
        snap->gpu_count++;
        added++;
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(output);
    return added;
}

static int find_vendor(Snapshot *snap, const char *vendor) {
    for (int i = 0; i < snap->gpu_count; i++) {
        if (strcmp(snap->gpus[i].vendor, vendor) == 0) {
            return i;
        }
    }
    return -1;
}

static void gpu_probe_sysfs(Snapshot *snap) {
    GDir *dir = g_dir_open("/sys/class/drm", 0, NULL);
    if (dir == NULL) {
        return;
    }
    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL && snap->gpu_count < PRISM_MAX_GPUS) {
        if (strncmp(entry, "card", 4) != 0 || strchr(entry, '-') != NULL) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/vendor", entry);
        char *vendor_id = prism_read_first_line(path);
        if (vendor_id == NULL) {
            continue;
        }
        const char *vendor = NULL;
        const char *kind = NULL;
        const char *name = NULL;
        if (strcmp(vendor_id, "0x8086") == 0) {
            vendor = "Intel";
            kind = "intel";
            name = "Intel Integrated Graphics";
        } else if (strcmp(vendor_id, "0x1002") == 0) {
            vendor = "AMD";
            kind = "amd";
            name = "AMD Radeon Graphics";
        } else if (strcmp(vendor_id, "0x10de") == 0) {
            vendor = "NVIDIA";
            kind = "nvidia";
            name = "NVIDIA GPU";
        }
        g_free(vendor_id);
        if (vendor == NULL || find_vendor(snap, vendor) >= 0) {
            continue;
        }
        GpuInfo *gpu = &snap->gpus[snap->gpu_count++];
        memset(gpu, 0, sizeof(*gpu));
        gpu->present = 1;
        g_strlcpy(gpu->vendor, vendor, sizeof(gpu->vendor));
        g_strlcpy(gpu->kind, kind, sizeof(gpu->kind));
        g_strlcpy(gpu->name, name, sizeof(gpu->name));
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/gpu_busy_percent", entry);
        char *busy = prism_read_first_line(path);
        if (busy != NULL) {
            gpu->util_pct = strtod(busy, NULL);
            g_free(busy);
        }
        /* AMD: mem_info, power, fan, clocks */
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/mem_info_vram_used", entry);
        {
            char *v = prism_read_first_line(path);
            if (v != NULL) {
                gpu->vram_used_mb = strtod(v, NULL) / 1048576.0;
                g_free(v);
            }
            snprintf(path, sizeof(path), "/sys/class/drm/%s/device/mem_info_vram_total", entry);
            v = prism_read_first_line(path);
            if (v != NULL) {
                gpu->vram_total_mb = strtod(v, NULL) / 1048576.0;
                g_free(v);
            }
            snprintf(path, sizeof(path), "/sys/class/drm/%s/device/pp_dpm_sclk", entry);
            /* pp_dpm_sclk first line is header; try to find active clock */
            char *clk = prism_read_file(path);
            if (clk != NULL) {
                const char *p = strstr(clk, "*");
                double mhz = 0;
                if (p != NULL) {
                    p = strchr(p, ' ');
                    if (p != NULL) {
                        mhz = strtod(p, NULL);
                    }
                }
                if (mhz > 0) {
                    gpu->clock_core_mhz = mhz;
                }
                g_free(clk);
            }
        }
        /* Intel i915: engine info via debugfs is root-only; try sysfs gt clocks */
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/gt_cur_freq_mhz", entry);
        {
            char *v = prism_read_first_line(path);
            if (v != NULL) {
                gpu->clock_core_mhz = strtod(v, NULL);
                g_free(v);
            }
        }
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/hwmon", entry);
        GDir *hwmon = g_dir_open(path, 0, NULL);
        if (hwmon != NULL) {
            const char *hw;
            while ((hw = g_dir_read_name(hwmon)) != NULL) {
                char temp_path[600];
                snprintf(temp_path, sizeof(temp_path), "%s/%s/temp1_input", path, hw);
                char *temp = prism_read_first_line(temp_path);
                if (temp != NULL && gpu->temp_c <= 0) {
                    gpu->temp_c = strtod(temp, NULL) / 1000.0;
                    g_free(temp);
                } else {
                    g_free(temp);
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s/power1_average", path, hw);
                char *pw = prism_read_first_line(temp_path);
                if (pw == NULL) {
                    snprintf(temp_path, sizeof(temp_path), "%s/%s/power1_input", path, hw);
                    pw = prism_read_first_line(temp_path);
                }
                if (pw != NULL) {
                    gpu->power_w = strtod(pw, NULL) / 1000000.0;
                    g_free(pw);
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s/fan1_input", path, hw);
                char *fan = prism_read_first_line(temp_path);
                if (fan != NULL) {
                    gpu->fan_rpm = strtod(fan, NULL);
                    g_free(fan);
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s/fan1_target", path, hw);
                char *ft = prism_read_first_line(temp_path);
                if (ft != NULL) {
                    double target = strtod(ft, NULL);
                    if (target > 0 && gpu->fan_rpm > 0) {
                        gpu->fan_pct = gpu->fan_rpm / target * 100.0;
                        if (gpu->fan_pct > 100) gpu->fan_pct = 100;
                    }
                    g_free(ft);
                }
            }
            g_dir_close(hwmon);
        }
    }
    g_dir_close(dir);
}

void gpu_read(GpuReader *reader, Snapshot *snap, double now) {
    snap->gpu_count = 0;
    if (reader->probed && now - reader->last_probe < 5.0) {
        gpu_reader_restore(reader, snap);
        return;
    }
    gpu_probe_nvidia(snap);
    gpu_probe_sysfs(snap);
    reader->last_probe = now;
    reader->probed = 1;
    gpu_reader_store(reader, snap);
}
