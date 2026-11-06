#include "core/sampler.h"

#include "core/common.h"
#include "core/plugins.h"
#include "sys/cpu.h"
#include "sys/disk.h"
#include "sys/gpu.h"
#include "sys/memory.h"
#include "sys/net.h"
#include "sys/proc.h"
#include "sys/sensors.h"
#include "sys/systeminfo.h"

#include <glib.h>
#include <string.h>

struct Sampler {
    GThread *thread;
    GMutex mutex;
    GCond cond;
    gboolean running;
    gboolean stop;
    gboolean paused;
    double interval;
    double last_time;
    double last_users;
    double last_startup;
    double last_services;
    double last_connections;
    double last_tcp;
    double last_procs;
    int proc_interval;
    guint64 seq;
    Snapshot *snapshot;
    Snapshot *work;
    History history;
    CpuReader cpu;
    DiskReader disk;
    NetReader net;
    ProcReader proc;
    GpuReader gpu;
    UserInfo user_cache[PRISM_MAX_USERS];
    int user_cache_count;
    StartupInfo startup_cache[PRISM_MAX_STARTUP];
    int startup_cache_count;
    ServiceInfo service_cache[PRISM_MAX_SERVICES];
    int service_cache_count;
    ConnectionInfo connection_cache[PRISM_MAX_CONNECTIONS];
    int connection_cache_count;
    TcpInfo tcp_cache;
    PluginInfo plugin_cache[PRISM_MAX_PLUGINS];
    int plugin_cache_count;
    double last_plugins;
    char plugin_dir[256];
};

static void sampler_collect(Sampler *sampler, double now) {
    Snapshot *snap = sampler->work;
    memset(snap, 0, sizeof(*snap));
    double elapsed = sampler->last_time > 0 ? now - sampler->last_time : 0;
    snap->timestamp_us = g_get_monotonic_time();
    cpu_read(&sampler->cpu, &snap->cpu, elapsed);
    memory_read(&snap->mem);
    disk_read(&sampler->disk, snap, now, elapsed);
    mounts_read(snap);
    net_read(&sampler->net, snap, elapsed);
    double proc_interval = sampler->proc_interval > 0 ? sampler->proc_interval : 1;
    if (sampler->last_procs <= 0 || now - sampler->last_procs >= proc_interval) {
        proc_read(&sampler->proc, snap, elapsed);
        sampler->last_procs = now;
    } else {
        snap->proc_count = sampler->snapshot->proc_count;
        memcpy(snap->procs, sampler->snapshot->procs, sizeof(snap->procs));
    }
    sensors_read(snap);
    gpu_read(&sampler->gpu, snap, now);
    system_read(&snap->sys);
    snap->uptime_s = snap->sys.uptime_s;

    if (now - sampler->last_users >= 15.0 || sampler->user_cache_count == 0) {
        users_read(snap);
        sampler->user_cache_count = snap->user_count;
        memcpy(sampler->user_cache, snap->users, sizeof(sampler->user_cache));
        sampler->last_users = now;
    } else {
        snap->user_count = sampler->user_cache_count;
        memcpy(snap->users, sampler->user_cache, sizeof(sampler->user_cache));
    }
    if (now - sampler->last_startup >= 120.0 || sampler->startup_cache_count == 0) {
        startup_read(snap);
        sampler->startup_cache_count = snap->startup_count;
        memcpy(sampler->startup_cache, snap->startups, sizeof(sampler->startup_cache));
        sampler->last_startup = now;
    } else {
        snap->startup_count = sampler->startup_cache_count;
        memcpy(snap->startups, sampler->startup_cache, sizeof(sampler->startup_cache));
    }
    if (now - sampler->last_services >= 30.0 || sampler->service_cache_count == 0) {
        services_read(snap);
        sampler->service_cache_count = snap->service_count;
        memcpy(sampler->service_cache, snap->services, sizeof(sampler->service_cache));
        sampler->last_services = now;
    } else {
        snap->service_count = sampler->service_cache_count;
        memcpy(snap->services, sampler->service_cache, sizeof(sampler->service_cache));
    }
    if (now - sampler->last_connections >= 20.0 || sampler->connection_cache_count == 0) {
        connections_read(snap);
        sampler->connection_cache_count = snap->connection_count;
        memcpy(sampler->connection_cache, snap->connections,
               sizeof(sampler->connection_cache));
        sampler->last_connections = now;
    } else {
        snap->connection_count = sampler->connection_cache_count;
        memcpy(snap->connections, sampler->connection_cache,
               sizeof(sampler->connection_cache));
    }
    if (now - sampler->last_tcp >= 8.0 || sampler->tcp_cache.total == 0) {
        tcp_read(&snap->tcp);
        sampler->tcp_cache = snap->tcp;
        sampler->last_tcp = now;
    } else {
        snap->tcp = sampler->tcp_cache;
    }
    if (now - sampler->last_plugins >= 5.0 || sampler->last_plugins == 0) {
        plugins_run(snap, sampler->plugin_dir[0] ? sampler->plugin_dir : NULL);
        sampler->plugin_cache_count = snap->plugin_count;
        memcpy(sampler->plugin_cache, snap->plugins, sizeof(sampler->plugin_cache));
        sampler->last_plugins = now;
    } else {
        snap->plugin_count = sampler->plugin_cache_count;
        memcpy(snap->plugins, sampler->plugin_cache, sizeof(snap->plugins));
    }

    sampler->last_time = now;
    g_mutex_lock(&sampler->mutex);
    Snapshot *published = sampler->snapshot;
    sampler->snapshot = sampler->work;
    sampler->work = published;
    history_push(&sampler->history, sampler->snapshot);
    sampler->seq++;
    g_mutex_unlock(&sampler->mutex);
}

