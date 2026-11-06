#include "core/common.h"
#include "core/format.h"
#include "sys/proc.h"
#include "ui/app.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/manage.h"
#include "ui/pages.h"
#include "ui/widgets.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PROC_SORT_CPU,
    PROC_SORT_MEM,
    PROC_SORT_PID,
    PROC_SORT_NAME,
    PROC_SORT_USER,
    PROC_SORT_THREADS,
    PROC_SORT_STATE,
    PROC_SORT_IO,
    PROC_SORT_TREE
};

typedef struct ProcessPage ProcessPage;

struct ProcessPage {
    App *app;
    HoverInfo hover;
    GtkWidget *area;
    GtkWidget *count_label;
    const ProcInfo *items[PRISM_MAX_PROCS];
    int count;
    int sort;
    int desc;
    int offset;
    int hover_row;
    int selected_pid;
    ProcDetails details;
    int details_pid;
    gint64 details_at;
};

static const char *sort_names[] = {
    "CPU", "MEMORY", "PID", "NAME", "USER", "THREADS", "STATE", "IO", "TREE"
};

static int contains_ci(const char *haystack, const char *needle) {
    if (needle[0] == '\0') {
        return 1;
    }
    return g_strrstr(haystack, needle) != NULL;
}

static int matches_filter(const ProcInfo *proc, const char *filter) {
    if (filter[0] == '\0') {
        return 1;
    }
    if (contains_ci(proc->name, filter) || contains_ci(proc->user, filter)) {
        return 1;
    }
    char pid[16];
    snprintf(pid, sizeof(pid), "%d", proc->pid);
    return contains_ci(pid, filter);
}

static int compare_proc(const void *a, const void *b, void *data) {
    ProcessPage *page = data;
    const ProcInfo *pa = *(const ProcInfo *const *)a;
    const ProcInfo *pb = *(const ProcInfo *const *)b;
    int result = 0;
    switch (page->sort) {
        case PROC_SORT_CPU:
            result = (pa->cpu_pct > pb->cpu_pct) - (pa->cpu_pct < pb->cpu_pct);
            break;
        case PROC_SORT_MEM:
            result = (pa->rss_kb > pb->rss_kb) - (pa->rss_kb < pb->rss_kb);
            break;
        case PROC_SORT_PID:
            result = (pa->pid > pb->pid) - (pa->pid < pb->pid);
            break;
        case PROC_SORT_NAME:
            result = strcasecmp(pa->name, pb->name);
            break;
        case PROC_SORT_USER:
            result = strcasecmp(pa->user, pb->user);
            break;
        case PROC_SORT_THREADS:
            result = (pa->threads > pb->threads) - (pa->threads < pb->threads);
            break;
        case PROC_SORT_STATE:
            result = (pa->state > pb->state) - (pa->state < pb->state);
            break;
        case PROC_SORT_IO: {
            double ioa = pa->io_read_bps + pa->io_write_bps;
            double iob = pb->io_read_bps + pb->io_write_bps;
            result = (ioa > iob) - (ioa < iob);
            break;
        }
        case PROC_SORT_TREE:
            result = (pa->tree_depth > pb->tree_depth) -
                     (pa->tree_depth < pb->tree_depth);
            if (result == 0) {
                result = strcasecmp(pa->name, pb->name);
            }
            break;
        default:
            break;
    }
    if (result == 0) {
        result = (pa->pid > pb->pid) - (pa->pid < pb->pid);
    }
    return page->desc ? -result : result;
}

static int visible_rows(int height) {
    return (height - 78) / 30;
}

