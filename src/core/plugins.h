#ifndef PRISM_PLUGINS_H
#define PRISM_PLUGINS_H

#include "core/types.h"

#include <glib.h>

void plugins_dir(char *buf, size_t len);
int plugins_run(Snapshot *snap, const char *custom_dir);
gboolean plugins_parse_output(const char *text, PluginInfo *out);

#endif