static gpointer sampler_thread(gpointer data) {
    Sampler *sampler = data;
    double now = g_get_monotonic_time() / 1000000.0;
    sampler->last_time = now;
    sampler_collect(sampler, now);
    g_mutex_lock(&sampler->mutex);
    sampler->running = TRUE;
    g_mutex_unlock(&sampler->mutex);

    while (TRUE) {
        g_mutex_lock(&sampler->mutex);
        while (sampler->paused && !sampler->stop) {
            g_cond_wait(&sampler->cond, &sampler->mutex);
        }
        if (sampler->stop) {
            g_mutex_unlock(&sampler->mutex);
            break;
        }
        gint64 deadline = g_get_monotonic_time() +
                          (gint64)(sampler->interval * G_USEC_PER_SEC);
        while (!sampler->stop) {
            gint64 remaining = deadline - g_get_monotonic_time();
            if (remaining <= 0) {
                break;
            }
            g_cond_wait_until(&sampler->cond, &sampler->mutex, deadline);
        }
        gboolean stop = sampler->stop;
        g_mutex_unlock(&sampler->mutex);
        if (stop) {
            break;
        }
        now = g_get_monotonic_time() / 1000000.0;
        sampler_collect(sampler, now);
    }
    return NULL;
}

Sampler *sampler_new(double interval) {
    Sampler *sampler = g_new0(Sampler, 1);
    sampler->interval = prism_clamp(interval, 0.25, 10.0);
    sampler->proc_interval = 1;
    g_mutex_init(&sampler->mutex);
    g_cond_init(&sampler->cond);
    sampler->snapshot = snapshot_new();
    sampler->work = snapshot_new();
    history_clear(&sampler->history);
    cpu_reader_init(&sampler->cpu);
    disk_reader_init(&sampler->disk);
    net_reader_init(&sampler->net);
    proc_reader_init(&sampler->proc);
    gpu_reader_init(&sampler->gpu);
    return sampler;
}

void sampler_free(Sampler *sampler) {
    if (sampler == NULL) {
        return;
    }
    sampler_stop(sampler);
    g_mutex_clear(&sampler->mutex);
    g_cond_clear(&sampler->cond);
    snapshot_free(sampler->snapshot);
    snapshot_free(sampler->work);
    g_free(sampler);
}

