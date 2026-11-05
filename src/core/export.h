#ifndef PRISM_EXPORT_H
#define PRISM_EXPORT_H

#include "core/types.h"

#include <glib.h>

/* JSON-escape a string (without surrounding quotes). Caller frees. */
char *export_json_escape(const char *text);

/* Full snapshot as JSON. Caller frees. hist may be NULL. */
char *export_snapshot_json(const Snapshot *snap, const History *hist);

/* Write snapshot JSON to path ("-" means stdout). Returns TRUE on success. */
gboolean export_snapshot_json_file(const char *path, const Snapshot *snap,
                                   const History *hist);

/* Convert a Prism TSV recording (text) to CSV. Caller frees. */
char *export_tsv_to_csv(const char *tsv_text);

/* Convert a Prism TSV recording to a JSON array of objects. Caller frees. */
char *export_tsv_to_json(const char *tsv_text);

/* Compute peak/avg for CPU and memory from TSV text.
 * Returns TRUE when at least one data row was found. */
gboolean export_tsv_stats(const char *tsv_text, double *cpu_peak,
                          double *cpu_avg, double *mem_peak, double *mem_avg,
                          int *rows);

#endif