static void rebuild(ProcessPage *page) {
    App *app = page->app;
    int count = 0;
    for (int i = 0; i < app->view->proc_count; i++) {
        const ProcInfo *proc = &app->view->procs[i];
        if (!app->config.kernel_threads && proc->kernel_thread) {
            continue;
        }
        if (!matches_filter(proc, app->filter)) {
            continue;
        }
        page->items[count++] = proc;
    }
    page->count = count;
    qsort_r(page->items, page->count, sizeof(page->items[0]), compare_proc, page);
    int max_offset = page->count > 0 ? page->count - 1 : 0;
    if (page->offset > max_offset) {
        page->offset = max_offset;
    }
    if (page->offset < 0) {
        page->offset = 0;
    }
    char text[128];
    if (app->filter[0] != '\0') {
        snprintf(text, sizeof(text), "%d matched  \xc2\xb7  filter \xe2\x80\x9c%s\xe2\x80\x9d",
                 page->count, app->filter);
    } else {
        unsigned long long threads = 0;
        for (int i = 0; i < page->count; i++) {
            threads += page->items[i]->threads;
        }
        snprintf(text, sizeof(text), "%d processes  \xc2\xb7  %llu threads  \xc2\xb7  sorted by %s",
                 page->count, threads, sort_names[page->sort]);
    }
    if (page->count_label != NULL) {
        gtk_label_set_text(GTK_LABEL(page->count_label), text);
    }
}

static Color cpu_color(const Theme *t, double pct) {
    if (pct >= 75) {
        return t->red;
    }
    if (pct >= 35) {
        return t->orange;
    }
    if (pct >= 10) {
        return t->yellow;
    }
    return t->dim;
}

static const char *state_name(char state) {
    switch (state) {
        case 'R':
            return "running";
        case 'S':
            return "sleeping";
        case 'D':
            return "waiting";
        case 'Z':
            return "zombie";
        case 'T':
            return "stopped";
        case 't':
            return "traced";
        case 'I':
            return "idle";
        case 'X':
            return "dead";
        default:
            return "unknown";
    }
}

static Color state_color(const Theme *t, char state) {
    switch (state) {
        case 'R':
            return t->green;
        case 'D':
            return t->yellow;
        case 'Z':
            return t->red;
        case 'T':
        case 't':
            return t->orange;
        default:
            return t->dim;
    }
}

static int column_at(int width, double x) {
    int pid_w = 62;
    int user_w = 120;
    int cpu_w = 72;
    int mem_w = 86;
    int thread_w = 58;
    int state_w = 54;
    int name_w = width - pid_w - user_w - cpu_w - mem_w - thread_w - state_w - 36;
    if (name_w < 120) {
        name_w = 120;
    }
    int pos = 18 + pid_w;
    if (x < pos) {
        return PROC_SORT_PID;
    }
    pos += name_w;
    if (x < pos) {
        return PROC_SORT_NAME;
    }
    pos += user_w;
    if (x < pos) {
        return PROC_SORT_USER;
    }
    pos += cpu_w;
    if (x < pos) {
        return PROC_SORT_CPU;
    }
    pos += mem_w;
    if (x < pos) {
        return PROC_SORT_MEM;
    }
    pos += thread_w;
    if (x < pos) {
        return PROC_SORT_THREADS;
    }
    return PROC_SORT_STATE;
}

static void sort_by(ProcessPage *page, int field) {
    if (page->sort == field) {
        page->desc = !page->desc;
    } else {
        page->sort = field;
        page->desc = (field == PROC_SORT_CPU || field == PROC_SORT_MEM ||
                      field == PROC_SORT_PID || field == PROC_SORT_THREADS);
    }
    page->offset = 0;
    rebuild(page);
    if (page->area != NULL) {
        gtk_widget_queue_draw(page->area);
    }
}

