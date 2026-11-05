#include "format.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void fmt_bytes_parts(double bytes, char *value, size_t value_len,
                     char *unit, size_t unit_len) {
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int index = 0;
    double amount = bytes < 0 ? 0 : bytes;
    while (amount >= 1024.0 && index < 5) {
        amount /= 1024.0;
        index++;
    }
    if (index == 0) {
        snprintf(value, value_len, "%.0f", amount);
    } else if (amount >= 100.0) {
        snprintf(value, value_len, "%.0f", amount);
    } else if (amount >= 10.0) {
        snprintf(value, value_len, "%.1f", amount);
    } else {
        snprintf(value, value_len, "%.2f", amount);
    }
    snprintf(unit, unit_len, "%s", units[index]);
}

const char *fmt_bytes(double bytes, char *buf, size_t buf_len) {
    char value[32];
    char unit[16];
    fmt_bytes_parts(bytes, value, sizeof(value), unit, sizeof(unit));
    snprintf(buf, buf_len, "%s %s", value, unit);
    return buf;
}

const char *fmt_rate(double bytes_per_sec, char *buf, size_t buf_len) {
    char value[32];
    char unit[16];
    fmt_bytes_parts(bytes_per_sec, value, sizeof(value), unit, sizeof(unit));
    snprintf(buf, buf_len, "%s %s/s", value, unit);
    return buf;
}

const char *fmt_rate_parts(double bytes_per_sec, char *value, size_t value_len,
                           char *unit, size_t unit_len) {
    char base[16];
    fmt_bytes_parts(bytes_per_sec, value, value_len, base, sizeof(base));
    snprintf(unit, unit_len, "%s/s", base);
    return value;
}

const char *fmt_uptime(double seconds, char *buf, size_t buf_len) {
    if (seconds < 0) {
        seconds = 0;
    }
    unsigned long long total = (unsigned long long)seconds;
    unsigned long long days = total / 86400;
    unsigned long long hours = (total % 86400) / 3600;
    unsigned long long minutes = (total % 3600) / 60;
    if (days > 0) {
        snprintf(buf, buf_len, "%llud %02lluh %02llum", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(buf, buf_len, "%lluh %02llum", hours, minutes);
    } else {
        snprintf(buf, buf_len, "%llum %02llus", minutes, total % 60);
    }
    return buf;
}

const char *fmt_duration(double seconds, char *buf, size_t buf_len) {
    if (seconds <= 0 || !isfinite(seconds)) {
        snprintf(buf, buf_len, "--");
        return buf;
    }
    unsigned long long total = (unsigned long long)seconds;
    unsigned long long days = total / 86400;
    unsigned long long hours = (total % 86400) / 3600;
    unsigned long long minutes = (total % 3600) / 60;
    if (days > 0) {
        snprintf(buf, buf_len, "%llud %lluh", days, hours);
    } else if (hours > 0) {
        snprintf(buf, buf_len, "%lluh %llum", hours, minutes);
    } else {
        snprintf(buf, buf_len, "%llum", minutes);
    }
    return buf;
}

const char *fmt_mhz(double mhz, char *buf, size_t buf_len) {
    if (mhz >= 1000.0) {
        snprintf(buf, buf_len, "%.2f GHz", mhz / 1000.0);
    } else {
        snprintf(buf, buf_len, "%.0f MHz", mhz);
    }
    return buf;
}

const char *fmt_pct(double pct, char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "%.1f%%", pct);
    return buf;
}

const char *fmt_number(double value, char *buf, size_t buf_len) {
    if (fabs(value) >= 100.0) {
        snprintf(buf, buf_len, "%.0f", value);
    } else if (fabs(value) >= 10.0) {
        snprintf(buf, buf_len, "%.1f", value);
    } else {
        snprintf(buf, buf_len, "%.2f", value);
    }
    return buf;
}

const char *fmt_count(unsigned long long value, char *buf, size_t buf_len) {
    if (value >= 1000000000ULL) {
        snprintf(buf, buf_len, "%.1fB", value / 1000000000.0);
    } else if (value >= 1000000ULL) {
        snprintf(buf, buf_len, "%.1fM", value / 1000000.0);
    } else if (value >= 100000ULL) {
        snprintf(buf, buf_len, "%.0fk", value / 1000.0);
    } else {
        snprintf(buf, buf_len, "%llu", value);
    }
    return buf;
}

const char *fmt_clock_short(double epoch_seconds, char *buf, size_t buf_len) {
    time_t when = (time_t)epoch_seconds;
    struct tm tm_value;
    if (localtime_r(&when, &tm_value) == NULL) {
        snprintf(buf, buf_len, "--:--");
        return buf;
    }
    strftime(buf, buf_len, "%H:%M", &tm_value);
    return buf;
}
