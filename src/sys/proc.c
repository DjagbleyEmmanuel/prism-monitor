#include "sys/proc.h"

#include "core/common.h"
#include "core/parse.h"

#include <fcntl.h>
#include <glib.h>
#include <signal.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

gboolean proc_parse_io(const char *text, unsigned long long *read_bytes,
                       unsigned long long *write_bytes) {
    if (text == NULL) {
        return FALSE;
    }
    gboolean ok = FALSE;
    const char *p = text;
    while ((p = strstr(p, "read_bytes:")) != NULL) {
        *read_bytes = strtoull(p + 11, NULL, 10);
        ok = TRUE;
        break;
    }
    p = text;
    while ((p = strstr(p, "write_bytes:")) != NULL) {
        *write_bytes = strtoull(p + 12, NULL, 10);
        break;
    }
    return ok;
}

static void read_proc_io(int pid, unsigned long long *read_bytes,
                         unsigned long long *write_bytes) {
    *read_bytes = 0;
    *write_bytes = 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    char *text = prism_read_file(path);
    if (text != NULL) {
        proc_parse_io(text, read_bytes, write_bytes);
        g_free(text);
    }
}

void proc_reader_init(ProcReader *reader) {
    memset(reader, 0, sizeof(*reader));
    reader->ticks_per_second = sysconf(_SC_CLK_TCK);
    reader->page_size = sysconf(_SC_PAGESIZE);
    if (reader->ticks_per_second <= 0) {
        reader->ticks_per_second = 100;
    }
    if (reader->page_size <= 0) {
        reader->page_size = 4096;
    }
}

