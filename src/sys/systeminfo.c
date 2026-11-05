#include "sys/systeminfo.h"

#include "core/common.h"
#include "core/parse.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <glib.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

typedef struct {
    int ready;
    SystemInfo info;
} SystemCache;

static SystemCache cache;

static void os_pretty_name(char *out, size_t out_len) {
    char *text = prism_read_file("/etc/os-release");
    if (text == NULL) {
        g_strlcpy(out, "Linux", out_len);
        return;
    }
    const char *hit = strstr(text, "PRETTY_NAME=");
    if (hit == NULL) {
        hit = strstr(text, "NAME=");
    }
    if (hit != NULL) {
        const char *start = strchr(hit, '=');
        if (start != NULL) {
            start++;
            if (*start == '"' || *start == '\'') {
                char quote = *start++;
                const char *end = strchr(start, quote);
                if (end != NULL) {
                    size_t len = (size_t)(end - start);
                    if (len >= out_len) {
                        len = out_len - 1;
                    }
                    memcpy(out, start, len);
                    out[len] = '\0';
                    g_free(text);
                    return;
                }
            }
        }
    }
    g_strlcpy(out, "Linux", out_len);
    g_free(text);
}

static int physical_core_count(void) {
    GDir *dir = g_dir_open("/sys/devices/system/cpu", 0, NULL);
    if (dir == NULL) {
        return (int)sysconf(_SC_NPROCESSORS_ONLN);
    }
    GHashTable *cores = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        if (strncmp(entry, "cpu", 3) != 0 || entry[3] < '0' || entry[3] > '9') {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/%s/topology/core_id", entry);
        char *core = prism_read_first_line(path);
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/%s/topology/physical_package_id", entry);
        char *package = prism_read_first_line(path);
        if (core != NULL && package != NULL) {
            char key[128];
            snprintf(key, sizeof(key), "%s:%s", package, core);
            if (!g_hash_table_contains(cores, key)) {
                g_hash_table_add(cores, g_strdup(key));
            }
        }
        g_free(core);
        g_free(package);
    }
    g_dir_close(dir);
    int count = (int)g_hash_table_size(cores);
    g_hash_table_destroy(cores);
    return count > 0 ? count : (int)sysconf(_SC_NPROCESSORS_ONLN);
}

static void system_read_static(SystemInfo *out) {
    memset(out, 0, sizeof(*out));
    gethostname(out->hostname, sizeof(out->hostname));
    out->hostname[sizeof(out->hostname) - 1] = '\0';
    struct utsname info;
    if (uname(&info) == 0) {
        g_strlcpy(out->kernel, info.release, sizeof(out->kernel));
        g_strlcpy(out->arch, info.machine, sizeof(out->arch));
    }
    os_pretty_name(out->distro, sizeof(out->distro));
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (desktop == NULL || *desktop == '\0') {
        desktop = g_getenv("DESKTOP_SESSION");
    }
    g_strlcpy(out->desktop, desktop != NULL ? desktop : "Unknown", sizeof(out->desktop));
    char *cpuinfo = prism_read_file("/proc/cpuinfo");
    if (cpuinfo != NULL) {
        const char *hit = strstr(cpuinfo, "model name");
        if (hit != NULL) {
            const char *colon = strchr(hit, ':');
            if (colon != NULL) {
                const char *start = colon + 1;
                while (*start == ' ' || *start == '\t') {
                    start++;
                }
                const char *end = strchr(start, '\n');
                size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
                if (length >= sizeof(out->cpu_model)) {
                    length = sizeof(out->cpu_model) - 1;
                }
                memcpy(out->cpu_model, start, length);
                out->cpu_model[length] = '\0';
                prism_trim(out->cpu_model);
            }
        }
        g_free(cpuinfo);
    }
    out->cpu_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
    out->cpu_cores = physical_core_count();
    char *meminfo = prism_read_file("/proc/meminfo");
    if (meminfo != NULL) {
        MemInfo mem;
        memset(&mem, 0, sizeof(mem));
        parse_meminfo(meminfo, &mem);
        out->total_mem_mb = mem.total_mb;
        g_free(meminfo);
    }
}

