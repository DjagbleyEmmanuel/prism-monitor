#include "sys/net.h"

#include "core/common.h"
#include "core/parse.h"

#include <arpa/inet.h>
#include <glib.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

static int prev_index(NetReader *reader, const char *name) {
    for (int i = 0; i < reader->prev_count; i++) {
        if (strcmp(reader->prev[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void net_reader_init(NetReader *reader) {
    memset(reader, 0, sizeof(*reader));
}

gboolean net_parse_wireless(const char *text, const char *iface,
                            double *signal_dbm) {
    if (text == NULL || iface == NULL) {
        return FALSE;
    }
    const char *p = text;
    while ((p = strstr(p, iface)) != NULL) {
        /* format: iface: 0000.  level  -46.  noise  -92. ... */
        const char *colon = strchr(p, ':');
        if (colon == NULL) {
            return FALSE;
        }
        const char *level = strstr(colon, "level");
        if (level == NULL) {
            return FALSE;
        }
        *signal_dbm = strtod(level + 5, NULL);
        return TRUE;
    }
    return FALSE;
}

gboolean net_parse_ping(const char *text, double *ms) {
    if (text == NULL) {
        return FALSE;
    }
    const char *p = strstr(text, "time=");
    if (p == NULL) {
        p = strstr(text, "time <");
        if (p == NULL) {
            return FALSE;
        }
        *ms = 0;
        return TRUE;
    }
    *ms = strtod(p + 5, NULL);
    return TRUE;
}

double net_ping_ms(void) {
    static double cached = 0;
    static gint64 last_us = 0;
    gint64 now = g_get_monotonic_time();
    if (last_us != 0 && now - last_us < 30 * G_USEC_PER_SEC) {
        return cached;
    }
    gchar *argv[] = {"ping", "-c", "1", "-W", "1", "1.1.1.1", NULL};
    gchar *out = NULL;
    GError *err = NULL;
    double ms = 0;
    if (g_spawn_sync(NULL, argv, NULL,
                     G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                     NULL, NULL, &out, NULL, NULL, &err) && out != NULL) {
        net_parse_ping(out, &ms);
    }
    g_free(out);
    g_clear_error(&err);
    cached = ms;
    last_us = now;
    return cached;
}

static void fill_addresses(NicInfo *nic, struct ifaddrs *list, const char *name) {
    for (struct ifaddrs *ifa = list; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || strcmp(ifa->ifa_name, name) != 0) {
            continue;
        }
        char buffer[INET6_ADDRSTRLEN];
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, buffer, sizeof(buffer));
            g_strlcpy(nic->ipv4, buffer, sizeof(nic->ipv4));
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *sa = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &sa->sin6_addr, buffer, sizeof(buffer));
            if (nic->ipv6[0] == '\0' && strncmp(buffer, "fe80", 4) != 0) {
                g_strlcpy(nic->ipv6, buffer, sizeof(nic->ipv6));
            }
        } else if (ifa->ifa_addr->sa_family == AF_PACKET) {
            struct sockaddr_ll *sa = (struct sockaddr_ll *)ifa->ifa_addr;
            if (sa->sll_halen == 6) {
                snprintf(nic->mac, sizeof(nic->mac),
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         sa->sll_addr[0], sa->sll_addr[1], sa->sll_addr[2],
                         sa->sll_addr[3], sa->sll_addr[4], sa->sll_addr[5]);
            }
        }
    }
}

void net_read(NetReader *reader, Snapshot *snap, double elapsed) {
    NetDevRaw entries[PRISM_MAX_NICS * 2];
    int entry_count = 0;
    char *text = prism_read_file("/proc/net/dev");
    if (text != NULL) {
        entry_count = parse_netdev(text, entries, PRISM_MAX_NICS * 2);
        g_free(text);
    }
    struct ifaddrs *addr_list = NULL;
    getifaddrs(&addr_list);
    snap->net_rx_bps = 0;
    snap->net_tx_bps = 0;
    snap->ping_ms = net_ping_ms();
    char *wireless_text = prism_read_file("/proc/net/wireless");
    int count = 0;
    for (int i = 0; i < entry_count && count < PRISM_MAX_NICS; i++) {
        if (strcmp(entries[i].name, "lo") == 0) {
            continue;
        }
        NicInfo *nic = &snap->nics[count];
        memset(nic, 0, sizeof(*nic));
        g_strlcpy(nic->name, entries[i].name, sizeof(nic->name));
        nic->rx_total = entries[i].rx_bytes;
        nic->tx_total = entries[i].tx_bytes;
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", entries[i].name);
        char *state = prism_read_first_line(path);
        g_strlcpy(nic->state, state != NULL ? state : "unknown", sizeof(nic->state));
        g_free(state);
        snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", entries[i].name);
        char *mtu = prism_read_first_line(path);
        nic->mtu = mtu != NULL ? atoi(mtu) : 0;
        g_free(mtu);
        snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", entries[i].name);
        struct stat st;
        nic->wireless = stat(path, &st) == 0;
        if (nic->wireless && wireless_text != NULL) {
            double sig = 0;
            if (net_parse_wireless(wireless_text, entries[i].name, &sig)) {
                nic->wifi_signal_dbm = sig;
            }
            /* link speed / freq via sysfs */
            snprintf(path, sizeof(path), "/sys/class/net/%s/speed", entries[i].name);
            char *sp = prism_read_first_line(path);
            if (sp != NULL) {
                nic->wifi_link_mbps = strtod(sp, NULL);
                g_free(sp);
            }
            snprintf(path, sizeof(path), "/sys/class/net/%s/frequency", entries[i].name);
            char *fq = prism_read_first_line(path);
            if (fq != NULL) {
                nic->wifi_freq_mhz = strtod(fq, NULL);
                g_free(fq);
            }
        }
        if (addr_list != NULL) {
            fill_addresses(nic, addr_list, entries[i].name);
        }
        int prev = prev_index(reader, entries[i].name);
        if (prev >= 0 && elapsed > 0) {
            unsigned long long prev_rx = reader->prev[prev].rx_bytes;
            unsigned long long prev_tx = reader->prev[prev].tx_bytes;
            if (entries[i].rx_bytes >= prev_rx) {
                nic->rx_bps = (double)(entries[i].rx_bytes - prev_rx) / elapsed;
            }
            if (entries[i].tx_bytes >= prev_tx) {
                nic->tx_bps = (double)(entries[i].tx_bytes - prev_tx) / elapsed;
            }
        }
        snap->net_rx_bps += nic->rx_bps;
        snap->net_tx_bps += nic->tx_bps;
        count++;
    }
    g_free(wireless_text);
    if (addr_list != NULL) {
        freeifaddrs(addr_list);
    }
    snap->nic_count = count;
    reader->prev_count = 0;
    for (int i = 0; i < entry_count && reader->prev_count < PRISM_MAX_NICS; i++) {
        if (strcmp(entries[i].name, "lo") == 0) {
            continue;
        }
        NetPrev *prev = &reader->prev[reader->prev_count++];
        g_strlcpy(prev->name, entries[i].name, sizeof(prev->name));
        prev->rx_bytes = entries[i].rx_bytes;
        prev->tx_bytes = entries[i].tx_bytes;
    }
    reader->initialized = 1;
}
