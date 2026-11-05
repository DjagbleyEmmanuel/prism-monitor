#ifndef PRISM_GPU_H
#define PRISM_GPU_H

#include "core/types.h"

typedef struct {
    double last_probe;
    int probed;
    GpuInfo cache[PRISM_MAX_GPUS];
    int count;
} GpuReader;

void gpu_reader_init(GpuReader *reader);
void gpu_read(GpuReader *reader, Snapshot *snap, double now);

#endif