void system_read(SystemInfo *out) {
    if (!cache.ready) {
        system_read_static(&cache.info);
        cache.ready = 1;
    }
    *out = cache.info;
    char *uptime = prism_read_first_line("/proc/uptime");
    if (uptime != NULL) {
        out->uptime_s = parse_uptime(uptime);
        g_free(uptime);
    }
    cache.info.uptime_s = out->uptime_s;
    out->boot_time = (double)time(NULL) - out->uptime_s;
    cache.info.boot_time = out->boot_time;
}

void users_read(Snapshot *snap) {
    snap->user_count = 0;
    setutxent();
    struct utmpx *entry;
    while ((entry = getutxent()) != NULL && snap->user_count < PRISM_MAX_USERS) {
        if (entry->ut_type != USER_PROCESS) {
            continue;
        }
        UserInfo *user = &snap->users[snap->user_count++];
        memset(user, 0, sizeof(*user));
        g_strlcpy(user->name, entry->ut_user, sizeof(user->name));
        g_strlcpy(user->tty, entry->ut_line, sizeof(user->tty));
        g_strlcpy(user->host, entry->ut_host, sizeof(user->host));
        user->login_time = (int64_t)entry->ut_tv.tv_sec;
    }
    endutxent();
}

static void startup_add(Snapshot *snap, const char *path, const char *source,
                        GKeyFile *keyfile) {
    if (snap->startup_count >= PRISM_MAX_STARTUP) {
        return;
    }
    gboolean hidden = g_key_file_get_boolean(keyfile, "Desktop Entry", "Hidden", NULL);
    gboolean enabled = TRUE;
    if (g_key_file_has_key(keyfile, "Desktop Entry", "X-GNOME-Autostart-enabled", NULL)) {
        enabled = g_key_file_get_boolean(
            keyfile, "Desktop Entry", "X-GNOME-Autostart-enabled", NULL);
    }
    char *display = g_key_file_get_string(keyfile, "Desktop Entry", "Name", NULL);
    char *exec = g_key_file_get_string(keyfile, "Desktop Entry", "Exec", NULL);
    if (display != NULL && exec != NULL && !hidden) {
        StartupInfo *item = &snap->startups[snap->startup_count++];
        memset(item, 0, sizeof(*item));
        g_strlcpy(item->name, display, sizeof(item->name));
        g_strlcpy(item->exec, exec, sizeof(item->exec));
        g_strlcpy(item->source, source, sizeof(item->source));
        g_strlcpy(item->file, path, sizeof(item->file));
        item->enabled = enabled ? 1 : 0;
    }
    g_free(display);
    g_free(exec);
}

static void startup_scan_dir(Snapshot *snap, const char *directory, const char *source) {
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir == NULL) {
        return;
    }
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL && snap->startup_count < PRISM_MAX_STARTUP) {
        if (!g_str_has_suffix(name, ".desktop")) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, name);
        GKeyFile *keyfile = g_key_file_new();
        if (g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL)) {
            startup_add(snap, path, source, keyfile);
        }
        g_key_file_free(keyfile);
    }
    g_dir_close(dir);
}

void startup_read(Snapshot *snap) {
    snap->startup_count = 0;
    startup_scan_dir(snap, "/etc/xdg/autostart", "system");
    char *user_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    startup_scan_dir(snap, user_dir, "user");
    g_free(user_dir);
}

typedef struct {
    gboolean enabled;
    char state[24];
} UnitFileState;

