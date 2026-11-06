#ifndef PRISM_DISK_H
#define PRISM_DISK_H

#include "core/parse.h"
#include "core/types.h"

#include <glib.h>

typedef struct {
    DiskStatRaw prev[PRISM_MAX_DISKS * 4];
    double prev_bytes_read[PRISM_MAX_DISKS * 4];
    double prev_bytes_written[PRISM_MAX_DISKS * 4];
    double prev_time;
    int prev_count;
    int initialized;
} DiskReader;

void disk_reader_init(DiskReader *reader);
void disk_read(DiskReader *reader, Snapshot *snap, double now, double elapsed);
void mounts_read(Snapshot *snap);
gboolean disk_parse_smart(const char *text, DiskInfo *disk);
void disk_smart_read(DiskInfo *disk, const char *device);

#endif