static int prev_compare(const void *a, const void *b) {
    const ProcPrev *pa = a;
    const ProcPrev *pb = b;
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

static int prev_lookup(const ProcPrev *list, int count, int pid) {
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (list[mid].pid == pid) {
            return mid;
        }
        if (list[mid].pid < pid) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

static gboolean is_kernel_thread(int pid, unsigned int uid) {
    if (uid != 0) {
        return FALSE;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    char target[2];
    if (readlink(path, target, sizeof(target)) >= 0) {
        return FALSE;
    }
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return TRUE;
    }
    char byte = 0;
    ssize_t got = read(fd, &byte, 1);
    close(fd);
    return got <= 0;
}

static void username_cached(ProcReader *reader, unsigned int uid, char *out,
                            size_t out_len) {
    for (int i = 0; i < reader->user_count; i++) {
        if (reader->users[i].uid == uid) {
            g_strlcpy(out, reader->users[i].name, out_len);
            return;
        }
    }
    char name[32];
    prism_username_for_uid(uid, name, sizeof(name));
    g_strlcpy(out, name, out_len);
    if (reader->user_count < (int)G_N_ELEMENTS(reader->users)) {
        reader->users[reader->user_count].uid = uid;
        g_strlcpy(reader->users[reader->user_count].name,
                  name, sizeof(reader->users[reader->user_count].name));
        reader->user_count++;
    }
}

void proc_read(ProcReader *reader, Snapshot *snap, double elapsed) {
    snap->proc_count = 0;
    GDir *dir = g_dir_open("/proc", 0, NULL);
    if (dir == NULL) {
        return;
    }
    if (elapsed <= 0) {
        elapsed = 1;
    }
    const char *name;
    int count = 0;
    ProcPrev next[PRISM_MAX_PROCS];
    int next_count = 0;
    while ((name = g_dir_read_name(dir)) != NULL && count < PRISM_MAX_PROCS) {
        if (name[0] < '1' || name[0] > '9') {
            continue;
        }
        gboolean numeric = TRUE;
        for (const char *p = name; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') {
                numeric = FALSE;
                break;
            }
        }
        if (!numeric) {
            continue;
        }
        int pid = atoi(name);
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        char *stat_text = prism_read_file(path);
        if (stat_text == NULL) {
            continue;
        }
        ProcStatRaw raw;
        if (!parse_proc_stat(stat_text, &raw)) {
            g_free(stat_text);
            continue;
        }
        const char *open = strchr(stat_text, '(');
        const char *close = strrchr(stat_text, ')');
        if (open == NULL || close == NULL || close <= open) {
            g_free(stat_text);
            continue;
        }
        ProcInfo *proc = &snap->procs[count];
        memset(proc, 0, sizeof(*proc));
        proc->pid = pid;
        proc->ppid = raw.ppid;
        proc->state = raw.state;
        size_t name_len = (size_t)(close - open - 1);
        if (name_len >= sizeof(proc->name)) {
            name_len = sizeof(proc->name) - 1;
        }
        memcpy(proc->name, open + 1, name_len);
        proc->name[name_len] = '\0';
        proc->rss_kb = (unsigned long long)raw.rss_pages * reader->page_size / 1024;
        proc->vsz_kb = raw.vsize / 1024;
        proc->threads = (int)raw.threads;
        proc->nice = (int)raw.nice;
        proc->start_ticks = raw.starttime;
        struct stat st;
        snprintf(path, sizeof(path), "/proc/%d", pid);
        unsigned int uid = 0;
        if (stat(path, &st) == 0) {
            uid = (unsigned int)st.st_uid;
        }
        username_cached(reader, uid, proc->user, sizeof(proc->user));
        unsigned long long ticks = raw.utime + raw.stime;
        int prev = -1;
        if (reader->initialized) {
            prev = prev_lookup(reader->prev, reader->prev_count, pid);
            if (prev >= 0) {
                unsigned long long previous = reader->prev[prev].ticks;
                if (ticks >= previous) {
                    double delta = (double)(ticks - previous);
                    proc->cpu_pct = prism_clamp(
                        delta / (double)reader->ticks_per_second / elapsed * 100.0,
                        0, 6400);
                }
                proc->kernel_thread = reader->prev[prev].kernel_thread;
            }
        }
        if (prev < 0) {
            proc->kernel_thread = is_kernel_thread(pid, uid);
        }
        read_proc_io(pid, &proc->io_read_bytes, &proc->io_write_bytes);
        if (prev >= 0 && elapsed > 0) {
            if (proc->io_read_bytes >= reader->prev[prev].io_read) {
                proc->io_read_bps = (double)(proc->io_read_bytes -
                                             reader->prev[prev].io_read) / elapsed;
            }
            if (proc->io_write_bytes >= reader->prev[prev].io_write) {
                proc->io_write_bps = (double)(proc->io_write_bytes -
                                              reader->prev[prev].io_write) / elapsed;
            }
        }
        if (next_count < PRISM_MAX_PROCS) {
            next[next_count].pid = pid;
            next[next_count].ticks = ticks;
            next[next_count].kernel_thread = (unsigned char)proc->kernel_thread;
            next[next_count].io_read = proc->io_read_bytes;
            next[next_count].io_write = proc->io_write_bytes;
            next_count++;
        }
        g_free(stat_text);
        count++;
    }
    g_dir_close(dir);
    snap->proc_count = count;
    /* tree depth: walk ppid chain, capped at 8 */
    for (int i = 0; i < count; i++) {
        int depth = 0;
        int ppid = snap->procs[i].ppid;
        for (int guard = 0; guard < 8 && ppid > 1; guard++) {
            int found = 0;
            for (int j = 0; j < count; j++) {
                if (snap->procs[j].pid == ppid) {
                    ppid = snap->procs[j].ppid;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                break;
            }
            depth++;
        }
        snap->procs[i].tree_depth = depth;
    }
    memcpy(reader->prev, next, (size_t)next_count * sizeof(next[0]));
    reader->prev_count = next_count;
    qsort(reader->prev, reader->prev_count, sizeof(ProcPrev), prev_compare);
    reader->initialized = 1;
}

static void read_cmdline(int pid, char *out, size_t out_len) {
    out[0] = '\0';
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL)) {
        return;
    }
    gsize written = 0;
    for (gsize i = 0; i < length && written + 1 < out_len; i++) {
        out[written++] = contents[i] == '\0' ? ' ' : contents[i];
    }
    while (written > 0 && out[written - 1] == ' ') {
        written--;
    }
    out[written] = '\0';
    g_free(contents);
}