static void service_merge_unit_files(Snapshot *snap) {
    gchar *output = NULL;
    if (!g_spawn_command_line_sync(
            "systemctl list-unit-files --type=service --no-pager --no-legend --plain",
            &output, NULL, NULL, NULL) || output == NULL) {
        g_free(output);
        return;
    }
    GHashTable *states = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        char **fields = g_strsplit_set(g_strstrip(lines[i]), " \t", 3);
        if (fields[0] != NULL && fields[0][0] != '\0' && fields[1] != NULL) {
            UnitFileState *state = g_new0(UnitFileState, 1);
            g_strlcpy(state->state, fields[1], sizeof(state->state));
            state->enabled = strcmp(fields[1], "enabled") == 0 ||
                             strcmp(fields[1], "enabled-runtime") == 0;
            g_hash_table_insert(states, g_strdup(fields[0]), state);
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    g_free(output);
    for (int i = 0; i < snap->service_count; i++) {
        UnitFileState *state = g_hash_table_lookup(states, snap->services[i].name);
        if (state != NULL) {
            snap->services[i].enabled = state->enabled ? 1 : 0;
            g_strlcpy(snap->services[i].unit_file, state->state,
                      sizeof(snap->services[i].unit_file));
        }
    }
    g_hash_table_destroy(states);
}

void services_read(Snapshot *snap) {
    snap->service_count = 0;
    gchar *output = NULL;
    if (!g_spawn_command_line_sync(
            "systemctl list-units --type=service --all --no-pager --no-legend --plain",
            &output, NULL, NULL, NULL) || output == NULL) {
        g_free(output);
        return;
    }
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i] != NULL && snap->service_count < PRISM_MAX_SERVICES; i++) {
        char *line = g_strstrip(lines[i]);
        if (*line == '\0' || *line == '\xe2') {
            continue;
        }
        char *tokens[4] = {NULL, NULL, NULL, NULL};
        char *cursor = line;
        int token_count = 0;
        for (int f = 0; f < 4; f++) {
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            if (*cursor == '\0') {
                break;
            }
            tokens[f] = cursor;
            token_count++;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
                cursor++;
            }
            if (*cursor != '\0') {
                *cursor++ = '\0';
            }
        }
        if (token_count < 4) {
            continue;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        ServiceInfo *service = &snap->services[snap->service_count++];
        memset(service, 0, sizeof(*service));
        g_strlcpy(service->name, tokens[0], sizeof(service->name));
        g_strlcpy(service->load, tokens[1], sizeof(service->load));
        g_strlcpy(service->active_state, tokens[2], sizeof(service->active_state));
        g_strlcpy(service->sub_state, tokens[3], sizeof(service->sub_state));
        g_strlcpy(service->description, cursor, sizeof(service->description));
        service->active = strcmp(service->active_state, "active") == 0;
    }
    g_strfreev(lines);
    g_free(output);
    service_merge_unit_files(snap);
}

static void startup_write_override(const StartupInfo *item, int enabled) {
    char *user_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_mkdir_with_parents(user_dir, 0755);
    char *name = g_path_get_basename(item->file);
    char *path = g_build_filename(user_dir, name, NULL);
    GKeyFile *keyfile = g_key_file_new();
    g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL);
    g_key_file_set_boolean(keyfile, "Desktop Entry", "Hidden", enabled ? FALSE : TRUE);
    g_key_file_set_boolean(keyfile, "Desktop Entry", "X-GNOME-Autostart-enabled",
                           enabled ? TRUE : FALSE);
    if (!g_key_file_has_key(keyfile, "Desktop Entry", "Type", NULL)) {
        g_key_file_set_string(keyfile, "Desktop Entry", "Type", "Application");
    }
    if (!g_key_file_has_key(keyfile, "Desktop Entry", "Name", NULL)) {
        g_key_file_set_string(keyfile, "Desktop Entry", "Name", item->name);
    }
    if (!g_key_file_has_key(keyfile, "Desktop Entry", "Exec", NULL)) {
        g_key_file_set_string(keyfile, "Desktop Entry", "Exec", item->exec);
    }
    g_key_file_save_to_file(keyfile, path, NULL);
    g_key_file_free(keyfile);
    g_free(path);
    g_free(name);
    g_free(user_dir);
}

void startup_set_enabled(const StartupInfo *item, int enabled) {
    if (strcmp(item->source, "user") == 0) {
        GKeyFile *keyfile = g_key_file_new();
        if (g_key_file_load_from_file(keyfile, item->file, G_KEY_FILE_NONE, NULL)) {
            g_key_file_set_boolean(keyfile, "Desktop Entry", "Hidden",
                                   enabled ? FALSE : TRUE);
            g_key_file_set_boolean(keyfile, "Desktop Entry",
                                   "X-GNOME-Autostart-enabled",
                                   enabled ? TRUE : FALSE);
            g_key_file_save_to_file(keyfile, item->file, NULL);
        }
        g_key_file_free(keyfile);
    } else {
        startup_write_override(item, enabled);
    }
}

