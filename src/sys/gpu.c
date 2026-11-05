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
    const char *command =
        "nvidia-smi --query-gpu=name,utilization.gpu,memory.used,memory.total,"
        "temperature.gpu,power.draw,power.limit,clocks.gr,clocks.mem "
        "--format=csv,noheader,nounits 2>/dev/null";
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        return 0;
    }
    int added = 0;
    char line[1024];
    while (fgets(line, sizeof(line), pipe) != NULL && snap->gpu_count < PRISM_MAX_GPUS) {
        g_strstrip(line);
        if (line[0] == '\0') {
            continue;
        }
        char **fields = g_strsplit(line, ",", -1);
        if (fields[0] == NULL) {
            g_strfreev(fields);
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
        g_strfreev(fields);
        snap->gpu_count++;
        added++;
    }
    pclose(pipe);
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
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/hwmon", entry);
        GDir *hwmon = g_dir_open(path, 0, NULL);
        if (hwmon != NULL) {
            const char *hw;
            while ((hw = g_dir_read_name(hwmon)) != NULL) {
                char temp_path[600];
                snprintf(temp_path, sizeof(temp_path), "%s/%s/temp1_input", path, hw);
                char *temp = prism_read_first_line(temp_path);
                if (temp != NULL) {
                    gpu->temp_c = strtod(temp, NULL) / 1000.0;
                    g_free(temp);
                    break;
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