static void table_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                       gpointer data) {
    (void)area;
    ProcessPage *page = data;
    App *app = page->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);

    int pid_w = 62;
    int user_w = 120;
    int cpu_w = 72;
    int mem_w = 86;
    int thread_w = 58;
    int state_w = 54;
    int name_w = width - pid_w - user_w - cpu_w - mem_w - thread_w - state_w - 36;
    if (name_w < 120) {
        name_w = 120;
    }
    double x_pid = 18;
    double x_name = x_pid + pid_w;
    double x_user = x_name + name_w;
    double x_cpu = x_user + user_w;
    double x_mem = x_cpu + cpu_w;
    double x_threads = x_mem + mem_w;
    double x_state = x_threads + thread_w;

    draw_icon(cr, "processes", (Rect){17, 15, 15, 15}, t->cyan, 1.7);
    text_draw(cr, layout, "PROCESSES", 39, 23, 10.5, PANGO_WEIGHT_BOLD, t->dim,
              TEXT_LEFT);
    char meta[96];
    snprintf(meta, sizeof(meta), "SORT %s %s", sort_names[page->sort],
             page->desc ? "\xe2\x86\x93" : "\xe2\x86\x91");
    text_draw(cr, layout, meta, width - 18, 23, 8.3, PANGO_WEIGHT_BOLD, t->faint,
              TEXT_RIGHT);

    double header_y = 54;
    struct {
        const char *label;
        double x;
        TextAlign align;
        int field;
    } headers[] = {
        {"PID", x_pid, TEXT_LEFT, PROC_SORT_PID},
        {"NAME", x_name, TEXT_LEFT, PROC_SORT_NAME},
        {"USER", x_user, TEXT_LEFT, PROC_SORT_USER},
        {"CPU", x_cpu + cpu_w, TEXT_RIGHT, PROC_SORT_CPU},
        {"MEMORY", x_mem + mem_w, TEXT_RIGHT, PROC_SORT_MEM},
        {"THR", x_threads + thread_w, TEXT_RIGHT, PROC_SORT_THREADS},
        {"STATE", x_state + state_w + 12, TEXT_RIGHT, PROC_SORT_STATE},
    };
    for (int i = 0; i < 7; i++) {
        Color color = page->sort == headers[i].field ? t->accent : t->faint;
        text_draw(cr, layout, headers[i].label, headers[i].x, header_y, 8.3,
                  PANGO_WEIGHT_BOLD, color, headers[i].align);
    }
    draw_separator(cr, t, 18, 66, width - 18, 66);

    int rows = visible_rows(height);
    page->hover_row = -1;
    if (page->hover.active && page->hover.y >= 70) {
        int row = (int)((page->hover.y - 70) / 30);
        if (row >= 0 && row < rows) {
            page->hover_row = row;
        }
    }
    double y = 82;
    for (int r = 0; r < rows; r++) {
        int index = page->offset + r;
        if (index >= page->count) {
            break;
        }
        const ProcInfo *proc = page->items[index];
        Rect row_rect = {8, y - 13, width - 16, 30};
        if (proc->pid == page->selected_pid) {
            rounded_rect(cr, row_rect, 7);
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 0.13);
            cairo_fill(cr);
        } else if (r == page->hover_row) {
            rounded_rect(cr, row_rect, 7);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.035);
            cairo_fill(cr);
        }
        Color state_col = state_color(t, proc->state);
        if (proc->pid == page->selected_pid) {
            cairo_set_source_rgba(cr, t->accent.r, t->accent.g, t->accent.b, 1);
            cairo_rectangle(cr, 8, y - 8, 3, 20);
            cairo_fill(cr);
        }
        char pid[16];
        snprintf(pid, sizeof(pid), "%d", proc->pid);
        text_draw(cr, layout, pid, x_pid + 4, y, 8.6, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_LEFT);
        {
            char disp[80];
            const char *prefix = "";
            if (proc->tree_depth > 0) {
                prefix = proc->tree_depth > 2 ? "      └─ " : "  └─ ";
                snprintf(disp, sizeof(disp), "%s%s", prefix, proc->name);
            } else {
                g_strlcpy(disp, proc->name, sizeof(disp));
            }
            text_draw_ellipsis(cr, layout, disp, x_name, y, 9.5,
                               proc->kernel_thread ? PANGO_WEIGHT_NORMAL : PANGO_WEIGHT_BOLD,
                               proc->kernel_thread ? t->dim : t->text, TEXT_LEFT,
                               name_w - 10);
        }
        char user[32];
        snprintf(user, sizeof(user), "%s", proc->user[0] ? proc->user : "-");
        text_draw_ellipsis(cr, layout, user, x_user, y, 8.5, PANGO_WEIGHT_NORMAL,
                           t->dim, TEXT_LEFT, user_w - 10);
        char cpu[32];
        snprintf(cpu, sizeof(cpu), "%.1f%%", proc->cpu_pct);
        text_draw(cr, layout, cpu, x_cpu + cpu_w, y, 9, PANGO_WEIGHT_BOLD,
                  cpu_color(t, proc->cpu_pct), TEXT_RIGHT);
        char mem[32];
        fmt_bytes((double)proc->rss_kb * 1024.0, mem, sizeof(mem));
        text_draw(cr, layout, mem, x_mem + mem_w, y, 8.6, PANGO_WEIGHT_NORMAL,
                  t->dim, TEXT_RIGHT);
        char threads[16];
        snprintf(threads, sizeof(threads), "%d", proc->threads);
        text_draw(cr, layout, threads, x_threads + thread_w, y, 8.6,
                  PANGO_WEIGHT_NORMAL, t->faint, TEXT_RIGHT);
        const char *state = state_name(proc->state);
        double state_width = text_width(cr, layout, state, 8.2, PANGO_WEIGHT_BOLD);
        double state_x = x_state + state_w + 12;
        rounded_rect(cr, (Rect){state_x - state_width - 8, y - 8, state_width + 8, 16}, 8);
        cairo_set_source_rgba(cr, state_col.r, state_col.g, state_col.b, 0.12);
        cairo_fill(cr);
        text_draw(cr, layout, state, state_x, y, 8.2, PANGO_WEIGHT_BOLD, state_col,
                  TEXT_RIGHT);
        y += 30;
    }

    if (page->count == 0) {
        text_draw(cr, layout, "No processes match this filter", width / 2.0,
                  height / 2.0 - 8, 12, PANGO_WEIGHT_BOLD, t->dim, TEXT_CENTER);
        text_draw(cr, layout, "Clear the search field or enable kernel threads in settings.",
                  width / 2.0, height / 2.0 + 14, 9, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_CENTER);
    } else {
        int first = page->offset + 1;
        int last = page->offset + rows;
        if (last > page->count) {
            last = page->count;
        }
        char range[96];
        snprintf(range, sizeof(range), "showing %d\xe2\x80\x93%d of %d  \xc2\xb7  scroll to browse  \xc2\xb7  click a row for details",
                 first, last, page->count);
        text_draw_ellipsis(cr, layout, range, width / 2.0, height - 18, 8.4,
                           PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER, width - 50);
    }
    g_object_unref(layout);
}

