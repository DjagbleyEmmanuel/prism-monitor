#include "core/alerts.h"
#include "core/export.h"
#include "core/format.h"
#include "core/history.h"
#include "core/parse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabs((a) - (b)) < (eps))

static void test_format(void) {
    char buffer[64];
    CHECK(strcmp(fmt_bytes(1536, buffer, sizeof(buffer)), "1.50 KiB") == 0);
    CHECK(strcmp(fmt_bytes(1024.0 * 1024.0 * 7.25, buffer, sizeof(buffer)),
                 "7.25 MiB") == 0);
    CHECK(strcmp(fmt_rate(1024.0, buffer, sizeof(buffer)), "1.00 KiB/s") == 0);
    CHECK(strcmp(fmt_mhz(2500, buffer, sizeof(buffer)), "2.50 GHz") == 0);
    CHECK(strcmp(fmt_mhz(800, buffer, sizeof(buffer)), "800 MHz") == 0);
    CHECK(strcmp(fmt_uptime(90061, buffer, sizeof(buffer)), "1d 01h 01m") == 0);
    CHECK(strcmp(fmt_count(1500000, buffer, sizeof(buffer)), "1.5M") == 0);
}

static void test_cpu(void) {
    const char *stat =
        "cpu  100 2 50 800 10 1 2 3 0 0\n"
        "cpu0 25 1 12 200 3 0 1 1 0 0\n"
        "cpu1 25 0 13 200 2 1 0 1 0 0\n"
        "cpu2 25 1 12 200 2 0 1 0 0 0\n"
        "cpu3 25 0 13 200 3 0 0 1 0 0\n"
        "ctxt 12345\n";
    CpuTimes total = {0};
    CpuTimes cores[8] = {{0}};
    int count = 0;
    CHECK(parse_cpu_stat(stat, &total, cores, 8, &count) == 1);
    CHECK(count == 4);
    CHECK_NEAR(total.user, 100, 0.01);
    CHECK_NEAR(total.idle, 800, 0.01);
    CHECK_NEAR(cores[2].system, 12, 0.01);
}

static void test_meminfo(void) {
    const char *text =
        "MemTotal:       24000000 kB\n"
        "MemFree:         1000000 kB\n"
        "MemAvailable:   16000000 kB\n"
        "Buffers:          200000 kB\n"
        "Cached:         16000000 kB\n"
        "SReclaimable:     500000 kB\n"
        "SwapTotal:      10000000 kB\n"
        "SwapFree:        9000000 kB\n";
    MemInfo mem;
    parse_meminfo(text, &mem);
    CHECK_NEAR(mem.total_mb, 23437.5, 0.1);
    CHECK_NEAR(mem.cached_mb, 16113.28125, 0.1);
    CHECK_NEAR(mem.swap_used_mb, 976.5625, 0.1);
    CHECK(mem.used_pct > 0 && mem.used_pct < 100);
}

static void test_netdev(void) {
    const char *text =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "  eth0: 1000 1 0 0 0 0 0 0 2000 2 0 0 0 0 0 0\n"
        "    lo: 10 0 0 0 0 0 0 0 20 0 0 0 0 0 0 0\n";
    NetDevRaw entries[4];
    int count = parse_netdev(text, entries, 4);
    CHECK(count == 2);
    CHECK(strcmp(entries[0].name, "eth0") == 0);
    CHECK(entries[0].rx_bytes == 1000);
    CHECK(entries[0].tx_bytes == 2000);
    CHECK(entries[1].rx_bytes == 10);
}

static void test_diskstats(void) {
    const char *text =
        "   8       0 sda 100 2 300 4 500 6 700 8 0 10 12\n"
        " 259       0 nvme0n1 1 0 8 0 2 0 16 0 0 0 0\n";
    DiskStatRaw entries[4];
    int count = parse_diskstats(text, entries, 4);
    CHECK(count == 2);
    CHECK(strcmp(entries[0].name, "sda") == 0);
    CHECK(entries[0].sectors_read == 300);
    CHECK(entries[0].sectors_written == 700);
    CHECK(entries[1].sectors_written == 16);
}

static void test_procstat(void) {
    const char *text =
        "1234 (weird name) S 1 1234 1234 0 -1 4194560 100 0 0 0 10 20 0 0 "
        "20 0 4 0 12345 1000000 500 18446744073709551615 0 0 0 0 0 0 0 0 0 "
        "0 0 0 0 0\n";
    ProcStatRaw raw;
    CHECK(parse_proc_stat(text, &raw) == 1);
    CHECK(raw.pid == 1234);
    CHECK(raw.ppid == 1);
    CHECK(raw.state == 'S');
    CHECK(raw.utime == 10);
    CHECK(raw.stime == 20);
    CHECK(raw.nice == 0);
    CHECK(raw.threads == 4);
    CHECK(raw.starttime == 12345);
    CHECK(raw.vsize == 1000000);
    CHECK(raw.rss_pages == 500);
}

