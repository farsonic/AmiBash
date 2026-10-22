/*
 * top - the tasks using the CPU, and memory, for AmigaOS.
 *
 *   top [-d seconds] [-n count] [-b] [-s cpu|pri|name|time]
 *
 * -d   time between updates (default 2, fractions allowed)
 * -n   stop after this many updates
 * -b   batch: no screen handling, one report after another (for files)
 * -s   sort order
 *
 * Keys: q quit, c/p/n/t sort by CPU, priority, name, time, +/- slower,
 * faster, space update now.  CPU times come from tasktime.c (the same
 * code as SysPulse's).
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "tasktime.h"

const char version[] = "$VER: top 0.1 (22.10.2026)";

#define MAXTASKS 256
#define CSI "\033["

enum { BY_CPU, BY_PRI, BY_NAME, BY_TIME };

static TaskInfo tasks[MAXTASKS];
static TaskInfo *order[MAXTASKS];
static int sortby = BY_CPU;
static char out[16384];
static int olen;

static void put(const char *s)
{
    int n = strlen(s);
    if (olen + n >= (int)sizeof(out)) n = sizeof(out) - 1 - olen;
    memcpy(out + olen, s, n);
    olen += n;
}

static void putf(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    put(b);
}

static void flush(void)
{
    if (olen) Write(Output(), out, olen);
    olen = 0;
}

static void size_text(char *b, ULONG v)
{
    if (v >= 10 * 1024 * 1024) sprintf(b, "%luM", v >> 20);
    else if (v >= 1024 * 1024) sprintf(b, "%lu.%luM", v >> 20, ((v & 0xfffff) * 10) >> 20);
    else if (v >= 10 * 1024) sprintf(b, "%luK", v >> 10);
    else sprintf(b, "%lu", v);
}

static void time_text(char *b, ULONG ms)
{
    ULONG s = ms / 1000;
    if (s >= 3600) sprintf(b, "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    else sprintf(b, "%lu:%02lu.%02lu", s / 60, s % 60, (ms % 1000) / 10);
}

/* a bar of width w for a share 0..1000, coloured by how full it is */
static void bar(int w, ULONG v, int colour)
{
    int i, n = (int)((v * w + 500) / 1000);
    if (n > w) n = w;
    put("[");
    if (colour) put(v >= 800 ? CSI "31m" : v >= 500 ? CSI "33m" : CSI "32m");
    for (i = 0; i < n; i++) put("|");
    if (colour) put(CSI "0m");
    for (; i < w; i++) put(" ");
    put("]");
}

static int nocase(const char *a, const char *b)
{
    while (*a && tolower((UBYTE)*a) == tolower((UBYTE)*b)) a++, b++;
    return tolower((UBYTE)*a) - tolower((UBYTE)*b);
}

static int cmp(const void *a, const void *b)
{
    const TaskInfo *x = *(const TaskInfo **)a, *y = *(const TaskInfo **)b;
    switch (sortby) {
    case BY_PRI:
        if (x->pri != y->pri) return y->pri - x->pri;
        break;
    case BY_NAME:
        return nocase(x->cmd[0] ? x->cmd : x->name, y->cmd[0] ? y->cmd : y->name);
    case BY_TIME:
        if (x->cpu_ms != y->cpu_ms) return x->cpu_ms < y->cpu_ms ? 1 : -1;
        break;
    }
    if (x->cpu != y->cpu) return x->cpu < y->cpu ? 1 : -1;
    if (x->cpu_ms != y->cpu_ms) return x->cpu_ms < y->cpu_ms ? 1 : -1;
    return nocase(x->name, y->name);
}

static void report(SysInfo *sy, MachineInfo *m, int n, int cols, int rows, int colour)
{
    char a[16], b[16], c[16], line[256];
    int i, w = cols - 40, shown;
    ULONG up = sy->uptime_s;
    static const char *const sortname[] = {"CPU", "priority", "name", "time"};

    if (w < 10) w = 10;
    if (w > 50) w = 50;
    putf("top - %s%s%s, %s, Kickstart %s - %lu:%02lu:%02lu - sorted by %s\n",
         m->cpu, m->fpu[0] == 'n' ? "" : strstr(m->fpu, m->cpu) ? " + " : "/",
         m->fpu[0] == 'n' ? "" : strstr(m->fpu, m->cpu) ? "FPU" : m->fpu, m->chipset, m->kickstart,
         up / 3600, (up / 60) % 60, up % 60, sortname[sortby]);
    putf("Tasks: %d, %d ready, %d waiting; %lu switches/s\n", sy->ntasks, sy->nready, sy->nwait,
         sy->interval_ms ? sy->dispatches * 1000 / sy->interval_ms : 0);
    put("CPU  ");
    bar(w, sy->cpu, colour);
    putf(" %3lu.%lu%% busy\n", sy->cpu / 10, sy->cpu % 10);
    if (sy->chip_total) {
        ULONG used = sy->chip_total - sy->chip_free;
        size_text(a, used);
        size_text(b, sy->chip_total);
        size_text(c, sy->chip_largest);
        put("Chip ");
        bar(w, tt_permille(used, sy->chip_total), colour);
        putf(" %s/%s used, largest free %s\n", a, b, c);
    }
    if (sy->fast_total) {
        ULONG used = sy->fast_total - sy->fast_free;
        size_text(a, used);
        size_text(b, sy->fast_total);
        size_text(c, sy->fast_largest);
        put("Fast ");
        bar(w, tt_permille(used, sy->fast_total), colour);
        putf(" %s/%s used, largest free %s\n", a, b, c);
    }
    put("\n");
    if (colour) put(CSI "7m");
    sprintf(line, "%6s %10s %4s %2s %13s %3s %s", "CPU%", "TIME", "PRI", "ST", "STACK", "CLI", "NAME");
    putf("%-*.*s", cols - 1, cols - 1, line);
    if (colour) put(CSI "0m");
    put("\n");
    for (i = 0; i < n; i++) order[i] = &tasks[i];
    qsort(order, n, sizeof(order[0]), cmp);
    shown = rows > 0 ? rows - 8 - (sy->chip_total != 0) - (sy->fast_total != 0) : n;
    if (shown > n) shown = n;
    for (i = 0; i < shown; i++) {
        TaskInfo *t = order[i];
        char st[24], cli[8], name[80];
        time_text(a, t->cpu_ms);
        size_text(b, t->stack_used);
        size_text(c, t->stack_size);
        sprintf(st, "%s/%s", b, c);
        if (t->cli) sprintf(cli, "%ld", t->cli);
        else strcpy(cli, "-");
        if (t->cmd[0] && strcmp(t->cmd, t->name)) sprintf(name, "%s (%s)", t->cmd, t->name);
        else strcpy(name, t->name);
        sprintf(line, "%4lu.%lu %10s %4d %2s %13s %3s %s", t->cpu / 10, t->cpu % 10, a, t->pri,
                t->state == TS_RUN ? "R" : t->state == TS_READY ? "r" : "W", st, cli, name);
        if (colour && t->cpu >= 100) put(t->cpu >= 500 ? CSI "1;31m" : CSI "1m");
        putf("%-.*s", cols - 1, line);
        if (colour && t->cpu >= 100) put(CSI "0m");
        put(rows > 0 ? CSI "K\n" : "\n");
    }
}

