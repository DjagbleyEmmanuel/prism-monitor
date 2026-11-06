#ifndef PRISM_TYPES_H
#define PRISM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define PRISM_HIST 240
#define PRISM_MAX_CORES 64
#define PRISM_MAX_PROCS 4096
#define PRISM_MAX_MOUNTS 24
#define PRISM_MAX_NICS 16
#define PRISM_MAX_SENSORS 128
#define PRISM_MAX_DISKS 24
#define PRISM_MAX_GPUS 4
#define PRISM_MAX_SERVICES 512
#define PRISM_MAX_USERS 16
#define PRISM_MAX_STARTUP 128
#define PRISM_MAX_CONNECTIONS 512
#define PRISM_MAX_APPS 512
#define PRISM_MAX_DRIVERS 256
#define PRISM_MAX_RECORDINGS 64
#define PRISM_MAX_PLUGINS 16

typedef struct {
    double user;
    double nice;
    double system;
    double idle;
    double iowait;
    double irq;
    double softirq;
    double steal;
} CpuTimes;

typedef struct {
    int core_count;
    double usage_pct;
    double user_pct;
    double system_pct;
    double iowait_pct;
    double irq_pct;
    double nice_pct;
    double steal_pct;
    double idle_pct;
    double per_core[PRISM_MAX_CORES];
    double core_freq_mhz[PRISM_MAX_CORES];
    int core_freq_count;
    double freq_mhz;
    double temp_c;
    double package_power_w;
    double load1;
    double load5;
    double load15;
    double psi_some10;
    double psi_full10;
    unsigned long long ctxt;
    unsigned long long forks;
    unsigned long long procs_running;
    unsigned long long procs_blocked;
    char model[96];
    char governor[32];
    char driver[32];
} CpuInfo;

typedef struct {
    double total_mb;
    double used_mb;
    double free_mb;
    double available_mb;
    double cached_mb;
    double buffers_mb;
    double shared_mb;
    double slab_mb;
    double dirty_mb;
    double swap_total_mb;
    double swap_used_mb;
    double swap_free_mb;
    double used_pct;
    double swap_pct;
    double psi_some10;
    double psi_full10;
} MemInfo;

typedef struct {
    int present;
    char name[96];
    char vendor[32];
    char kind[16];
    char uuid[64];
    double util_pct;
    double vram_used_mb;
    double vram_total_mb;
    double temp_c;
    double power_w;
    double power_limit_w;
    double clock_core_mhz;
    double clock_mem_mhz;
    double fan_pct;
    double fan_rpm;
} GpuInfo;

typedef struct {
    char device[32];
    char model[64];
    double size_gb;
    double read_bps;
    double write_bps;
    unsigned long long read_total;
    unsigned long long write_total;
    double temp_c;
    int rotational;
    int smart_available;
    double health_pct;
    double wear_pct;
    unsigned long long media_errors;
    double power_on_hours;
    double data_written_tb;
} DiskInfo;

typedef struct {
    char device[64];
    char mount[128];
    char fs[32];
    double total_mb;
    double used_mb;
    double avail_mb;
    double used_pct;
    int read_only;
} MountInfo;

typedef struct {
    char name[32];
    char state[16];
    char ipv4[64];
    char ipv6[96];
    char mac[24];
    int wireless;
    double rx_bps;
    double tx_bps;
    unsigned long long rx_total;
    unsigned long long tx_total;
    int mtu;
    double wifi_signal_dbm;
    double wifi_freq_mhz;
    double wifi_link_mbps;
} NicInfo;

enum {
    SENSOR_TEMP,
    SENSOR_FAN,
    SENSOR_VOLTAGE,
    SENSOR_POWER,
    SENSOR_CURRENT,
    SENSOR_OTHER
};

typedef struct {
    char chip[32];
    char label[64];
    int kind;
    double value;
    double min;
    double max;
    char unit[16];
} SensorInfo;

typedef struct {
    int present;
    char status[24];
    char model[48];
    double capacity_pct;
    double power_w;
    double energy_now_wh;
    double energy_full_wh;
    double voltage_v;
    double time_remaining_s;
    int cycles;
    double health_pct;
} BatteryInfo;