int proc_details(int pid, ProcDetails *out) {
    memset(out, 0, sizeof(*out));
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    char *stat_text = prism_read_file(path);
    if (stat_text == NULL) {
        return 0;
    }
    ProcStatRaw raw;
    if (!parse_proc_stat(stat_text, &raw)) {
        g_free(stat_text);
        return 0;
    }
    const char *open = strchr(stat_text, '(');
    const char *close = strrchr(stat_text, ')');
    if (open != NULL && close != NULL && close > open) {
        size_t len = (size_t)(close - open - 1);
        if (len >= sizeof(out->name)) {
            len = sizeof(out->name) - 1;
        }
        memcpy(out->name, open + 1, len);
        out->name[len] = '\0';
    }
    g_free(stat_text);
    out->pid = pid;
    out->ppid = raw.ppid;
    out->state = raw.state;
    out->rss_kb = (unsigned long long)raw.rss_pages * sysconf(_SC_PAGESIZE) / 1024;
    out->vsz_kb = raw.vsize / 1024;
    out->threads = (int)raw.threads;
    out->nice = (int)raw.nice;
    out->priority = (int)raw.priority;
    out->start_ticks = raw.starttime;
    snprintf(path, sizeof(path), "/proc/%d", pid);
    struct stat st;
    if (stat(path, &st) == 0) {
        prism_username_for_uid((unsigned int)st.st_uid, out->user, sizeof(out->user));
    }
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    ssize_t len = readlink(path, out->exe, sizeof(out->exe) - 1);
    if (len > 0) {
        out->exe[len] = '\0';
    }
    read_proc_io(pid, &out->io_read_bytes, &out->io_write_bytes);
    snprintf(path, sizeof(path), "/proc/%d/cwd", pid);
    len = readlink(path, out->cwd, sizeof(out->cwd) - 1);
    if (len > 0) {
        out->cwd[len] = '\0';
    }
    read_cmdline(pid, out->cmdline, sizeof(out->cmdline));
    return 1;
}

int proc_signal(int pid, int signal_number) {
    if (pid <= 1) {
        return -1;
    }
    return kill(pid, signal_number);
}

int proc_set_nice(int pid, int nice) {
    if (pid <= 1 || nice < -20 || nice > 19) {
        return -1;
    }
    return setpriority(PRIO_PROCESS, (id_t)pid, nice);
}

int proc_set_ionice(int pid, int level) {
    if (pid <= 1 || level < 0 || level > 7) {
        return -1;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/ionice", pid);
    /* ioprio_set via syscall number 251 on x86_64, best-effort */
    int priority = (2 << 13) | level; /* IOPRIO_CLASS_BE=2 */
#ifdef SYS_ioprio_set
    return syscall(SYS_ioprio_set, 1, pid, priority);
#else
    (void)priority;
    return -1;
#endif
}

int proc_set_affinity(int pid, const char *cpuset) {
    if (pid <= 1 || cpuset == NULL || cpuset[0] == '\0') {
        return -1;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    char *copy = g_strdup(cpuset);
    char *save = NULL;
    char *tok = strtok_r(copy, ",", &save);
    int any = 0;
    while (tok != NULL) {
        char *dash = strchr(tok, '-');
        if (dash != NULL) {
            *dash = '\0';
            int lo = atoi(tok);
            int hi = atoi(dash + 1);
            for (int c = lo; c <= hi && c < CPU_SETSIZE; c++) {
                CPU_SET(c, &set);
                any = 1;
            }
        } else {
            int c = atoi(tok);
            if (c >= 0 && c < CPU_SETSIZE) {
                CPU_SET(c, &set);
                any = 1;
            }
        }
        tok = strtok_r(NULL, ",", &save);
    }
    g_free(copy);
    if (!any) {
        return -1;
    }
    return sched_setaffinity((pid_t)pid, sizeof(set), &set);
}

int proc_kill_tree(int pid, int signal_number) {
    if (pid <= 1) {
        return -1;
    }
    /* Best-effort: signal process group if it is a group leader,
     * otherwise just the process. Caller should confirm in UI. */
    return kill(-pid, signal_number) == 0 ? 0 : kill(pid, signal_number);
}
