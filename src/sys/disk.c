#include "sys/disk.h"

#include "core/common.h"
#include "core/parse.h"

#include <dirent.h>
#include <glib.h>
#include <mntent.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

static int prev_index(DiskReader *reader, const char *name) {
    for (int i = 0; i < reader->prev_count; i++) {
        if (strcmp(reader->prev[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static double disk_temp(const char *device) {
    char path[512];
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/sys/block/%s/device/hwmon/hwmon%d/temp1_input",
                 device, i);
        char *value = prism_read_first_line(path);
        if (value != NULL) {
            double temp = strtod(value, NULL) / 1000.0;
            g_free(value);
            return temp;
        }
    }
    return 0;
}

static gboolean skip_device(const char *name) {
    return strncmp(name, "loop", 4) == 0 ||
           strncmp(name, "ram", 3) == 0 ||
           strncmp(name, "zram", 4) == 0 ||
           strncmp(name, "sr", 2) == 0 ||
           strncmp(name, "fd", 2) == 0;
}

void disk_reader_init(DiskReader *reader) {
    memset(reader, 0, sizeof(*reader));
}

static double parse_after(const char *text, const char *key) {
    const char *p = strstr(text, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') {
        p++;
    }
    return strtod(p, NULL);
}

gboolean disk_parse_smart(const char *text, DiskInfo *disk) {
    if (text == NULL || disk == NULL) {
        return FALSE;
    }
    gboolean found = FALSE;
    double v;
    /* NVMe smart-log style */
    if ((v = parse_after(text, "Percentage Used")) >= 0) {
        disk->wear_pct = v;
        disk->smart_available = 1;
        found = TRUE;
    }
    if ((v = parse_after(text, "Data Units Written")) >= 0) {
        disk->data_written_tb = v * 0.512 / 1000.0; /* 512B units -> TB approx */
        disk->smart_available = 1;
        found = TRUE;
    }
    if ((v = parse_after(text, "Media and Data Integrity Errors")) >= 0) {
        disk->media_errors = (unsigned long long)v;
        disk->smart_available = 1;
        found = TRUE;
    }
    if ((v = parse_after(text, "Power On Hours")) >= 0) {
        disk->power_on_hours = v;
        disk->smart_available = 1;
        found = TRUE;
    }
    if ((v = parse_after(text, "Temperature")) >= 0 && v > 0 && v < 200) {
        /* smartctl reports Kelvin or Celsius depending; keep sysfs temp if set */
        if (disk->temp_c <= 0) {
            disk->temp_c = v > 200 ? v - 273.15 : v;
        }
        found = TRUE;
    }
    /* ATA smartctl style */
    if ((v = parse_after(text, "194 Temperature_Celsius")) >= 0 ||
        (v = parse_after(text, "Temperature Celsius")) >= 0) {
        if (disk->temp_c <= 0) {
            disk->temp_c = v;
        }
    }
    if ((v = parse_after(text, "9 Power_On_Hours")) >= 0) {
        disk->power_on_hours = v;
        disk->smart_available = 1;
        found = TRUE;
    }
    if ((v = parse_after(text, "5 Reallocated_Sector_Ct")) >= 0) {
        disk->media_errors = (unsigned long long)v;
        disk->smart_available = 1;
        found = TRUE;
    }
    if (disk->smart_available) {
        if (disk->wear_pct > 0) {
            disk->health_pct = 100.0 - disk->wear_pct;
            if (disk->health_pct < 0) disk->health_pct = 0;
        } else if (disk->media_errors == 0) {
            disk->health_pct = 100.0;
        } else {
            disk->health_pct = 90.0;
        }
    }
    return found;
}

void disk_smart_read(DiskInfo *disk, const char *device) {
    /* NVMe sysfs first (no root, no external tool) */
    char path[512];
    snprintf(path, sizeof(path), "/sys/block/%s/device/nvme/smart_log", device);
    char *nvme_dir = NULL;
    /* Try NVMe sysfs via scanning */
    GDir *nvme = g_dir_open("/sys/class/nvme", 0, NULL);
    if (nvme != NULL) {
        const char *entry;
        while ((entry = g_dir_read_name(nvme)) != NULL) {
            char link[512];
            snprintf(link, sizeof(link), "/sys/class/nvme/%s/device", entry);
            /* Match by checking namespace device name via symlink target is hard;
             * just try first nvme smart_log if device starts with nvme */
            if (strncmp(device, "nvme", 4) == 0) {
                snprintf(path, sizeof(path),
                         "/sys/class/nvme/%s/device/smart_log", entry);
                char *t = prism_read_file(path);
                if (t != NULL) {
                    /* binary-ish; also try text files below */
                    g_free(t);
                }
                snprintf(path, sizeof(path),
                         "/sys/class/nvme/%s/device/wear_level", entry);
                char *w = prism_read_first_line(path);
                if (w != NULL) {
                    disk->wear_pct = strtod(w, NULL);
                    disk->smart_available = 1;
                    disk->health_pct = 100.0 - disk->wear_pct;
                    g_free(w);
                }
                snprintf(path, sizeof(path),
                         "/sys/class/nvme/%s/device/media_errors", entry);
                char *e = prism_read_first_line(path);
                if (e != NULL) {
                    disk->media_errors = strtoull(e, NULL, 10);
                    disk->smart_available = 1;
                    g_free(e);
                }
                break;
            }
        }
        g_dir_close(nvme);
    }
    (void)nvme_dir;
    if (disk->smart_available) {
        return;
    }
    /* smartctl fallback via argv spawn (cached by caller cadence ~60s) */
    gchar *argv[] = {"smartctl", "-a", NULL, NULL};
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/%s", device);
    argv[2] = devpath;
    gchar *out = NULL;
    GError *err = NULL;
    if (g_spawn_sync(NULL, argv, NULL,
                     G_SPAWN_SEARCH_PATH,
                     NULL, NULL, &out, NULL, NULL, &err)) {
        if (out != NULL) {
            disk_parse_smart(out, disk);
        }
    }
    g_free(out);
    g_clear_error(&err);
}

void disk_read(DiskReader *reader, Snapshot *snap, double now, double elapsed) {
    DiskStatRaw stats[PRISM_MAX_DISKS * 4];
    int stat_count = 0;
    char *text = prism_read_file("/proc/diskstats");
    if (text != NULL) {
        stat_count = parse_diskstats(text, stats, PRISM_MAX_DISKS * 4);
        g_free(text);
    }
    if (!reader->initialized) {
        reader->prev_count = stat_count;
        for (int i = 0; i < stat_count; i++) {
            reader->prev[i] = stats[i];
        }
        reader->prev_time = now;
        reader->initialized = 1;
        elapsed = 0;
    }
    if (elapsed <= 0) {
        elapsed = now - reader->prev_time;
    }
    if (elapsed <= 0) {
        elapsed = 1;
    }

    int count = 0;
    GDir *dir = g_dir_open("/sys/block", 0, NULL);
    if (dir != NULL) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL && count < PRISM_MAX_DISKS) {
            if (skip_device(name)) {
                continue;
            }
            DiskInfo *disk = &snap->disks[count];
            memset(disk, 0, sizeof(*disk));
            g_strlcpy(disk->device, name, sizeof(disk->device));
            char path[512];
            snprintf(path, sizeof(path), "/sys/block/%s/size", name);
            char *size = prism_read_first_line(path);
            double sectors = size == NULL ? 0 : strtod(size, NULL);
            g_free(size);
            disk->size_gb = sectors * 512.0 / 1000000000.0;
            snprintf(path, sizeof(path), "/sys/block/%s/device/model", name);
            char *model = prism_read_first_line(path);
            if (model != NULL) {
                g_strlcpy(disk->model, model, sizeof(disk->model));
                g_free(model);
            } else {
                g_strlcpy(disk->model, name, sizeof(disk->model));
            }
            snprintf(path, sizeof(path), "/sys/block/%s/queue/rotational", name);
            char *rot = prism_read_first_line(path);
            disk->rotational = rot != NULL && atoi(rot) == 1;
            g_free(rot);
            disk->temp_c = disk_temp(name);
            {
                static double last_smart = -1000;
                if (now - last_smart >= 60.0) {
                    disk_smart_read(disk, name);
                    last_smart = now;
                }
            }
            for (int i = 0; i < stat_count; i++) {
                if (strcmp(stats[i].name, name) != 0) {
                    continue;
                }
                disk->read_total = stats[i].sectors_read * 512ULL;
                disk->write_total = stats[i].sectors_written * 512ULL;
                int prev = prev_index(reader, name);
                if (prev >= 0) {
                    unsigned long long prev_read = reader->prev[prev].sectors_read;
                    unsigned long long prev_write = reader->prev[prev].sectors_written;
                    if (stats[i].sectors_read >= prev_read) {
                        disk->read_bps = (double)(stats[i].sectors_read - prev_read) *
                                         512.0 / elapsed;
                    }
                    if (stats[i].sectors_written >= prev_write) {
                        disk->write_bps = (double)(stats[i].sectors_written - prev_write) *
                                          512.0 / elapsed;
                    }
                }
                break;
            }
            count++;
        }
        g_dir_close(dir);
    }
    snap->disk_count = count;
    reader->prev_count = stat_count > (int)PRISM_ARRAY_LEN(reader->prev)
                             ? (int)PRISM_ARRAY_LEN(reader->prev)
                             : stat_count;
    for (int i = 0; i < reader->prev_count; i++) {
        reader->prev[i] = stats[i];
    }
    reader->prev_time = now;
}

static gboolean fs_is_real(const char *fs) {
    static const char *skip[] = {
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "ramfs", "cgroup",
        "cgroup2", "pstore", "bpf", "tracefs", "debugfs", "securityfs",
        "configfs", "fusectl", "mqueue", "hugetlbfs", "autofs", "binfmt_misc",
        "efivarfs", "rpc_pipefs", "nsfs", "squashfs", "iso9660", "overlay"
    };
    for (size_t i = 0; i < PRISM_ARRAY_LEN(skip); i++) {
        if (strcmp(fs, skip[i]) == 0) {
            return FALSE;
        }
    }
    return TRUE;
}

void mounts_read(Snapshot *snap) {
    int count = 0;
    FILE *file = setmntent("/proc/mounts", "r");
    if (file == NULL) {
        snap->mount_count = 0;
        return;
    }
    struct mntent *entry;
    while ((entry = getmntent(file)) != NULL && count < PRISM_MAX_MOUNTS) {
        if (!fs_is_real(entry->mnt_type)) {
            continue;
        }
        if (strncmp(entry->mnt_fsname, "/dev/", 5) != 0) {
            continue;
        }
        struct statvfs vfs;
        if (statvfs(entry->mnt_dir, &vfs) != 0 || vfs.f_blocks == 0) {
            continue;
        }
        unsigned long long block = vfs.f_frsize > 0 ? vfs.f_frsize : vfs.f_bsize;
        double total = (double)vfs.f_blocks * block / (1024.0 * 1024.0);
        double avail = (double)vfs.f_bavail * block / (1024.0 * 1024.0);
        double used = total - (double)vfs.f_bfree * block / (1024.0 * 1024.0);
        if (total < 64) {
            continue;
        }
        MountInfo *mount = &snap->mounts[count];
        memset(mount, 0, sizeof(*mount));
        g_strlcpy(mount->device, entry->mnt_fsname, sizeof(mount->device));
        g_strlcpy(mount->mount, entry->mnt_dir, sizeof(mount->mount));
        g_strlcpy(mount->fs, entry->mnt_type, sizeof(mount->fs));
        mount->total_mb = total;
        mount->used_mb = used;
        mount->avail_mb = avail;
        mount->used_pct = total > 0 ? used / total * 100.0 : 0;
        mount->read_only = hasmntopt(entry, "ro") != NULL ? 1 : 0;
        count++;
    }
    endmntent(file);
    snap->mount_count = count;
}
