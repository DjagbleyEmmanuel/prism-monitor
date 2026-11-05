#include "sys/sensors.h"

#include "core/common.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static void add_sensor(Snapshot *snap, const char *chip, const char *label,
                       int kind, double value, double min, double max,
                       const char *unit) {
    if (snap->sensor_count >= PRISM_MAX_SENSORS) {
        return;
    }
    SensorInfo *sensor = &snap->sensors[snap->sensor_count++];
    memset(sensor, 0, sizeof(*sensor));
    g_strlcpy(sensor->chip, chip, sizeof(sensor->chip));
    g_strlcpy(sensor->label, label, sizeof(sensor->label));
    g_strlcpy(sensor->unit, unit, sizeof(sensor->unit));
    sensor->kind = kind;
    sensor->value = value;
    sensor->min = min;
    sensor->max = max;
}

static double read_number(const char *path, int *ok) {
    char *text = prism_read_first_line(path);
    if (text == NULL) {
        if (ok != NULL) {
            *ok = 0;
        }
        return 0;
    }
    double value = strtod(text, NULL);
    g_free(text);
    if (ok != NULL) {
        *ok = 1;
    }
    return value;
}

void battery_read(BatteryInfo *out) {
    static gint64 last_us = 0;
    static BatteryInfo cached;
    gint64 now = g_get_monotonic_time();
    if (last_us != 0 && now - last_us < 15 * G_USEC_PER_SEC) {
        *out = cached;
        return;
    }
    memset(out, 0, sizeof(*out));
    GDir *dir = g_dir_open("/sys/class/power_supply", 0, NULL);
    if (dir == NULL) {
        return;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (strncmp(name, "BAT", 3) != 0) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", name);
        char *type = prism_read_first_line(path);
        gboolean battery = type == NULL || g_strcmp0(type, "Battery") == 0;
        g_free(type);
        if (!battery) {
            continue;
        }
        out->present = 1;
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", name);
        int ok = 0;
        out->capacity_pct = read_number(path, &ok);
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", name);
        char *status = prism_read_first_line(path);
        if (status != NULL) {
            g_strlcpy(out->status, status, sizeof(out->status));
            g_free(status);
        }
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/model_name", name);
        char *model = prism_read_first_line(path);
        if (model != NULL) {
            g_strlcpy(out->model, model, sizeof(out->model));
            g_free(model);
        }
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/power_now", name);
        double power_uw = read_number(path, &ok);
        if (!ok) {
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/current_now", name);
            double current_ua = read_number(path, NULL);
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/voltage_now", name);
            double voltage_uv = read_number(path, NULL);
            power_uw = current_ua * voltage_uv / 1000000.0;
        }
        out->power_w = power_uw / 1000000.0;
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_now", name);
        double energy_uw = read_number(path, &ok);
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_full", name);
        double full_uw = read_number(path, &ok);
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_full_design", name);
        double design_uw = read_number(path, NULL);
        if (ok) {
            out->energy_now_wh = energy_uw / 1000000.0;
            out->energy_full_wh = full_uw / 1000000.0;
            if (design_uw > 0) {
                out->health_pct = prism_clamp(full_uw / design_uw * 100.0, 0, 200);
            }
        } else {
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_now", name);
            double charge_now = read_number(path, &ok);
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_full", name);
            double charge_full = read_number(path, &ok);
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/voltage_now", name);
            double voltage = read_number(path, NULL) / 1000000.0;
            out->energy_now_wh = charge_now / 1000000.0 * voltage;
            out->energy_full_wh = charge_full / 1000000.0 * voltage;
        }
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/voltage_now", name);
        out->voltage_v = read_number(path, NULL) / 1000000.0;
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/cycle_count", name);
        out->cycles = (int)read_number(path, NULL);
        if (g_strcmp0(out->status, "Discharging") == 0 && out->power_w > 0.001) {
            out->time_remaining_s = out->energy_now_wh / out->power_w * 3600.0;
        } else if (g_strcmp0(out->status, "Charging") == 0 && out->power_w > 0.001) {
            out->time_remaining_s =
                (out->energy_full_wh - out->energy_now_wh) / out->power_w * 3600.0;
        }
        break;
    }
    g_dir_close(dir);
    cached = *out;
    last_us = now;
}

