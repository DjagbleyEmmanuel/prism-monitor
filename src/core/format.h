#ifndef PRISM_FORMAT_H
#define PRISM_FORMAT_H

#include <stddef.h>

void fmt_bytes_parts(double bytes, char *value, size_t value_len,
                     char *unit, size_t unit_len);
const char *fmt_bytes(double bytes, char *buf, size_t buf_len);
const char *fmt_rate(double bytes_per_sec, char *buf, size_t buf_len);
const char *fmt_rate_parts(double bytes_per_sec, char *value, size_t value_len,
                           char *unit, size_t unit_len);
const char *fmt_uptime(double seconds, char *buf, size_t buf_len);
const char *fmt_duration(double seconds, char *buf, size_t buf_len);
const char *fmt_mhz(double mhz, char *buf, size_t buf_len);
const char *fmt_pct(double pct, char *buf, size_t buf_len);
const char *fmt_number(double value, char *buf, size_t buf_len);
const char *fmt_count(unsigned long long value, char *buf, size_t buf_len);
const char *fmt_clock_short(double epoch_seconds, char *buf, size_t buf_len);

#endif