static void table_click(GtkGestureClick *gesture, int n_press, double x, double y,
                        gpointer data) {
    (void)gesture;
    (void)n_press;
    ProcessPage *page = data;
    if (y <= 68) {
        sort_by(page, column_at(gtk_widget_get_width(page->area), x));
        return;
    }
    int row = (int)((y - 70) / 30);
    int index = page->offset + row;
    if (row >= 0 && index >= 0 && index < page->count) {
        page->selected_pid = page->items[index]->pid;
        page->details_pid = -1;
        page->app->proc_selected_pid = page->selected_pid;
        if (page->area != NULL) {
            gtk_widget_queue_draw(page->area);
        }
    }
}

static gboolean table_scroll(GtkEventControllerScroll *controller, double dx,
                             double dy, gpointer data) {
    (void)controller;
    (void)dx;
    ProcessPage *page = data;
    int height = gtk_widget_get_height(page->area);
    int rows = visible_rows(height);
    int max_offset = page->count - rows;
    if (max_offset < 0) {
        max_offset = 0;
    }
    int delta = dy > 0 ? 3 : -3;
    page->offset += delta;
    if (page->offset > max_offset) {
        page->offset = max_offset;
    }
    if (page->offset < 0) {
        page->offset = 0;
    }
    gtk_widget_queue_draw(page->area);
    return TRUE;
}