void sampler_start(Sampler *sampler) {
    if (sampler->thread != NULL) {
        return;
    }
    sampler->stop = FALSE;
    sampler->thread = g_thread_new("prism-sampler", sampler_thread, sampler);
}

void sampler_stop(Sampler *sampler) {
    if (sampler->thread == NULL) {
        return;
    }
    g_mutex_lock(&sampler->mutex);
    sampler->stop = TRUE;
    g_cond_broadcast(&sampler->cond);
    g_mutex_unlock(&sampler->mutex);
    g_thread_join(sampler->thread);
    sampler->thread = NULL;
    sampler->running = FALSE;
}

gboolean sampler_copy(Sampler *sampler, Snapshot *dst, History *hist,
                      guint64 *last_seq) {
    g_mutex_lock(&sampler->mutex);
    gboolean changed = last_seq == NULL || *last_seq != sampler->seq;
    if (changed) {
        memcpy(dst, sampler->snapshot, sizeof(*dst));
        if (hist != NULL) {
            *hist = sampler->history;
        }
        if (last_seq != NULL) {
            *last_seq = sampler->seq;
        }
    }
    g_mutex_unlock(&sampler->mutex);
    return changed;
}

double sampler_interval(Sampler *sampler) {
    g_mutex_lock(&sampler->mutex);
    double interval = sampler->interval;
    g_mutex_unlock(&sampler->mutex);
    return interval;
}

void sampler_set_interval(Sampler *sampler, double interval) {
    g_mutex_lock(&sampler->mutex);
    sampler->interval = prism_clamp(interval, 0.25, 10.0);
    g_cond_broadcast(&sampler->cond);
    g_mutex_unlock(&sampler->mutex);
}

void sampler_set_process_interval(Sampler *sampler, int seconds) {
    g_mutex_lock(&sampler->mutex);
    sampler->proc_interval = seconds < 1 ? 1 : (seconds > 10 ? 10 : seconds);
    g_mutex_unlock(&sampler->mutex);
}

void sampler_set_paused(Sampler *sampler, gboolean paused) {
    if (sampler == NULL) {
        return;
    }
    g_mutex_lock(&sampler->mutex);
    sampler->paused = paused;
    g_cond_broadcast(&sampler->cond);
    g_mutex_unlock(&sampler->mutex);
}

gboolean sampler_is_paused(Sampler *sampler) {
    if (sampler == NULL) {
        return FALSE;
    }
    g_mutex_lock(&sampler->mutex);
    gboolean paused = sampler->paused;
    g_mutex_unlock(&sampler->mutex);
    return paused;
}

int sampler_load_history(Sampler *sampler) {
    if (sampler == NULL) {
        return 0;
    }
    g_mutex_lock(&sampler->mutex);
    int n = history_load(&sampler->history);
    g_mutex_unlock(&sampler->mutex);
    return n;
}

void sampler_set_plugin_dir(Sampler *sampler, const char *dir) {
    if (sampler == NULL) {
        return;
    }
    g_mutex_lock(&sampler->mutex);
    if (dir != NULL) {
        g_strlcpy(sampler->plugin_dir, dir, sizeof(sampler->plugin_dir));
    } else {
        sampler->plugin_dir[0] = '\0';
    }
    sampler->last_plugins = 0;
    g_mutex_unlock(&sampler->mutex);
}

void sampler_invalidate_aux(Sampler *sampler) {
    g_mutex_lock(&sampler->mutex);
    sampler->last_users = -1000.0;
    sampler->last_startup = -1000.0;
    sampler->last_services = -1000.0;
    sampler->last_connections = -1000.0;
    sampler->last_tcp = -1000.0;
    sampler->last_procs = -1000.0;
    g_cond_broadcast(&sampler->cond);
    g_mutex_unlock(&sampler->mutex);
}
