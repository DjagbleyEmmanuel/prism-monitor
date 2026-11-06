#ifndef PRISM_HISTORY_H
#define PRISM_HISTORY_H

#include "core/types.h"

Snapshot *snapshot_new(void);
void snapshot_free(Snapshot *snap);
void history_clear(History *hist);
void history_push(History *hist, const Snapshot *snap);
float history_get(const float *series, const History *hist, int ago);
int history_count(const History *hist);
const char *history_path(void);
int history_load(History *hist);
void history_append(const Snapshot *snap);
void history_rotate_if_large(void);

#endif