static void details_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                         gpointer data) {
    (void)area;
    ProcessPage *page = data;
    App *app = page->app;
    const Theme *t = &app->theme;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    draw_card(cr, t, (Rect){1, 1, width - 2, height - 2}, 16);
    draw_icon(cr, "system", (Rect){17, 15, 15, 15}, t->accent, 1.7);
    text_draw(cr, layout, "PROCESS DETAILS", 39, 23, 10.5, PANGO_WEIGHT_BOLD,
              t->dim, TEXT_LEFT);

    const ProcInfo *sel = NULL;
    for (int i = 0; i < page->count; i++) {
        if (page->items[i]->pid == page->selected_pid) {
            sel = page->items[i];
            break;
        }
    }
    if (sel == NULL) {
        text_draw(cr, layout, "Select a process", width / 2.0, height / 2.0 - 10, 13,
                  PANGO_WEIGHT_BOLD, t->dim, TEXT_CENTER);
        text_draw(cr, layout, "Click any row to inspect its command line, memory and state.",
                  width / 2.0, height / 2.0 + 14, 8.8, PANGO_WEIGHT_NORMAL,
                  t->faint, TEXT_CENTER);
        g_object_unref(layout);
        return;
    }

    gint64 now = g_get_monotonic_time();
    if (page->details_pid != sel->pid || now - page->details_at > 700000) {
        if (proc_details(sel->pid, &page->details) == 0) {
            page->details_pid = sel->pid;
        }
        page->details_at = now;
    }
    const ProcDetails *details = &page->details;
    gboolean loaded = page->details_pid == sel->pid;

    char pid[32];
    snprintf(pid, sizeof(pid), "PID %d", sel->pid);
    text_draw(cr, layout, pid, 18, 48, 8.5, PANGO_WEIGHT_BOLD, t->faint, TEXT_LEFT);
    text_draw_ellipsis(cr, layout, sel->name, 18, 70, 18, PANGO_WEIGHT_BOLD,
                       t->text, TEXT_LEFT, width - 36);
    char meta[192];
    snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s  \xc2\xb7  PPID %d",
             sel->user, state_name(sel->state), sel->ppid);
    text_draw_ellipsis(cr, layout, meta, 18, 92, 9, PANGO_WEIGHT_NORMAL, t->dim,
                       TEXT_LEFT, width - 36);
    draw_separator(cr, t, 18, 106, width - 18, 106);

    double y = 126;
    const char *labels[] = {"CPU", "Memory (RSS)", "Virtual memory", "Threads",
                            "Nice", "Priority", "Disk read", "Disk write",
                            "Tree depth"};
    char values[9][64];
    snprintf(values[0], sizeof(values[0]), "%.1f%%", sel->cpu_pct);
    fmt_bytes((double)sel->rss_kb * 1024.0, values[1], sizeof(values[1]));
    fmt_bytes((double)sel->vsz_kb * 1024.0, values[2], sizeof(values[2]));
    snprintf(values[3], sizeof(values[3]), "%d", sel->threads);
    snprintf(values[4], sizeof(values[4]), "%d", sel->nice);
    snprintf(values[5], sizeof(values[5]), "%d", loaded ? details->priority : sel->nice);
    {
        char r[32], w[32];
        fmt_rate(sel->io_read_bps, r, sizeof(r));
        fmt_rate(sel->io_write_bps, w, sizeof(w));
        snprintf(values[6], sizeof(values[6]), "%s  (%llu B)", r, sel->io_read_bytes);
        snprintf(values[7], sizeof(values[7]), "%s  (%llu B)", w, sel->io_write_bytes);
    }
    snprintf(values[8], sizeof(values[8]), "%d", sel->tree_depth);
    for (int i = 0; i < 9; i++) {
        text_draw(cr, layout, labels[i], 18, y, 8.7, PANGO_WEIGHT_NORMAL, t->faint,
                  TEXT_LEFT);
        text_draw(cr, layout, values[i], width - 18, y, 9.2, PANGO_WEIGHT_BOLD,
                  i == 0 ? cpu_color(t, sel->cpu_pct) : t->text, TEXT_RIGHT);
        y += 25;
    }

    draw_separator(cr, t, 18, y + 2, width - 18, y + 2);
    y += 22;
    text_draw(cr, layout, "COMMAND LINE", 18, y, 8.2, PANGO_WEIGHT_BOLD, t->faint,
              TEXT_LEFT);
    y += 18;
    if (loaded && details->cmdline[0] != '\0') {
        text_draw_ellipsis(cr, layout, details->cmdline, 18, y, 8.5,
                           PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT, width - 36);
    } else {
        text_draw(cr, layout, sel->name, 18, y, 8.5, PANGO_WEIGHT_NORMAL, t->dim,
                  TEXT_LEFT);
    }
    y += 26;
    text_draw(cr, layout, "EXECUTABLE", 18, y, 8.2, PANGO_WEIGHT_BOLD, t->faint,
              TEXT_LEFT);
    y += 18;
    text_draw_ellipsis(cr, layout, loaded && details->exe[0] ? details->exe : "unavailable",
                       18, y, 8.3, PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT, width - 36);
    y += 26;
    text_draw(cr, layout, "WORKING DIRECTORY", 18, y, 8.2, PANGO_WEIGHT_BOLD,
              t->faint, TEXT_LEFT);
    y += 18;
    text_draw_ellipsis(cr, layout, loaded && details->cwd[0] ? details->cwd : "unavailable",
                       18, y, 8.3, PANGO_WEIGHT_NORMAL, t->dim, TEXT_LEFT, width - 36);
    y += 28;
    text_draw(cr, layout, "Thread state", 18, y, 8.7, PANGO_WEIGHT_NORMAL, t->faint,
              TEXT_LEFT);
    text_draw(cr, layout, state_name(sel->state), width - 18, y, 9.2,
              PANGO_WEIGHT_BOLD, state_color(t, sel->state), TEXT_RIGHT);
    y += 25;
    text_draw(cr, layout, "Process type", 18, y, 8.7, PANGO_WEIGHT_NORMAL, t->faint,
              TEXT_LEFT);
    text_draw(cr, layout, sel->kernel_thread ? "kernel thread" : "user process",
              width - 18, y, 9.2, PANGO_WEIGHT_BOLD, t->dim, TEXT_RIGHT);

    char hint[128];
    snprintf(hint, sizeof(hint), "End Task sends SIGTERM  \xc2\xb7  Force Kill sends SIGKILL");
    text_draw_ellipsis(cr, layout, hint, width / 2.0, height - 18, 8,
                       PANGO_WEIGHT_NORMAL, t->faint, TEXT_CENTER, width - 36);
    g_object_unref(layout);
}

