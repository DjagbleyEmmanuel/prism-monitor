#ifndef PRISM_CPU_H
#define PRISM_CPU_H

#include "core/types.h"

typedef struct {
    CpuTimes prev_total;
    CpuTimes prev_cores[PRISM_MAX_CORES];
    int prev_core_count;
    int initialized;
    char model[96];
    char governor[32];
    char driver[32];
    int strings_ready;
    int sample_count;
    double last_temp;
    double last_energy_uj;
    int power_ready;
} CpuReader;

void cpu_reader_init(CpuReader *reader);
void cpu_read(CpuReader *reader, CpuInfo *out, double elapsed);

#endif
