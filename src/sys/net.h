#ifndef PRISM_NET_H
#define PRISM_NET_H

#include "core/types.h"

#include <glib.h>

typedef struct {
    char name[32];
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} NetPrev;

typedef struct {
    NetPrev prev[PRISM_MAX_NICS];
    int prev_count;
    int initialized;
} NetReader;

void net_reader_init(NetReader *reader);
void net_read(NetReader *reader, Snapshot *snap, double elapsed);
gboolean net_parse_wireless(const char *text, const char *iface,
                            double *signal_dbm);
gboolean net_parse_ping(const char *text, double *ms);
double net_ping_ms(void);

#endif