static void service_run(const char *verb, const char *unit) {
    char *quoted = g_shell_quote(unit);
    char *command = g_strdup_printf("systemctl %s %s", verb, quoted);
    g_spawn_command_line_async(command, NULL);
    g_free(command);
    g_free(quoted);
}

void service_action(const char *unit, const char *verb) {
    if (strcmp(verb, "enable") == 0 || strcmp(verb, "disable") == 0) {
        service_run(verb, unit);
        return;
    }
    service_run(verb, unit);
}

void system_run_command(const char *command) {
    if (command == NULL || command[0] == '\0') {
        return;
    }
    gchar *argv[] = {"/bin/sh", "-c", (gchar *)command, NULL};
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                  NULL, NULL, NULL, NULL);
}

/* ---- socket inventory ---- */

static const char *tcp_state_name(unsigned int state, int *listening) {
    if (listening != NULL) {
        *listening = 0;
    }
    switch (state) {
        case 0x01:
            return "ESTABLISHED";
        case 0x02:
            return "SYN_SENT";
        case 0x03:
            return "SYN_RECV";
        case 0x04:
            return "FIN_WAIT1";
        case 0x05:
            return "FIN_WAIT2";
        case 0x06:
            return "TIME_WAIT";
        case 0x07:
            return "CLOSE";
        case 0x08:
            return "CLOSE_WAIT";
        case 0x09:
            return "LAST_ACK";
        case 0x0A:
            if (listening != NULL) {
                *listening = 1;
            }
            return "LISTEN";
        case 0x0B:
            return "CLOSING";
        default:
            return "UNKNOWN";
    }
}

static int decode_endpoint(const char *token, int family, char *out, size_t out_len) {
    char address[40];
    unsigned int port = 0;
    const char *colon = strrchr(token, ':');
    if (colon == NULL) {
        return 0;
    }
    size_t address_len = (size_t)(colon - token);
    if (address_len >= sizeof(address)) {
        address_len = sizeof(address) - 1;
    }
    memcpy(address, token, address_len);
    address[address_len] = '\0';
    port = (unsigned int)strtoul(colon + 1, NULL, 16);
    if (family == AF_INET) {
        unsigned int raw = (unsigned int)strtoul(address, NULL, 16);
        struct in_addr addr;
        addr.s_addr = raw;
        char text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, text, sizeof(text));
        snprintf(out, out_len, "%s:%u", text, port);
        return 1;
    }
    unsigned int words[4] = {0};
    if (sscanf(address, "%8x%8x%8x%8x", &words[0], &words[1], &words[2], &words[3]) != 4) {
        g_strlcpy(out, token, out_len);
        return 1;
    }
    unsigned char bytes[16];
    for (int i = 0; i < 4; i++) {
        bytes[i * 4 + 0] = (unsigned char)(words[i] & 0xff);
        bytes[i * 4 + 1] = (unsigned char)((words[i] >> 8) & 0xff);
        bytes[i * 4 + 2] = (unsigned char)((words[i] >> 16) & 0xff);
        bytes[i * 4 + 3] = (unsigned char)((words[i] >> 24) & 0xff);
    }
    struct in6_addr addr6;
    memcpy(&addr6, bytes, sizeof(bytes));
    char text[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr6, text, sizeof(text));
    snprintf(out, out_len, "[%s]:%u", text, port);
    return 1;
}

