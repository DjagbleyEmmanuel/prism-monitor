#include "parse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_cpu_line(const char *line, CpuTimes *out) {
    const char *p = line + 3;
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    if (*p != ' ' && *p != '\t') {
        return -1;
    }
    unsigned long long values[8] = {0};
    for (int i = 0; i < 8; i++) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '\n') {
            break;
        }
        char *end = NULL;
        values[i] = strtoull(p, &end, 10);
        if (end == p) {
            break;
        }
        p = end;
    }
    out->user = (double)values[0];
    out->nice = (double)values[1];
    out->system = (double)values[2];
    out->idle = (double)values[3];
    out->iowait = (double)values[4];
    out->irq = (double)values[5];
    out->softirq = (double)values[6];
    out->steal = (double)values[7];
    return 0;
}

int parse_cpu_stat(const char *text, CpuTimes *total, CpuTimes *cores,
                   int max_cores, int *core_count) {
    if (text == NULL || total == NULL || cores == NULL || core_count == NULL) {
        return 0;
    }
    int count = 0;
    const char *line = text;
    while (*line != '\0') {
        if (strncmp(line, "cpu", 3) == 0) {
            if (line[3] == ' ' || line[3] == '\t') {
                if (parse_cpu_line(line, total) != 0) {
                    return 0;
                }
            } else if (line[3] >= '0' && line[3] <= '9') {
                if (count < max_cores) {
                    if (parse_cpu_line(line, &cores[count]) == 0) {
                        count++;
                    }
                }
            }
        }
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    *core_count = count;
    return count > 0;
}

static double meminfo_value(const char *text, const char *key, int *found) {
    size_t key_len = strlen(key);
    const char *hit = text;
    while ((hit = strstr(hit, key)) != NULL) {
        if (hit[key_len] == ':' && (hit == text || hit[-1] == '\n')) {
            break;
        }
        hit++;
    }
    if (found != NULL) {
        *found = hit != NULL;
    }
    if (hit == NULL) {
        return 0.0;
    }
    const char *p = strchr(hit + key_len, ':');
    if (p == NULL) {
        return 0.0;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return strtod(p, NULL) / 1024.0;
}

void parse_meminfo(const char *text, MemInfo *out) {
    memset(out, 0, sizeof(*out));
    if (text == NULL) {
        return;
    }
    double cached = meminfo_value(text, "Cached", NULL);
    double reclaimable = meminfo_value(text, "SReclaimable", NULL);
    out->total_mb = meminfo_value(text, "MemTotal", NULL);
    out->free_mb = meminfo_value(text, "MemFree", NULL);
    out->available_mb = meminfo_value(text, "MemAvailable", NULL);
    out->buffers_mb = meminfo_value(text, "Buffers", NULL);
    out->cached_mb = cached + reclaimable;
    out->shared_mb = meminfo_value(text, "Shmem", NULL);
    out->slab_mb = meminfo_value(text, "Slab", NULL);
    out->dirty_mb = meminfo_value(text, "Dirty", NULL);
    out->swap_total_mb = meminfo_value(text, "SwapTotal", NULL);
    out->swap_free_mb = meminfo_value(text, "SwapFree", NULL);
    out->swap_used_mb = out->swap_total_mb - out->swap_free_mb;
    if (out->swap_used_mb < 0) {
        out->swap_used_mb = 0;
    }
    if (out->available_mb <= 0) {
        out->available_mb = out->free_mb + out->buffers_mb + out->cached_mb;
    }
    out->used_mb = out->total_mb - out->free_mb - out->buffers_mb - out->cached_mb;
    if (out->used_mb < 0) {
        out->used_mb = out->total_mb - out->available_mb;
    }
    if (out->used_mb < 0) {
        out->used_mb = 0;
    }
    if (out->total_mb > 0) {
        out->used_pct = out->used_mb / out->total_mb * 100.0;
    }
    if (out->swap_total_mb > 0) {
        out->swap_pct = out->swap_used_mb / out->swap_total_mb * 100.0;
    }
}

void parse_pressure(const char *text, double *avg10, double *avg60,
                    double *avg300, double *full10) {
    if (avg10 != NULL) {
        *avg10 = 0;
    }
    if (avg60 != NULL) {
        *avg60 = 0;
    }
    if (avg300 != NULL) {
        *avg300 = 0;
    }
    if (full10 != NULL) {
        *full10 = 0;
    }
    if (text == NULL) {
        return;
    }
    const char *some = strstr(text, "some ");
    if (some != NULL) {
        const char *v = strstr(some, "avg10=");
        if (v != NULL && avg10 != NULL) {
            *avg10 = strtod(v + 6, NULL);
        }
        v = strstr(some, "avg60=");
        if (v != NULL && avg60 != NULL) {
            *avg60 = strtod(v + 6, NULL);
        }
        v = strstr(some, "avg300=");
        if (v != NULL && avg300 != NULL) {
            *avg300 = strtod(v + 7, NULL);
        }
    }
    const char *full = strstr(text, "full ");
    if (full != NULL) {
        const char *v = strstr(full, "avg10=");
        if (v != NULL && full10 != NULL) {
            *full10 = strtod(v + 6, NULL);
        }
    }
}

int parse_netdev(const char *text, NetDevRaw *out, int max_entries) {
    if (text == NULL || out == NULL) {
        return 0;
    }
    int count = 0;
    const char *line = text;
    while (*line != '\0' && count < max_entries) {
        const char *colon = strchr(line, ':');
        if (colon == NULL) {
            break;
        }
        char name[32];
        size_t name_len = (size_t)(colon - line);
        while (name_len > 0 && (line[0] == ' ' || line[0] == '\t')) {
            line++;
            name_len--;
        }
        if (name_len == 0 || name_len >= sizeof(name)) {
            const char *next = strchr(line, '\n');
            if (next == NULL) {
                break;
            }
            line = next + 1;
            continue;
        }
        memcpy(name, line, name_len);
        name[name_len] = '\0';
        unsigned long long fields[16] = {0};
        const char *p = colon + 1;
        for (int i = 0; i < 16; i++) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0' || *p == '\n') {
                break;
            }
            char *end = NULL;
            fields[i] = strtoull(p, &end, 10);
            if (end == p) {
                break;
            }
            p = end;
        }
        snprintf(out[count].name, sizeof(out[count].name), "%s", name);
        out[count].rx_bytes = fields[0];
        out[count].tx_bytes = fields[8];
        count++;
        const char *next = strchr(colon, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    return count;
}

int parse_diskstats(const char *text, DiskStatRaw *out, int max_entries) {
    if (text == NULL || out == NULL) {
        return 0;
    }
    int count = 0;
    const char *line = text;
    while (*line != '\0' && count < max_entries) {
        char name[32];
        unsigned long long values[11] = {0};
        int parsed = sscanf(line, "%*u %*u %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                            name, &values[0], &values[1], &values[2],
                            &values[3], &values[4], &values[5], &values[6],
                            &values[7], &values[8], &values[9], &values[10]);
        if (parsed >= 8) {
            snprintf(out[count].name, sizeof(out[count].name), "%s", name);
            out[count].reads = values[0];
            out[count].sectors_read = values[2];
            out[count].writes = values[4];
            out[count].sectors_written = values[6];
            count++;
        }
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
    return count;
}

int parse_proc_stat(const char *text, ProcStatRaw *out) {
    memset(out, 0, sizeof(*out));
    if (text == NULL) {
        return 0;
    }
    const char *open = strchr(text, '(');
    const char *close = strrchr(text, ')');
    if (open == NULL || close == NULL || close <= open) {
        return 0;
    }
    out->pid = (int)strtol(text, NULL, 10);
    const char *p = close + 1;
    while (*p == ' ') {
        p++;
    }
    out->state = *p;
    if (out->state != '\0') {
        p++;
    }
    char *copy = strdup(p);
    if (copy == NULL) {
        return 0;
    }
    char *tokens[32] = {0};
    int token_count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(copy, " \t\n", &save);
         tok != NULL && token_count < 32;
         tok = strtok_r(NULL, " \t\n", &save)) {
        tokens[token_count++] = tok;
    }
    if (token_count >= 21) {
        out->ppid = atoi(tokens[0]);
        out->utime = strtoull(tokens[10], NULL, 10);
        out->stime = strtoull(tokens[11], NULL, 10);
        out->priority = atol(tokens[14]);
        out->nice = atol(tokens[15]);
        out->threads = atol(tokens[16]);
        out->starttime = strtoull(tokens[18], NULL, 10);
        out->vsize = strtoull(tokens[19], NULL, 10);
        out->rss_pages = atol(tokens[20]);
    }
    free(copy);
    return token_count >= 21;
}

void parse_loadavg(const char *text, double *load1, double *load5,
                   double *load15) {
    double a = 0, b = 0, c = 0;
    if (text != NULL) {
        sscanf(text, "%lf %lf %lf", &a, &b, &c);
    }
    if (load1 != NULL) {
        *load1 = a;
    }
    if (load5 != NULL) {
        *load5 = b;
    }
    if (load15 != NULL) {
        *load15 = c;
    }
}

double parse_uptime(const char *text) {
    if (text == NULL) {
        return 0;
    }
    return strtod(text, NULL);
}
