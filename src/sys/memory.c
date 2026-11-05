#include "sys/memory.h"

#include "core/common.h"
#include "core/parse.h"

#include <string.h>

void memory_read(MemInfo *out) {
    memset(out, 0, sizeof(*out));
    char *info = prism_read_file("/proc/meminfo");
    if (info == NULL) {
        return;
    }
    parse_meminfo(info, out);
    g_free(info);
    char *pressure = prism_read_file("/proc/pressure/memory");
    if (pressure != NULL) {
        parse_pressure(pressure, &out->psi_some10, NULL, NULL, &out->psi_full10);
        g_free(pressure);
    }
}
