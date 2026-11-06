#ifndef PRISM_COMMON_H
#define PRISM_COMMON_H

#include <glib.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PRISM_APP_ID "io.prism.Monitor"
#define PRISM_APP_NAME "Prism Monitor"
#define PRISM_VERSION "1.3.0"
#define PRISM_MAINTAINER "Priderock-Elohimmanuel"
#define PRISM_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static inline double prism_clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline double prism_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static inline double prism_ease(double current, double target, double factor) {
    return current + (target - current) * factor;
}

static inline int prism_min_int(int a, int b) {
    return a < b ? a : b;
}

static inline int prism_max_int(int a, int b) {
    return a > b ? a : b;
}

static inline double prism_max_double(double a, double b) {
    return a > b ? a : b;
}

static inline unsigned long long prism_monotonic_ms(void) {
    return (unsigned long long)(g_get_monotonic_time() / 1000);
}

char *prism_read_file(const char *path);
char *prism_read_first_line(const char *path);
void prism_trim(char *text);
const char *prism_basename(const char *path);
void prism_username_for_uid(unsigned int uid, char *out, size_t out_len);
double prism_parse_double_after(const char *text, const char *key);

#endif