static void connections_scan_file(Snapshot *snap, const char *path, const char *protocol,
                                  int family) {
    char *text = prism_read_file(path);
    if (text == NULL) {
        return;
    }
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    if (line != NULL) {
        line = strtok_r(NULL, "\n", &save);
    }
    while (line != NULL && snap->connection_count < PRISM_MAX_CONNECTIONS) {
        char local[64];
        char remote[64];
        unsigned int state = 0;
        unsigned long long inode = 0;
        if (sscanf(line, " %*u: %63s %63s %x %*s %*s %*s %*u %*u %llu",
                   local, remote, &state, &inode) == 4) {
            ConnectionInfo *conn = &snap->connections[snap->connection_count];
            memset(conn, 0, sizeof(*conn));
            int listening = 0;
            g_strlcpy(conn->protocol, protocol, sizeof(conn->protocol));
            decode_endpoint(local, family, conn->local, sizeof(conn->local));
            decode_endpoint(remote, family, conn->remote, sizeof(conn->remote));
            const char *state_name = tcp_state_name(state, &listening);
            if (family == AF_INET && strncmp(protocol, "UDP", 3) == 0) {
                state_name = state == 0x07 ? "UNCONN" : "ESTABLISHED";
                listening = state == 0x07;
            }
            g_strlcpy(conn->state, state_name, sizeof(conn->state));
            conn->listening = listening;
            conn->inode = inode;
            conn->pid = -1;
            if (inode != 0) {
                snap->connection_count++;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    g_free(text);
}

static void connections_map_processes(Snapshot *snap) {
    if (snap->connection_count == 0) {
        return;
    }
    GHashTable *inodes = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (int i = 0; i < snap->connection_count; i++) {
        g_hash_table_insert(inodes, GUINT_TO_POINTER((guint)snap->connections[i].inode),
                            GINT_TO_POINTER(i));
    }
    DIR *proc = opendir("/proc");
    if (proc == NULL) {
        g_hash_table_destroy(inodes);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }
        int pid = atoi(entry->d_name);
        char fd_dir_path[64];
        snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);
        DIR *fds = opendir(fd_dir_path);
        if (fds == NULL) {
            continue;
        }
        char process[64] = "";
        struct dirent *fd;
        while ((fd = readdir(fds)) != NULL) {
            if (fd->d_name[0] == '.') {
                continue;
            }
            char link_path[512];
            char target[512];
            snprintf(link_path, sizeof(link_path), "/proc/%d/fd/%s", pid, fd->d_name);
            ssize_t length = readlink(link_path, target, sizeof(target) - 1);
            if (length <= 0) {
                continue;
            }
            target[length] = '\0';
            if (strncmp(target, "socket:[", 8) != 0) {
                continue;
            }
            unsigned long long inode = strtoull(target + 8, NULL, 10);
            gpointer value = g_hash_table_lookup(inodes, GUINT_TO_POINTER((guint)inode));
            if (value == NULL) {
                continue;
            }
            int index = GPOINTER_TO_INT(value);
            if (snap->connections[index].pid < 0) {
                if (process[0] == '\0') {
                    char comm_path[64];
                    snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
                    char *comm = prism_read_first_line(comm_path);
                    if (comm != NULL) {
                        g_strlcpy(process, comm, sizeof(process));
                        g_free(comm);
                    }
                }
                snap->connections[index].pid = pid;
                g_strlcpy(snap->connections[index].process, process,
                          sizeof(snap->connections[index].process));
            }
        }
        closedir(fds);
    }
    closedir(proc);
    g_hash_table_destroy(inodes);
}

void connections_read(Snapshot *snap) {
    snap->connection_count = 0;
    connections_scan_file(snap, "/proc/net/tcp", "TCP4", AF_INET);
    connections_scan_file(snap, "/proc/net/tcp6", "TCP6", AF_INET6);
    connections_scan_file(snap, "/proc/net/udp", "UDP4", AF_INET);
    connections_scan_file(snap, "/proc/net/udp6", "UDP6", AF_INET6);
    connections_map_processes(snap);
}

static void tcp_count_file(const char *path, TcpInfo *out) {
    char *text = prism_read_file(path);
    if (text == NULL) {
        return;
    }
    const char *line = strchr(text, '\n');
    while (line != NULL) {
        line++;
        if (*line == '\0') {
            break;
        }
        unsigned int state = 0;
        if (sscanf(line, " %*d: %*s %*s %x", &state) == 1) {
            switch (state) {
                case 0x01:
                    out->established++;
                    break;
                case 0x0A:
                    out->listen++;
                    break;
                case 0x06:
                    out->time_wait++;
                    break;
                case 0x08:
                    out->close_wait++;
                    break;
                default:
                    break;
            }
            out->total++;
        }
        line = strchr(line, '\n');
    }
    g_free(text);
}

void tcp_read(TcpInfo *out) {
    memset(out, 0, sizeof(*out));
    tcp_count_file("/proc/net/tcp", out);
    tcp_count_file("/proc/net/tcp6", out);
}