void page_processes_update(App *app) {
    if (app->process_state == NULL) {
        return;
    }
    ProcessPage *page = app->process_state;
    rebuild(page);
    if (page->area != NULL) {
        gtk_widget_queue_draw(page->area);
    }
}

void page_processes_focus_search(App *app) {
    if (app->search != NULL) {
        gtk_widget_grab_focus(app->search);
    }
}

static const ProcInfo *selected_process(App *app) {
    if (app->process_state == NULL) {
        return NULL;
    }
    ProcessPage *page = app->process_state;
    for (int i = 0; i < page->count; i++) {
        if (page->items[i]->pid == page->selected_pid) {
            return page->items[i];
        }
    }
    return NULL;
}

void page_processes_act(App *app, int force) {
    const ProcInfo *proc = selected_process(app);
    if (proc == NULL) {
        app_notify(app, "Select a process first");
        return;
    }
    if (proc->pid <= 1) {
        app_notify(app, "Refusing to signal PID %d", proc->pid);
        return;
    }
    int signal_number = force ? SIGKILL : SIGTERM;
    if (proc_signal(proc->pid, signal_number) != 0) {
        app_notify(app, "Cannot signal %s (%d)", proc->name, proc->pid);
        return;
    }
    app_notify(app, "%s sent to %s (%d)", force ? "SIGKILL" : "SIGTERM",
               proc->name, proc->pid);
}

