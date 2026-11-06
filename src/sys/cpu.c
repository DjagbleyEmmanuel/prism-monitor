#include "sys/cpu.h"

#include "core/common.h"
#include "core/parse.h"

#include <dirent.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double sum_times(const CpuTimes *t) {
    return t->user + t->nice + t->system + t->idle + t->iowait +
           t->irq + t->softirq + t->steal;
}

static double cpu_temp_from_hwmon(void) {
    double best = 0;
    GDir *dir = g_dir_open("/sys/class/hwmon", 0, NULL);
    if (dir == NULL) {
        return 0;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", name);
        char *chip = prism_read_first_line(path);
        if (chip == NULL) {
            continue;
        }
        gboolean relevant = g_strcmp0(chip, "coretemp") == 0 ||
                            g_strcmp0(chip, "k10temp") == 0 ||
                            g_strcmp0(chip, "zenpower") == 0 ||
                            g_strcmp0(chip, "cpu_thermal") == 0;
        g_free(chip);
        if (!relevant) {
            continue;
        }
        for (int i = 1; i <= 32; i++) {
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_input", name, i);
            char *value = prism_read_first_line(path);
            if (value == NULL) {
                continue;
            }
            double celsius = strtod(value, NULL) / 1000.0;
            g_free(value);
            if (celsius > best && celsius < 150.0) {
                best = celsius;
            }
        }
    }
    g_dir_close(dir);
    if (best > 0) {
        return best;
    }
    GDir *thermal = g_dir_open("/sys/class/thermal", 0, NULL);
    if (thermal == NULL) {
        return 0;
    }
    while ((name = g_dir_read_name(thermal)) != NULL) {
        if (strncmp(name, "thermal_zone", 12) != 0) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", name);
        char *type = prism_read_first_line(path);
        if (type == NULL) {
            continue;
        }
        gboolean relevant = strstr(type, "x86_pkg_temp") != NULL ||
                            strstr(type, "cpu") != NULL ||
                            strstr(type, "acpitz") != NULL;
        g_free(type);
        if (!relevant) {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", name);
        char *value = prism_read_first_line(path);
        if (value != NULL) {
            double celsius = strtod(value, NULL) / 1000.0;
            if (celsius > best && celsius < 150.0) {
                best = celsius;
            }
            g_free(value);
        }
    }
    g_dir_close(thermal);
    return best;
}

static double cpu_frequency_mhz(int core_count, CpuInfo *out) {
    double total = 0;
    int counted = 0;
    for (int i = 0; i < core_count && i < PRISM_MAX_CORES; i++) {
        char path[256];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        char *value = prism_read_first_line(path);
        if (value == NULL) {
            continue;
        }
        double khz = strtod(value, NULL);
        g_free(value);
        if (khz > 0) {
            out->core_freq_mhz[i] = khz / 1000.0;
            total += khz / 1000.0;
            counted++;
        }
    }
    out->core_freq_count = counted;
    if (counted > 0) {
        return total / counted;
    }
    char *info = prism_read_file("/proc/cpuinfo");
    if (info != NULL) {
        const char *p = info;
        while ((p = strstr(p, "cpu MHz")) != NULL) {
            const char *colon = strchr(p, ':');
            if (colon != NULL) {
                double mhz = strtod(colon + 1, NULL);
                if (mhz > 0) {
                    g_free(info);
                    return mhz;
                }
            }
            p += 7;
        }
        g_free(info);
    }
    return 0;
}

static void read_cpu_strings(CpuReader *reader, CpuInfo *out) {
    if (!reader->strings_ready) {
        if (reader->model[0] == '\0') {
            char *info = prism_read_file("/proc/cpuinfo");
            if (info != NULL) {
                const char *hit = strstr(info, "model name");
                if (hit != NULL) {
                    const char *colon = strchr(hit, ':');
                    if (colon != NULL) {
                        const char *start = colon + 1;
                        while (*start == ' ' || *start == '\t') {
                            start++;
                        }
                        const char *end = strchr(start, '\n');
                        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
                        if (length >= sizeof(reader->model)) {
                            length = sizeof(reader->model) - 1;
                        }
                        memcpy(reader->model, start, length);
                        reader->model[length] = '\0';
                        prism_trim(reader->model);
                    }
                }
                g_free(info);
            }
        }
        char *value = prism_read_first_line(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        if (value != NULL) {
            g_strlcpy(reader->governor, value, sizeof(reader->governor));
            g_free(value);
        }
        value = prism_read_first_line(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_driver");
        if (value != NULL) {
            g_strlcpy(reader->driver, value, sizeof(reader->driver));
            g_free(value);
        }
        reader->strings_ready = 1;
    }
    g_strlcpy(out->model, reader->model, sizeof(out->model));
    g_strlcpy(out->governor, reader->governor, sizeof(out->governor));
    g_strlcpy(out->driver, reader->driver, sizeof(out->driver));
}

static double cpu_package_power(CpuReader *reader, double elapsed) {
    static const char *paths[] = {
        "/sys/class/powercap/intel-rapl:0/energy_uj",
        "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj"
    };
    char *text = NULL;
    for (size_t i = 0; i < PRISM_ARRAY_LEN(paths) && text == NULL; i++) {
        text = prism_read_first_line(paths[i]);
    }
    if (text == NULL) {
        return 0;
    }
    double energy = strtod(text, NULL);
    g_free(text);
    double watts = 0;
    if (reader->power_ready && elapsed > 0.1 && energy >= reader->last_energy_uj) {
        watts = (energy - reader->last_energy_uj) / 1000000.0 / elapsed;
    }
    reader->last_energy_uj = energy;
    reader->power_ready = 1;
    return watts > 0 ? watts : 0;
}

void cpu_reader_init(CpuReader *reader) {
    memset(reader, 0, sizeof(*reader));
}

void cpu_read(CpuReader *reader, CpuInfo *out, double elapsed) {
    (void)elapsed;
    memset(out, 0, sizeof(*out));
    char *stat = prism_read_file("/proc/stat");
    if (stat == NULL) {
        return;
    }
    CpuTimes total = {0};
    CpuTimes cores[PRISM_MAX_CORES] = {{0}};
    int core_count = 0;
    if (!parse_cpu_stat(stat, &total, cores, PRISM_MAX_CORES, &core_count)) {
        g_free(stat);
        return;
    }
    out->core_count = core_count;
    if (reader->initialized) {
        double delta_total = sum_times(&total) - sum_times(&reader->prev_total);
        if (delta_total > 0) {
            double user = total.user - reader->prev_total.user;
            double nice = total.nice - reader->prev_total.nice;
            double system = total.system - reader->prev_total.system;
            double idle = total.idle - reader->prev_total.idle;
            double iowait = total.iowait - reader->prev_total.iowait;
            double irq = total.irq - reader->prev_total.irq;
            double softirq = total.softirq - reader->prev_total.softirq;
            double steal = total.steal - reader->prev_total.steal;
            out->user_pct = prism_clamp(user / delta_total * 100.0, 0, 100);
            out->nice_pct = prism_clamp(nice / delta_total * 100.0, 0, 100);
            out->system_pct = prism_clamp(system / delta_total * 100.0, 0, 100);
            out->iowait_pct = prism_clamp(iowait / delta_total * 100.0, 0, 100);
            out->irq_pct = prism_clamp((irq + softirq) / delta_total * 100.0, 0, 100);
            out->steal_pct = prism_clamp(steal / delta_total * 100.0, 0, 100);
            out->idle_pct = prism_clamp(idle / delta_total * 100.0, 0, 100);
            double busy = delta_total - idle - iowait;
            out->usage_pct = prism_clamp(busy / delta_total * 100.0, 0, 100);
        }
        for (int i = 0; i < core_count && i < reader->prev_core_count; i++) {
            double core_delta = sum_times(&cores[i]) - sum_times(&reader->prev_cores[i]);
            if (core_delta > 0) {
                double idle = cores[i].idle - reader->prev_cores[i].idle;
                double iowait = cores[i].iowait - reader->prev_cores[i].iowait;
                out->per_core[i] = prism_clamp(
                    (core_delta - idle - iowait) / core_delta * 100.0, 0, 100);
            }
        }
    }
    reader->prev_total = total;
    for (int i = 0; i < core_count && i < PRISM_MAX_CORES; i++) {
        reader->prev_cores[i] = cores[i];
    }
    reader->prev_core_count = core_count;
    reader->initialized = 1;

    const char *line = stat;
    while (*line != '\0') {
        if (strncmp(line, "ctxt ", 5) == 0) {
            out->ctxt = strtoull(line + 5, NULL, 10);
        } else if (strncmp(line, "processes ", 10) == 0) {
            out->forks = strtoull(line + 10, NULL, 10);
        } else if (strncmp(line, "procs_running ", 14) == 0) {
            out->procs_running = strtoull(line + 14, NULL, 10);
        } else if (strncmp(line, "procs_blocked ", 14) == 0) {
            out->procs_blocked = strtoull(line + 14, NULL, 10);
        }
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    g_free(stat);

    char *load = prism_read_first_line("/proc/loadavg");
    if (load != NULL) {
        parse_loadavg(load, &out->load1, &out->load5, &out->load15);
        g_free(load);
    }
    char *pressure = prism_read_file("/proc/pressure/cpu");
    if (pressure != NULL) {
        parse_pressure(pressure, &out->psi_some10, NULL, NULL, &out->psi_full10);
        g_free(pressure);
    }
    out->freq_mhz = cpu_frequency_mhz(core_count, out);
    reader->sample_count++;
    if ((reader->sample_count % 3) == 1 || reader->last_temp <= 0) {
        reader->last_temp = cpu_temp_from_hwmon();
    }
    out->temp_c = reader->last_temp;
    out->package_power_w = cpu_package_power(reader, elapsed);
    read_cpu_strings(reader, out);
}

static int write_sysfs(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    fputs(value, f);
    return fclose(f) == 0 ? 0 : -1;
}

int cpu_set_governor(const char *governor) {
    if (governor == NULL || governor[0] == '\0') {
        return -1;
    }
    /* Validate against known governors to avoid arbitrary sysfs writes */
    static const char *valid[] = {"performance", "powersave", "ondemand",
                                  "conservative", "schedutil", "userspace", NULL};
    int ok = 0;
    for (int i = 0; valid[i] != NULL; i++) {
        if (strcmp(governor, valid[i]) == 0) {
            ok = 1;
            break;
        }
    }
    if (!ok) {
        return -1;
    }
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    return write_sysfs(path, governor);
}

int cpu_set_epp(const char *epp) {
    if (epp == NULL || epp[0] == '\0') {
        return -1;
    }
    static const char *valid[] = {"performance", "balance_performance",
                                  "default", "balance_power", "power", NULL};
    int ok = 0;
    for (int i = 0; valid[i] != NULL; i++) {
        if (strcmp(epp, valid[i]) == 0) {
            ok = 1;
            break;
        }
    }
    if (!ok) {
        return -1;
    }
    return write_sysfs(
        "/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", epp);
}

int cpu_set_turbo(int enabled) {
    return write_sysfs("/sys/devices/system/cpu/intel_pstate/no_turbo",
                       enabled ? "0" : "1");
}

int cpu_set_online(int cpu, int online) {
    if (cpu <= 0 || cpu >= PRISM_MAX_CORES) {
        return -1;
    }
    char path[256];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);
    return write_sysfs(path, online ? "1" : "0");
}