void sensors_read(Snapshot *snap) {
    snap->sensor_count = 0;
    GDir *dir = g_dir_open("/sys/class/hwmon", 0, NULL);
    if (dir != NULL) {
        const char *entry;
        while ((entry = g_dir_read_name(dir)) != NULL) {
            char path[512];
            char chip_path[512];
            snprintf(chip_path, sizeof(chip_path), "/sys/class/hwmon/%s/name", entry);
            char *chip = prism_read_first_line(chip_path);
            if (chip == NULL) {
                continue;
            }
            static const char *prefixes[] = {"temp", "fan", "in", "power", "curr"};
            guint64 present[PRISM_ARRAY_LEN(prefixes)];
            memset(present, 0, sizeof(present));
            char dir_path[512];
            snprintf(dir_path, sizeof(dir_path), "/sys/class/hwmon/%s", entry);
            GDir *files = g_dir_open(dir_path, 0, NULL);
            if (files != NULL) {
                const char *file;
                while ((file = g_dir_read_name(files)) != NULL) {
                    for (size_t p = 0; p < PRISM_ARRAY_LEN(prefixes); p++) {
                        size_t plen = strlen(prefixes[p]);
                        if (strncmp(file, prefixes[p], plen) != 0) {
                            continue;
                        }
                        char *end = NULL;
                        long index = strtol(file + plen, &end, 10);
                        if (index >= 1 && index <= 64 && end != NULL &&
                            strcmp(end, "_input") == 0) {
                            present[p] |= (guint64)1 << (index - 1);
                        }
                    }
                }
                g_dir_close(files);
            }
            for (size_t p = 0; p < PRISM_ARRAY_LEN(prefixes); p++) {
                for (int index = 1; index <= 64 && snap->sensor_count < PRISM_MAX_SENSORS; index++) {
                    if ((present[p] & ((guint64)1 << (index - 1))) == 0) {
                        continue;
                    }
                    snprintf(path, sizeof(path), "/sys/class/hwmon/%s/%s%d_input",
                             entry, prefixes[p], index);
                    int ok = 0;
                    double raw = read_number(path, &ok);
                    if (!ok) {
                        continue;
                    }
                    char label[64];
                    char label_path[512];
                    snprintf(label_path, sizeof(label_path),
                             "/sys/class/hwmon/%s/%s%d_label", entry, prefixes[p], index);
                    char *custom = prism_read_first_line(label_path);
                    if (custom != NULL) {
                        g_strlcpy(label, custom, sizeof(label));
                        g_free(custom);
                    } else {
                        snprintf(label, sizeof(label), "%s %d", prefixes[p], index);
                    }
                    double value = raw;
                    double min = 0;
                    double max = 0;
                    const char *unit = "";
                    int kind = SENSOR_OTHER;
                    if (strcmp(prefixes[p], "temp") == 0) {
                        value = raw / 1000.0;
                        min = 0;
                        max = 100;
                        unit = "\xc2\xb0" "C";
                        kind = SENSOR_TEMP;
                        if (value < -40 || value > 150) {
                            continue;
                        }
                        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_max",
                                 entry, index);
                        int has_max = 0;
                        double max_raw = read_number(path, &has_max);
                        if (has_max && max_raw > 0) {
                            max = max_raw / 1000.0;
                        }
                    } else if (strcmp(prefixes[p], "fan") == 0) {
                        min = 0;
                        max = 6000;
                        unit = "RPM";
                        kind = SENSOR_FAN;
                    } else if (strcmp(prefixes[p], "in") == 0) {
                        value = raw / 1000.0;
                        min = 0;
                        max = 2.0;
                        unit = "V";
                        kind = SENSOR_VOLTAGE;
                    } else if (strcmp(prefixes[p], "power") == 0) {
                        value = raw / 1000000.0;
                        min = 0;
                        max = 150;
                        unit = "W";
                        kind = SENSOR_POWER;
                    } else if (strcmp(prefixes[p], "curr") == 0) {
                        value = raw / 1000.0;
                        min = 0;
                        max = 20;
                        unit = "A";
                        kind = SENSOR_CURRENT;
                    }
                    add_sensor(snap, chip, label, kind, value, min, max, unit);
                }
            }
            g_free(chip);
        }
        g_dir_close(dir);
    }
    battery_read(&snap->battery);
}
