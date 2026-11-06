#ifndef PRISM_SAMPLER_H
#define PRISM_SAMPLER_H

#include "core/history.h"

#include <glib.h>

typedef struct Sampler Sampler;

Sampler *sampler_new(double interval);
void sampler_free(Sampler *sampler);
void sampler_start(Sampler *sampler);
void sampler_stop(Sampler *sampler);
gboolean sampler_copy(Sampler *sampler, Snapshot *dst, History *hist,
                      guint64 *last_seq);
double sampler_interval(Sampler *sampler);
void sampler_set_interval(Sampler *sampler, double interval);
void sampler_set_process_interval(Sampler *sampler, int seconds);
void sampler_invalidate_aux(Sampler *sampler);
void sampler_set_paused(Sampler *sampler, gboolean paused);
gboolean sampler_is_paused(Sampler *sampler);
void sampler_set_plugin_dir(Sampler *sampler, const char *dir);
int sampler_load_history(Sampler *sampler);

#endif