static void test_pressure(void) {
    const char *text =
        "some avg10=1.25 avg60=0.50 avg300=0.10 total=123\n"
        "full avg10=0.75 avg60=0.20 avg300=0.05 total=45\n";
    double a = 0, b = 0, c = 0, full = 0;
    parse_pressure(text, &a, &b, &c, &full);
    CHECK_NEAR(a, 1.25, 0.001);
    CHECK_NEAR(b, 0.50, 0.001);
    CHECK_NEAR(c, 0.10, 0.001);
    CHECK_NEAR(full, 0.75, 0.001);
}

static void test_history(void) {
    History hist;
    history_clear(&hist);
    Snapshot *snap = snapshot_new();
    for (int i = 0; i < PRISM_HIST + 10; i++) {
        snap->cpu.usage_pct = i;
        snap->mem.used_pct = 50;
        snap->cpu.core_count = 1;
        snap->cpu.per_core[0] = (double)i;
        history_push(&hist, snap);
    }
    CHECK(history_count(&hist) == PRISM_HIST);
    CHECK_NEAR(history_get(hist.cpu_total, &hist, 0), PRISM_HIST + 9, 0.001);
    CHECK_NEAR(history_get(hist.cpu_total, &hist, PRISM_HIST - 1), 10, 0.001);
    snapshot_free(snap);
}

static void test_alerts(void) {
    Snapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.cpu.temp_c = 90;
    snap.mem.used_pct = 95;
    snap.battery.present = 1;
    snap.battery.capacity_pct = 10;
    snap.mount_count = 1;
    snap.mounts[0].used_pct = 99;
    AlertState state;
    alerts_init(&state);
    int fired = alerts_check(&state, &snap, 1, 85, 90, 90, 20, 1000000, 0);
    CHECK((fired & ALERT_TEMP) != 0);
    CHECK((fired & ALERT_DISK) != 0);
    CHECK((fired & ALERT_MEM) != 0);
    CHECK((fired & ALERT_BATTERY) != 0);
    /* cooldown: immediate second check fires nothing */
    CHECK(alerts_check(&state, &snap, 1, 85, 90, 90, 20, 2000000, 0) == 0);
    /* disabled */
    CHECK(alerts_check(&state, &snap, 0, 85, 90, 90, 20, 100000000, 0) == 0);
    CHECK_NEAR(alerts_worst_disk_pct(&snap), 99, 0.001);
}

static void test_export(void) {
    char *e = export_json_escape("a\"b\\c\nd");
    CHECK(e != NULL && strstr(e, "\\\"") != NULL);
    g_free(e);
    Snapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.timestamp_us = 123;
    g_strlcpy(snap.sys.hostname, "host\"x", sizeof(snap.sys.hostname));
    snap.cpu.usage_pct = 42.5;
    snap.mem.used_pct = 33;
    History hist;
    history_clear(&hist);
    char *json = export_snapshot_json(&snap, &hist);
    CHECK(json != NULL && strstr(json, "\"cpu\"") != NULL);
    CHECK(strstr(json, "host\\\"x") != NULL);
    g_free(json);
    const char *tsv =
        "# prism-monitor recording v1\n"
        "# timestamp\tcpu_pct\tmem_pct\tswap_pct\trx_bps\ttx_bps\tdisk_r_bps\t"
        "disk_w_bps\tgpu_pct\tcpu_temp_c\tpower_w\tload1\n"
        "1.000\t10.00\t20.00\t0.00\t100\t200\t300\t400\t5.00\t50.0\t10.00\t0.50\n"
        "2.000\t80.00\t40.00\t1.00\t110\t210\t310\t410\t6.00\t60.0\t11.00\t1.50\n";
    char *csv = export_tsv_to_csv(tsv);
    CHECK(csv != NULL && strstr(csv, "timestamp,cpu_pct") != NULL);
    CHECK(strstr(csv, "80.00") != NULL);
    g_free(csv);
    char *js = export_tsv_to_json(tsv);
    CHECK(js != NULL && strstr(js, "\"cpu_pct\": 80.000") != NULL);
    g_free(js);
    double cp = 0, ca = 0, mp = 0, ma = 0;
    int rows = 0;
    CHECK(export_tsv_stats(tsv, &cp, &ca, &mp, &ma, &rows));
    CHECK(rows == 2);
    CHECK_NEAR(cp, 80, 0.001);
    CHECK_NEAR(ca, 45, 0.001);
    CHECK_NEAR(mp, 40, 0.001);
}

int main(void) {
    test_format();
    test_cpu();
    test_meminfo();
    test_netdev();
    test_diskstats();
    test_procstat();
    test_pressure();
    test_history();
    test_alerts();
    test_export();
    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