/* the console's size: CSI 0 SPACE q -> CSI 1;1;rows;cols SPACE r */
static void con_size(int *rows, int *cols)
{
    BPTR in = Input();
    char buf[48];
    int n = 0;
    long r = 0, c = 0;
    char *p;
    Write(Output(), "\2330 q", 4);
    while (n < (int)sizeof(buf) - 1 && WaitForChar(in, 300000)) {
        if (Read(in, buf + n, 1) != 1) break;
        if (buf[n++] == 'r') break;
    }
    buf[n] = 0;
    /* the reply starts with CSI (0x9b) or ESC [ */
    for (p = buf; *p && (UBYTE)*p != 0x9b && !(*p == '\033' && p[1] == '['); p++)
        ;
    if (*p) {
        long v[4];
        int k = 0;
        p += *p == '\033' ? 2 : 1;
        while (k < 4) {
            v[k++] = strtol(p, &p, 10);
            if (*p != ';') break;
            p++;
        }
        if (k == 4) {
            r = v[2];
            c = v[3];
        }
    }
    if (r > 0 && c > 0) {
        *rows = (int)r;
        *cols = (int)c;
    }
}

/* "2" or "1.5" seconds, in tenths, without floating point */
static long tenths(const char *s)
{
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    v *= 10;
    if (*s == '.' && s[1] >= '0' && s[1] <= '9') v += s[1] - '0';
    return v;
}

int main(int argc, char **argv)
{
    long delay10 = 20, count = -1, i;
    int batch = 0, interactive, rows = 24, cols = 80, n;
    SysInfo sy;
    MachineInfo m;
    BPTR in = Input();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b")) batch = 1;
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay10 = tenths(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) count = atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            const char *s = argv[++i];
            sortby = *s == 'p' ? BY_PRI : *s == 'n' ? BY_NAME : *s == 't' ? BY_TIME : BY_CPU;
        } else {
            printf("usage: top [-d seconds] [-n count] [-b] [-s cpu|pri|name|time]\n");
            return argv[i][1] == 'h' ? 0 : 10;
        }
    }
    if (delay10 < 1) delay10 = 1;
    if (tt_open()) {
        printf("top: no timer.device\n");
        return 20;
    }
    tt_machine(&m);
    interactive = !batch && IsInteractive(in) && IsInteractive(Output());
    if (interactive) {
        SetMode(in, 1);
        put(CSI "?25l" CSI "H" CSI "J");    /* no cursor, clear */
        flush();
    }
    tt_sample(&sy, tasks, MAXTASKS);         /* the start of the first interval */
    for (;;) {
        int quit = 0;
        long waited = 0;
        /* wait for the interval, or a key */
        while (waited < delay10 && !quit) {
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
                quit = 1;
                break;
            }
            if (interactive) {
                if (WaitForChar(in, 100000)) {
                    char k;
                    if (Read(in, &k, 1) == 1) {
                        if (k == 'q' || k == 'Q' || k == 27 || k == 3) quit = 1;
                        else if (k == 'c') sortby = BY_CPU;
                        else if (k == 'p') sortby = BY_PRI;
                        else if (k == 'n') sortby = BY_NAME;
                        else if (k == 't') sortby = BY_TIME;
                        else if (k == '+') delay10 += delay10 >= 10 ? 10 : 2;
                        else if (k == '-') delay10 -= delay10 > 10 ? 10 : delay10 > 2 ? 2 : 0;
                        else if (k == ' ') break;
                    }
                }
            } else
                Delay(5);
            waited++;
        }
        if (quit) break;
        n = tt_sample(&sy, tasks, MAXTASKS);
        if (interactive) {
            con_size(&rows, &cols);
            put(CSI "H");
            report(&sy, &m, n, cols, rows, 1);
            put(CSI "J");
        } else {
            report(&sy, &m, n, 132, 0, 0);
            put("\n");
        }
        flush();
        if (count > 0 && --count == 0) break;
    }
    if (interactive) {
        put(CSI "?25h\n");
        flush();
        SetMode(in, 0);
    }
    tt_close();
    return 0;
}