typedef struct {
    int pid;
    int ppid;
    char name[64];
    char user[32];
    char state;
    double cpu_pct;
    unsigned long long rss_kb;
    unsigned long long vsz_kb;
    int threads;
    int nice;
    unsigned long long start_ticks;
    int kernel_thread;
    unsigned long long io_read_bytes;
    unsigned long long io_write_bytes;
    double io_read_bps;
    double io_write_bps;
    int tree_depth;
} ProcInfo;

typedef struct {
    int pid;
    int ppid;
    char name[64];
    char user[32];
    char state;
    char exe[256];
    char cwd[256];
    char cmdline[512];
    unsigned long long rss_kb;
    unsigned long long vsz_kb;
    int threads;
    int nice;
    int priority;
    double cpu_pct;
    unsigned long long start_ticks;
    unsigned long long io_read_bytes;
    unsigned long long io_write_bytes;
} ProcDetails;

typedef struct {
    char name[64];
    char value[64];
    char unit[16];
} PluginInfo;

typedef struct {
    char name[32];
    char tty[32];
    char host[64];
    int64_t login_time;
} UserInfo;

typedef struct {
    char name[128];
    char exec[256];
    char source[16];
    char file[512];
    int enabled;
} StartupInfo;

typedef struct {
    char name[128];
    char description[192];
    char load[24];
    char active_state[24];
    char sub_state[32];
    char unit_file[24];
    char exec[256];
    int active;
    int enabled;
    int main_pid;
    unsigned long long memory_kb;
} ServiceInfo;

typedef struct {
    char protocol[8];
    char local[96];
    char remote[96];
    char state[24];
    char process[64];
    int pid;
    int listening;
    unsigned long long inode;
} ConnectionInfo;

typedef struct {
    int established;
    int listen;
    int time_wait;
    int close_wait;
    int total;
} TcpInfo;

typedef struct {
    char hostname[128];
    char kernel[128];
    char distro[128];
    char arch[32];
    char desktop[64];
    char cpu_model[96];
    int cpu_cores;
    int cpu_threads;
    double total_mem_mb;
    double boot_time;
    double uptime_s;
} SystemInfo;

typedef struct {
    int64_t timestamp_us;
    double uptime_s;
    CpuInfo cpu;
    MemInfo mem;
    int gpu_count;
    GpuInfo gpus[PRISM_MAX_GPUS];
    int disk_count;
    DiskInfo disks[PRISM_MAX_DISKS];
    int mount_count;
    MountInfo mounts[PRISM_MAX_MOUNTS];
    int nic_count;
    NicInfo nics[PRISM_MAX_NICS];
    double net_rx_bps;
    double net_tx_bps;
    int sensor_count;
    SensorInfo sensors[PRISM_MAX_SENSORS];
    BatteryInfo battery;
    int proc_count;
    ProcInfo procs[PRISM_MAX_PROCS];
    SystemInfo sys;
    int service_count;
    ServiceInfo services[PRISM_MAX_SERVICES];
    int user_count;
    UserInfo users[PRISM_MAX_USERS];
    int startup_count;
    StartupInfo startups[PRISM_MAX_STARTUP];
    int connection_count;
    ConnectionInfo connections[PRISM_MAX_CONNECTIONS];
    TcpInfo tcp;
    double ping_ms;
    int plugin_count;
    PluginInfo plugins[PRISM_MAX_PLUGINS];
} Snapshot;

typedef struct {
    int head;
    int count;
    float cpu_total[PRISM_HIST];
    float cpu_user[PRISM_HIST];
    float cpu_sys[PRISM_HIST];
    float cpu_io[PRISM_HIST];
    float cpu_irq[PRISM_HIST];
    float cpu_nice[PRISM_HIST];
    float cpu_steal[PRISM_HIST];
    float cpu_core[PRISM_MAX_CORES][PRISM_HIST];
    float mem_used[PRISM_HIST];
    float mem_cache[PRISM_HIST];
    float swap[PRISM_HIST];
    float net_rx[PRISM_HIST];
    float net_tx[PRISM_HIST];
    float disk_r[PRISM_HIST];
    float disk_w[PRISM_HIST];
    float gpu_util[PRISM_HIST];
    float gpu_temp[PRISM_HIST];
    float freq[PRISM_HIST];
    float power[PRISM_HIST];
    float temp[PRISM_HIST];
    float load1[PRISM_HIST];
} History;

typedef struct {
    const float *samples;
    int count;
} HistSeries;

#endif
