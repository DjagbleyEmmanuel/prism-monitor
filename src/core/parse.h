#ifndef PRISM_PARSE_H
#define PRISM_PARSE_H

#include "types.h"

typedef struct {
    char name[32];
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} NetDevRaw;

typedef struct {
    char name[32];
    unsigned long long reads;
    unsigned long long sectors_read;
    unsigned long long writes;
    unsigned long long sectors_written;
} DiskStatRaw;

typedef struct {
    int pid;
    int ppid;
    char state;
    unsigned long long utime;
    unsigned long long stime;
    long priority;
    long nice;
    long threads;
    unsigned long long starttime;
    unsigned long long vsize;
    long rss_pages;
} ProcStatRaw;

int parse_cpu_stat(const char *text, CpuTimes *total, CpuTimes *cores,
                   int max_cores, int *core_count);
void parse_meminfo(const char *text, MemInfo *out);
void parse_pressure(const char *text, double *avg10, double *avg60,
                    double *avg300, double *full10);
int parse_netdev(const char *text, NetDevRaw *out, int max_entries);
int parse_diskstats(const char *text, DiskStatRaw *out, int max_entries);
int parse_proc_stat(const char *text, ProcStatRaw *out);
void parse_loadavg(const char *text, double *load1, double *load5,
                   double *load15);
double parse_uptime(const char *text);

#endif