void page_processes_pause(App *app) {
    const ProcInfo *proc = selected_process(app);
    if (proc == NULL) {
        app_notify(app, "Select a process first");
        return;
    }
    if (proc->pid <= 1) {
        app_notify(app, "Refusing to signal PID %d", proc->pid);
        return;
    }
    gboolean stopped = proc->state == 'T' || proc->state == 't';
    int signal_number = stopped ? SIGCONT : SIGSTOP;
    if (proc_signal(proc->pid, signal_number) != 0) {
        app_notify(app, "Cannot signal %s (%d)", proc->name, proc->pid);
        return;
    }
    app_notify(app, "%s %s (%d)", stopped ? "Resumed" : "Paused", proc->name,
               proc->pid);
}

static void confirm_end(GtkButton *button, gpointer data) {
    (void)button;
    page_processes_act(data, 0);
}

static void confirm_kill(GtkButton *button, gpointer data) {
    (void)button;
    page_processes_act(data, 1);
}

static void action_end(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const ProcInfo *proc = selected_process(app);
    if (proc != NULL && app->config.confirm_end) {
        char message[384];
        snprintf(message, sizeof(message),
                 "Send SIGTERM to %s (%d)? The process can still save its state "
                 "and exit cleanly.", proc->name, proc->pid);
        manage_confirm_dialog(app, "End process", message, "End process",
                              G_CALLBACK(confirm_end), app);
        return;
    }
    page_processes_act(app, 0);
}

static void action_kill(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const ProcInfo *proc = selected_process(app);
    if (proc != NULL && app->config.confirm_end) {
        char message[384];
        snprintf(message, sizeof(message),
                 "Force kill %s (%d)? Unsaved work will be lost and the process "
                 "cannot handle the signal.", proc->name, proc->pid);
        manage_confirm_dialog(app, "Force kill", message, "Force kill",
                              G_CALLBACK(confirm_kill), app);
        return;
    }
    page_processes_act(app, 1);
}

static void action_pause(GtkButton *button, gpointer data) {
    (void)button;
    page_processes_pause(data);
}

static void renice_done(App *app, const char *command, gpointer data) {
    (void)data;
    const ProcInfo *proc = selected_process(app);
    if (proc == NULL || command == NULL) {
        return;
    }
    int nice = atoi(command);
    if (proc_set_nice(proc->pid, nice) == 0) {
        app_notify(app, "Nice set to %d for %s", nice, proc->name);
    } else {
        app_notify(app, "Cannot set nice (need permission?)");
    }
}

static void affinity_done(App *app, const char *command, gpointer data) {
    (void)data;
    const ProcInfo *proc = selected_process(app);
    if (proc == NULL || command == NULL) {
        return;
    }
    if (proc_set_affinity(proc->pid, command) == 0) {
        app_notify(app, "Affinity %s for %s", command, proc->name);
    } else {
        app_notify(app, "Cannot set affinity (need permission?)");
    }
}

