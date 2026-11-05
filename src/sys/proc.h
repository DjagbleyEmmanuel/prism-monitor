#ifndef PRISM_PROC_H
#define PRISM_PROC_H

#include "core/types.h"

typedef struct {
    int pid;
    unsigned long long ticks;
    unsigned char kernel_thread;
} ProcPrev;

typedef struct {
    unsigned int uid;
    char name[32];
} ProcUserCache;

typedef struct {
    ProcPrev prev[8192];
    int prev_count;
    ProcUserCache users[64];
    int user_count;
    int initialized;
    long ticks_per_second;
    long page_size;
} ProcReader;

void proc_reader_init(ProcReader *reader);
void proc_read(ProcReader *reader, Snapshot *snap, double elapsed);
int proc_details(int pid, ProcDetails *out);
int proc_signal(int pid, int signal_number);

#endif