static void action_renice(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (selected_process(app) == NULL) {
        app_notify(app, "Select a process first");
        return;
    }
    manage_command_dialog(app, "Set nice (-20..19)",
                          "Higher nice = lower CPU priority. Needs permission for negative values.",
                          "10", renice_done, app);
}

static void action_affinity(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (selected_process(app) == NULL) {
        app_notify(app, "Select a process first");
        return;
    }
    manage_command_dialog(app, "Set CPU affinity",
                          "CPU list, e.g. 0, 0-3, 0,2,4. Empty = all CPUs.",
                          "0", affinity_done, app);
}

static void action_killtree(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const ProcInfo *proc = selected_process(app);
    if (proc == NULL) {
        app_notify(app, "Select a process first");
        return;
    }
    if (proc_kill_tree(proc->pid, SIGTERM) == 0) {
        app_notify(app, "SIGTERM sent to tree of %s", proc->name);
    } else {
        app_notify(app, "Cannot signal tree of %s", proc->name);
    }
}

GtkWidget *page_processes_create(App *app) {
    ProcessPage *page = g_new0(ProcessPage, 1);
    page->app = app;
    page->sort = PROC_SORT_CPU;
    page->desc = 1;
    page->selected_pid = app->proc_selected_pid;
    app->process_state = page;

    GtkWidget *content = prism_column();
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    page->count_label = prism_label("Waiting for process sample...", "dim");
    gtk_widget_add_css_class(GTK_WIDGET(page->count_label), "subtitle");
    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(page->count_label));
    gtk_box_append(GTK_BOX(toolbar), prism_spacer());

    GtkWidget *end = prism_button("End Task", NULL);
    GtkWidget *kill = prism_button("Force Kill", "danger");
    GtkWidget *pause = prism_button("Pause / Resume", NULL);
    GtkWidget *renice = prism_button("Renice", NULL);
    GtkWidget *aff = prism_button("Affinity", NULL);
    GtkWidget *tree = prism_button("Kill Tree", "danger");
    g_signal_connect(end, "clicked", G_CALLBACK(action_end), app);
    g_signal_connect(kill, "clicked", G_CALLBACK(action_kill), app);
    g_signal_connect(pause, "clicked", G_CALLBACK(action_pause), app);
    g_signal_connect(renice, "clicked", G_CALLBACK(action_renice), app);
    g_signal_connect(aff, "clicked", G_CALLBACK(action_affinity), app);
    g_signal_connect(tree, "clicked", G_CALLBACK(action_killtree), app);
    gtk_box_append(GTK_BOX(toolbar), end);
    gtk_box_append(GTK_BOX(toolbar), pause);
    gtk_box_append(GTK_BOX(toolbar), renice);
    gtk_box_append(GTK_BOX(toolbar), aff);
    gtk_box_append(GTK_BOX(toolbar), tree);
    gtk_box_append(GTK_BOX(toolbar), kill);
    gtk_box_append(GTK_BOX(content), toolbar);

    GtkWidget *row = prism_row();
    page->area = prism_area(app, 760, table_draw, page);
    prism_hover_attach(page->area, &page->hover);
    gtk_widget_set_hexpand(page->area, TRUE);
    gtk_widget_set_size_request(page->area, 760, 760);
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(table_click), page);
    gtk_widget_add_controller(page->area, GTK_EVENT_CONTROLLER(click));
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(table_scroll), page);
    gtk_widget_add_controller(page->area, scroll);
    gtk_box_append(GTK_BOX(row), page->area);

    GtkWidget *details = prism_area(app, 760, details_draw, page);
    gtk_widget_set_size_request(details, 350, 760);
    gtk_widget_set_hexpand(details, FALSE);
    gtk_box_append(GTK_BOX(row), details);
    gtk_box_append(GTK_BOX(content), row);

    g_object_set_data_full(G_OBJECT(content), "process-page", page, NULL);
    app->pages[PAGE_PROCESSES].root = content;
    rebuild(page);
    return prism_page(content);
}
