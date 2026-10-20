/*
 * utils_misc.c - shared helpers for the built-in utilities, small
 * utilities (basename, dirname, sleep, date, env, printenv, clear, xargs,
 * uname, whoami, hostname) and the util_builtins table.
 */
#include <string.h>
#include <ctype.h>
#include "sh.h"
#include "utils.h"

/* ================================================================== */
/* option parser                                                       */
/* ================================================================== */

void ug_init(UGetopt *g, int argc, char **argv, const char *opts, const char *const *lopts)
{
    memset(g, 0, sizeof(*g));
    g->argc = argc;
    g->argv = argv;
    g->i = 1;
    g->sp = 0;
    g->opts = opts;
    g->lopts = lopts;
    g->cmd = argv[0];
    g->ops = xmalloc((argc + 1) * sizeof(char *));
    g->ops[0] = NULL;
}

void ug_free(UGetopt *g)
{
    xfree(g->ops);
    g->ops = NULL;
}

static int all_digits(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static int ug_long(UGetopt *g, char *a)
{
    const char *name = a + 2;
    const char *eq = strchr(name, '=');
    size_t len = eq ? (size_t)(eq - name) : strlen(name);
    int i, found = -1, nfound = 0;
    char kind = 0;
    g->i++;
    if (g->lopts) {
        for (i = 0; g->lopts[i]; i++) {
            const char *lo = g->lopts[i];
            size_t ll = strcspn(lo, "=?");
            if (ll == len && strncmp(lo, name, len) == 0) {
                found = i;
                nfound = 1;
                break;
            }
            if (ll > len && strncmp(lo, name, len) == 0) {
                found = i;
                nfound++;
            }
        }
    }
    if (nfound != 1) {
        if (nfound == 0 && strncmp(name, "help", len) == 0 && len > 0) return UG_LONG + 999;
        sh_cmd_error(g->cmd, nfound ? "option '%s' is ambiguous" : "unrecognized option '%s'", a);
        return '?';
    }
    kind = g->lopts[found][strcspn(g->lopts[found], "=?")];
    if (kind == '=') {
        if (eq) {
            g->arg = (char *)eq + 1;
        } else if (g->i < g->argc) {
            g->arg = g->argv[g->i++];
        } else {
            sh_cmd_error(g->cmd, "option '%s' requires an argument", a);
            return '?';
        }
    } else if (kind == '?') {
        g->arg = eq ? (char *)eq + 1 : NULL;
    } else if (eq) {
        sh_cmd_error(g->cmd, "option '--%.*s' doesn't allow an argument", (int)len, name);
        return '?';
    }
    return UG_LONG + found;
}

int ug_next(UGetopt *g)
{
    char *a;
    const char *cp;
    int c;
    g->arg = NULL;
    if (g->sp == 0) {
        for (;;) {
            if (g->i >= g->argc) {
                g->ops[g->nops] = NULL;
                return -1;
            }
            a = g->argv[g->i];
            if (g->done) {
                g->ops[g->nops++] = a;
                g->i++;
                continue;
            }
            if (strcmp(a, "--") == 0) {
                g->done = 1;
                g->i++;
                continue;
            }
            if (a[0] != '-' || a[1] == 0) {
                if (g->stop_nonopt) {
                    g->done = 1;
                    continue;
                }
                g->ops[g->nops++] = a;
                g->i++;
                continue;
            }
            break;
        }
        if (a[1] == '-') return ug_long(g, a);
        if (g->allow_num && all_digits(a + 1)) {
            g->arg = a + 1;
            g->i++;
            return UG_NUM;
        }
        g->sp = 1;
    }
    a = g->argv[g->i];
    c = (unsigned char)a[g->sp++];
    cp = c ? strchr(g->opts, c) : NULL;
    if (!cp || c == ':') {
        sh_cmd_error(g->cmd, "invalid option -- '%c'", c);
        if (!a[g->sp]) {
            g->i++;
            g->sp = 0;
        }
        return '?';
    }
    if (cp[1] == ':') {
        if (a[g->sp]) {
            g->arg = a + g->sp;
        } else if (cp[2] == ':') {
            g->arg = NULL;
        } else if (g->i + 1 < g->argc) {
            g->i++;
            g->arg = g->argv[g->i];
        } else {
            sh_cmd_error(g->cmd, "option requires an argument -- '%c'", c);
            g->i++;
            g->sp = 0;
            return '?';
        }
        g->i++;
        g->sp = 0;
        return c;
    }
    if (!a[g->sp]) {
        g->i++;
        g->sp = 0;
    }
    return c;
}

/* ================================================================== */
/* misc helpers                                                        */
/* ================================================================== */

static const char *usage_of(const char *cmd)
{
    int i;
    for (i = 0; util_builtins[i].name; i++)
        if (strcmp(util_builtins[i].name, cmd) == 0) return util_builtins[i].usage;
    return cmd;
}

int u_usage(const char *cmd)
{
    sh_printf(2, "Usage: %s\n", usage_of(cmd));
    return 2;
}

static int u_help(const char *cmd)
{
    sh_printf(1, "Usage: %s\n", usage_of(cmd));
    return 0;
}

int u_atoi(const char *s, long *out)
{
    long v = 0;
    int neg = 0;
    if (*s == '-') neg = 1, s++;
    else if (*s == '+') s++;
    if (!isdigit((unsigned char)*s)) return 0;
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    if (*s) return 0;
    *out = neg ? -v : v;
    return 1;
}

char *u_ultoa(unsigned long v)
{
    static char buf[24];
    char *p = buf + sizeof(buf) - 1;
    *p = 0;
    do {
        *--p = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    return p;
}

char *u_size_str(double sz, int human)
{
    static const char units[] = "KMGTPE";
    int u = -1;
    if (sz < 0) sz = 0;
    if (!human || sz < 1024.0) {
        if (sz < 4294967295.0) return xstrdup(u_ultoa((unsigned long)sz));
        {
            /* >= 4 GB: split into billions and remainder */
            unsigned long hi = (unsigned long)(sz / 1e9);
            double rem = sz - (double)hi * 1e9;
            if (rem < 0) rem = 0;
            return str_printf("%lu%09lu", hi, (unsigned long)rem);
        }
    }
    while (sz >= 1024.0 && u < 5) {
        sz /= 1024.0;
        u++;
    }
    if (sz < 10.0) {
        unsigned long d = (unsigned long)(sz * 10.0);
        if ((double)d < sz * 10.0) d++;             /* round up like GNU */
        if (d >= 100) return str_printf("10%c", units[u]);
        return str_printf("%lu.%lu%c", d / 10, d % 10, units[u]);
    } else {
        unsigned long d = (unsigned long)sz;
        if ((double)d < sz) d++;
        if (d >= 1024 && u < 5) return str_printf("1.0%c", units[u + 1]);
        return str_printf("%lu%c", d, units[u]);
    }
}

void u_msort(void **v, int n, UCmp cmp, void *ud)
{
    void **tmp, **a, **b;
    int w, i;
    if (n < 2) return;
    tmp = xmalloc(n * sizeof(void *));
    a = v;
    b = tmp;
    for (w = 1; w < n; w *= 2) {
        for (i = 0; i < n; i += 2 * w) {
            int lo = i, mid = i + w < n ? i + w : n, hi = i + 2 * w < n ? i + 2 * w : n;
            int p = lo, q = mid, k = lo;
            while (p < mid && q < hi) b[k++] = cmp(a[q], a[p], ud) < 0 ? a[q++] : a[p++];
            while (p < mid) b[k++] = a[p++];
            while (q < hi) b[k++] = a[q++];
        }
        { void **t = a; a = b; b = t; }
    }
    if (a != v) memcpy(v, a, n * sizeof(void *));
    xfree(tmp);
}

static int brk_flag;

int u_break(void)
{
    if (os_check_break()) brk_flag = 1;
    return brk_flag;
}

void u_break_reset(void)
{
    brk_flag = 0;
}

ShFile *u_open_in(const char *cmd, const char *path)
{
    ShFile *f;
    if (strcmp(path, "-") == 0) {
        f = sh_fd(0);
        return f ? shf_ref(f) : shf_null();
    }
    f = shf_open(path, OS_RD);
    if (!f) {
        if (sh_is_dir(path)) sh_cmd_error(cmd, "%s: Is a directory", path);
        else sh_cmd_error(cmd, "%s: %s", path, os_errstr());
    }
    return f;
}

void u_close_in(ShFile *f)
{
    shf_close(f);
}

int u_out_is_tty(int *cols)
{
    ShFile *f = sh_fd(1);
    int r, c;
    if (cols) *cols = 80;
    if (!f || f->kind != SHF_OS || !f->tty) return 0;
    if (os_console_size(f->h, &r, &c) == 0 && c > 0 && cols) *cols = c;
    return 1;
}

#define CPBUF 8192

int u_copy_file(const char *cmd, const char *from, const char *to, int preserve)
{
    char *nf = path_native(from), *nt = path_native(to);
    OsHandle in, out;
    char *buf;
    long n;
    int rc = 0;
    OsDirEnt st;
    in = os_open(nf, OS_RD);
    if (!in) {
        sh_cmd_error(cmd, "cannot open '%s' for reading: %s", from, os_errstr());
        xfree(nf);
        xfree(nt);
        return 1;
    }
    out = os_open(nt, OS_WR);
    if (!out) {
        sh_cmd_error(cmd, "cannot create regular file '%s': %s", to, os_errstr());
        os_close(in);
        xfree(nf);
        xfree(nt);
        return 1;
    }
    buf = xmalloc(CPBUF);
    while ((n = os_read(in, buf, CPBUF)) > 0) {
        if (os_write(out, buf, n) != n) {
            sh_cmd_error(cmd, "error writing '%s': %s", to, os_errstr());
            rc = 1;
            break;
        }
        if (u_break()) {
            rc = 130;
            break;
        }
    }
    if (n < 0) {
        sh_cmd_error(cmd, "error reading '%s': %s", from, os_errstr());
        rc = 1;
    }
    xfree(buf);
    os_close(in);
    os_close(out);
    if (rc == 0 && os_stat(nf, &st) == 0) {
        os_setprot(nt, st.prot);
        if (preserve) {
            os_setdate(nt, st.mtime);
            if (st.comment[0]) os_setcomment(nt, st.comment);
        }
    }
    xfree(nf);
    xfree(nt);
    return rc;
}

/* read the names in a directory (sorted) */
static int list_dir(const char *path, StrList *out)
{
    void *d = sh_opendir(path);
    OsDirEnt e;
    sl_init(out);
    if (!d) return -1;
    while (os_readdir(d, &e)) sl_adddup(out, e.name);
    os_closedir(d);
    sl_sort(out, 1);
    return 0;
}

int u_remove_tree(const char *cmd, const char *path, int verbose)
{
    OsDirEnt e;
    int rc = 0, i;
    if (u_break()) return 1;
    if (sh_stat(path, &e) == 0 && e.type == OS_T_DIR) {
        StrList l;
        if (list_dir(path, &l) != 0) {
            sh_cmd_error(cmd, "cannot open directory '%s': %s", path, os_errstr());
            return 1;
        }
        for (i = 0; i < l.n && !u_break(); i++) {
            char *p = path_join(path, l.v[i]);
            rc |= u_remove_tree(cmd, p, verbose);
            xfree(p);
        }
        sl_free(&l);
        if (rc || brk_flag) return 1;
        if (sh_remove(path) != 0) {
            sh_cmd_error(cmd, "cannot remove '%s': %s", path, os_errstr());
            return 1;
        }
        if (verbose) sh_printf(1, "removed directory '%s'\n", path);
        return 0;
    }
    if (sh_remove(path) != 0) {
        sh_cmd_error(cmd, "cannot remove '%s': %s", path, os_errstr());
        return 1;
    }
    if (verbose) sh_printf(1, "removed '%s'\n", path);
    return 0;
}

int u_copy_tree(const char *cmd, const char *from, const char *to, int preserve, int verbose)
{
    OsDirEnt e;
    int rc = 0, i;
    if (u_break()) return 1;
    if (sh_stat(from, &e) != 0) {
        sh_cmd_error(cmd, "cannot stat '%s': %s", from, os_errstr());
        return 1;
    }
    if (e.type == OS_T_DIR || e.type == OS_T_VOL) {
        StrList l;
        if (!sh_is_dir(to) && sh_mkdir(to) != 0) {
            sh_cmd_error(cmd, "cannot create directory '%s': %s", to, os_errstr());
            return 1;
        }
        if (verbose) sh_printf(1, "'%s' -> '%s'\n", from, to);
        if (list_dir(from, &l) != 0) {
            sh_cmd_error(cmd, "cannot open directory '%s': %s", from, os_errstr());
            return 1;
        }
        for (i = 0; i < l.n && !u_break(); i++) {
            char *a = path_join(from, l.v[i]);
            char *b = path_join(to, l.v[i]);
            rc |= u_copy_tree(cmd, a, b, preserve, verbose);
            xfree(a);
            xfree(b);
        }
        sl_free(&l);
        if (preserve) {
            char *nt = path_native(to);
            os_setdate(nt, e.mtime);
            xfree(nt);
        }
        return rc || brk_flag;
    }
    rc = u_copy_file(cmd, from, to, preserve);
    if (rc == 0 && verbose) sh_printf(1, "'%s' -> '%s'\n", from, to);
    return rc;
}

/* ================================================================== */
/* basename / dirname                                                  */
/* ================================================================== */

static void do_basename(const char *p, const char *suf, int term)
{
    char *s = xstrdup(p);
    int n = (int)strlen(s);
    const char *b;
    char *r;
    while (n > 1 && s[n - 1] == '/') s[--n] = 0;
    b = path_basename(s);
    r = xstrdup(b);
    if (suf && *suf) {
        int lr = (int)strlen(r), ls = (int)strlen(suf);
        if (lr > ls && strcmp(r + lr - ls, suf) == 0) r[lr - ls] = 0;
    }
    sh_puts(1, r);
    sh_putc(1, term);
    xfree(r);
    xfree(s);
}

int util_basename(int argc, char **argv)
{
    static const char *const lo[] = { "multiple", "suffix=", "zero", NULL };
    UGetopt g;
    int c, multi = 0, term = '\n', i;
    const char *suf = NULL;
    ug_init(&g, argc, argv, "as:z", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'a': case UG_LONG + 0: multi = 1; break;
        case 's': case UG_LONG + 1: suf = g.arg; multi = 1; break;
        case 'z': case UG_LONG + 2: term = 0; break;
        case UG_LONG + 999: ug_free(&g); return u_help(argv[0]);
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops < 1) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    if (!multi) {
        if (g.nops > 2) {
            sh_cmd_error(argv[0], "extra operand '%s'", g.ops[2]);
            ug_free(&g);
            return 1;
        }
        do_basename(g.ops[0], g.nops == 2 ? g.ops[1] : NULL, term);
    } else {
        for (i = 0; i < g.nops; i++) do_basename(g.ops[i], suf, term);
    }
    ug_free(&g);
    return 0;
}

int util_dirname(int argc, char **argv)
{
    static const char *const lo[] = { "zero", NULL };
    UGetopt g;
    int c, i, term = '\n';
    ug_init(&g, argc, argv, "z", lo);
    while ((c = ug_next(&g)) != -1) {
        if (c == 'z' || c == UG_LONG) term = 0;
        else if (c == UG_LONG + 999) { ug_free(&g); return u_help(argv[0]); }
        else { ug_free(&g); return u_usage(argv[0]); }
    }
    if (g.nops < 1) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops; i++) {
        char *d = path_dirname(g.ops[i]);
        sh_puts(1, d);
        sh_putc(1, term);
        xfree(d);
    }
    ug_free(&g);
    return 0;
}

/* ================================================================== */
/* sleep                                                               */
/* ================================================================== */

/* parse "1.5", "2m", ".25h" into milliseconds; -1 on error */
static long parse_duration(const char *s)
{
    unsigned long ip = 0, frac = 0, scale = 1000;
    long mult = 1000;
    int any = 0;
    while (isdigit((unsigned char)*s)) {
        ip = ip * 10 + (*s++ - '0');
        any = 1;
    }
    if (*s == '.') {
        unsigned long div = 1;
        s++;
        while (isdigit((unsigned char)*s)) {
            if (div < 1000000UL) {
                frac = frac * 10 + (*s - '0');
                div *= 10;
            }
            s++;
            any = 1;
        }
        scale = div;
    } else {
        scale = 1;
    }
    if (!any) return -1;
    switch (*s) {
    case 0: case 's': break;
    case 'm': mult = 60000L; break;
    case 'h': mult = 3600000L; break;
    case 'd': mult = 86400000L; break;
    default: return -1;
    }
    if (*s && s[1]) return -1;
    if (ip > 2000000000UL / (unsigned long)mult) return 2000000000L;
    return (long)(ip * mult) + (long)((frac * (unsigned long)mult) / scale);
}

int util_sleep(int argc, char **argv)
{
    long total = 0, start;
    int i;
    if (argc < 2) {
        sh_cmd_error(argv[0], "missing operand");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        long ms;
        if (strcmp(argv[i], "--help") == 0) return u_help(argv[0]);
        ms = parse_duration(argv[i]);
        if (ms < 0) {
            sh_cmd_error(argv[0], "invalid time interval '%s'", argv[i]);
            return 1;
        }
        total += ms;
    }
    sh_flush_all();
    start = os_time_ms();
    while (total > 0) {
        long chunk = total > 100 ? 100 : total;
        long now;
        os_sleep_ms(chunk);
        if (os_check_break()) return 130;
        now = os_time_ms();
        if (now >= start && now - start <= chunk + 1000) total -= now - start;
        else total -= chunk;
        start = now;
    }
    return 0;
}

/* ================================================================== */
/* date                                                                */
/* ================================================================== */

static int scan_num(const char **sp, int maxdig, long *out)
{
    const char *s = *sp;
    long v = 0;
    int n = 0;
    while (n < maxdig && isdigit((unsigned char)*s)) {
        v = v * 10 + (*s++ - '0');
        n++;
    }
    if (!n) return 0;
    *sp = s;
    *out = v;
    return 1;
}

/* parse a -d string; returns 0 ok */
int u_parse_date(const char *s, int utc, long *out)
{
    ShTm tm;
    long now = os_time(), y, mo, d, h = 0, mi = 0, se = 0, t;
    long off = utc ? 0 : os_tz_offset();
    int have_date = 0, have_tz = 0;
    long tzoff = 0;
    while (*s == ' ') s++;
    if (*s == '@') {
        long v;
        if (!u_atoi(s + 1, &v)) return -1;
        *out = v;
        return 0;
    }
    if (str_casecmp(s, "now") == 0 || !*s) {
        *out = now;
        return 0;
    }
    sh_gmtime(now + off, &tm);
    tm.hour = tm.min = tm.sec = 0;
    if (str_casecmp(s, "today") == 0) {
        *out = sh_timegm(&tm) - off;
        return 0;
    }
    if (str_casecmp(s, "yesterday") == 0 || str_casecmp(s, "tomorrow") == 0) {
        sh_gmtime(now + off, &tm);
        *out = now + (s[0] == 'y' || s[0] == 'Y' ? -86400L : 86400L);
        return 0;
    }
    if (isdigit((unsigned char)s[0])) {
        const char *p = s;
        if (scan_num(&p, 4, &y) && p - s == 4 && *p == '-' ) {
            p++;
            if (!scan_num(&p, 2, &mo) || *p++ != '-' || !scan_num(&p, 2, &d)) return -1;
            tm.year = (int)y;
            tm.mon = (int)mo - 1;
            tm.mday = (int)d;
            have_date = 1;
            s = p;
            if (*s == 'T' || *s == 't' || *s == ' ') s++;
            while (*s == ' ') s++;
        } else {
            p = s;
        }
    }
    if (isdigit((unsigned char)*s)) {
        const char *p = s;
        if (!scan_num(&p, 2, &h) || *p++ != ':' || !scan_num(&p, 2, &mi)) return -1;
        if (*p == ':') {
            p++;
            if (!scan_num(&p, 2, &se)) return -1;
            if (*p == '.') {
                p++;
                while (isdigit((unsigned char)*p)) p++;
            }
        }
        tm.hour = (int)h;
        tm.min = (int)mi;
        tm.sec = (int)se;
        s = p;
    } else if (!have_date) {
        return -1;
    }
    while (*s == ' ') s++;
    if (*s == 'Z' || *s == 'z' || str_casecmp(s, "UTC") == 0 || str_casecmp(s, "GMT") == 0) {
        have_tz = 1;
        s += (*s == 'Z' || *s == 'z') ? 1 : 3;
    } else if (*s == '+' || *s == '-') {
        int sg = *s == '-' ? -1 : 1;
        long hh, mm = 0;
        const char *p = s + 1;
        if (!scan_num(&p, 2, &hh)) return -1;
        if (*p == ':') p++;
        scan_num(&p, 2, &mm);
        tzoff = sg * (hh * 3600 + mm * 60);
        have_tz = 1;
        s = p;
    }
    while (*s == ' ') s++;
    if (*s) return -1;
    t = sh_timegm(&tm);
    *out = have_tz ? t - tzoff : t - off;
    return 0;
}

int util_date(int argc, char **argv)
{
    static const char *const lo[] = { "date=", "utc", "universal", "reference=", "iso-8601?", "rfc-2822", "rfc-email", NULL };
    UGetopt g;
    int c, utc = 0, i, rc = 0;
    const char *dstr = NULL, *ref = NULL, *fmtstr = NULL, *iso = NULL;
    int rfc = 0, isoset = 0;
    long t, off;
    ShTm tm;
    char buf[512];
    ug_init(&g, argc, argv, "d:ur:I::R", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'd': case UG_LONG + 0: dstr = g.arg; break;
        case 'u': case UG_LONG + 1: case UG_LONG + 2: utc = 1; break;
        case 'r': case UG_LONG + 3: ref = g.arg; break;
        case 'I': case UG_LONG + 4: isoset = 1; iso = g.arg; break;
        case 'R': case UG_LONG + 5: case UG_LONG + 6: rfc = 1; break;
        case UG_LONG + 999: ug_free(&g); return u_help(argv[0]);
        default: ug_free(&g); return 1;
        }
    }
    for (i = 0; i < g.nops; i++) {
        if (g.ops[i][0] == '+' && !fmtstr) {
            fmtstr = g.ops[i] + 1;
        } else {
            sh_cmd_error(argv[0], g.ops[i][0] == '+' ? "extra operand '%s'" :
                         "setting the date is not supported ('%s')", g.ops[i]);
            ug_free(&g);
            return 1;
        }
    }
    ug_free(&g);
    t = os_time();
    if (ref) {
        OsDirEnt e;
        if (sh_stat(ref, &e) != 0) {
            sh_cmd_error(argv[0], "%s: %s", ref, os_errstr());
            return 1;
        }
        t = e.mtime;
    }
    if (dstr && u_parse_date(dstr, utc, &t) != 0) {
        sh_cmd_error(argv[0], "invalid date '%s'", dstr);
        return 1;
    }
    off = utc ? 0 : os_tz_offset();
    sh_gmtime(t + off, &tm);
    if (isoset || rfc) {
        char z[8];
        long a = off < 0 ? -off : off;
        sh_strftime_tz(z, sizeof(z), "%z", &tm, off, utc);
        if (rfc) {
            sh_strftime_tz(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", &tm, off, utc);
        } else {
            const char *f = "%Y-%m-%d";
            char colz[8];
            colz[0] = off < 0 ? '-' : '+';
            colz[1] = (char)('0' + a / 36000);
            colz[2] = (char)('0' + a / 3600 % 10);
            colz[3] = ':';
            colz[4] = (char)('0' + a / 600 % 6);
            colz[5] = (char)('0' + a / 60 % 10);
            colz[6] = 0;
            if (!iso || str_startswith("date", iso)) f = "%Y-%m-%d";
            else if (str_startswith("hours", iso)) f = "%Y-%m-%dT%H";
            else if (str_startswith("minutes", iso)) f = "%Y-%m-%dT%H:%M";
            else if (str_startswith("seconds", iso) || str_startswith("ns", iso)) f = "%Y-%m-%dT%H:%M:%S";
            else {
                sh_cmd_error(argv[0], "invalid argument '%s' for '--iso-8601'", iso);
                return 1;
            }
            sh_strftime_tz(buf, sizeof(buf), f, &tm, off, utc);
            if (iso && !str_startswith("date", iso)) {
                size_t l = strlen(buf);
                strcpy(buf + l, colz);
            }
        }
    } else {
        sh_strftime_tz(buf, sizeof(buf), fmtstr ? fmtstr : "%a %b %e %H:%M:%S %Z %Y", &tm, off, utc);
    }
    sh_puts(1, buf);
    sh_putc(1, '\n');
    return rc;
}

/* ================================================================== */
/* env / printenv                                                      */
/* ================================================================== */

static int is_exported(const char *name)
{
    Var *v = var_lookup(name);
    return v && (v->flags & V_EXPORT) && !(v->flags & (V_UNSET | V_ARRAY | V_ASSOC));
}

static void print_env(int term)
{
    StrList names;
    int i;
    sl_init(&names);
    var_names(&names, "");
    for (i = 0; i < names.n; i++) {
        const char *val;
        if (!is_exported(names.v[i])) continue;
        val = var_get(names.v[i]);
        if (!val) continue;
        sh_printf(1, "%s=%s", names.v[i], val);
        sh_putc(1, term);
    }
    sl_free(&names);
}

typedef struct EnvSave { char *name; char *val; int flags; int existed; } EnvSave;

int util_env(int argc, char **argv)
{
    static const char *const lo[] = { "ignore-environment", "unset=", "null", NULL };
    UGetopt g;
    int c, i, rc = 0, ignore = 0, term = '\n', nsave = 0, first;
    EnvSave *save;
    StrList cleared;
    ug_init(&g, argc, argv, "iu:0", lo);
    g.stop_nonopt = 1;
    save = xcalloc((argc + 1) * sizeof(EnvSave));
    sl_init(&cleared);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'i': case UG_LONG + 0: ignore = 1; break;
        case 'u': case UG_LONG + 1: {
            Var *v = var_lookup(g.arg);
            if (v && (v->flags & V_EXPORT)) {
                v->flags &= ~V_EXPORT;
                sl_adddup(&cleared, g.arg);
            }
            break;
        }
        case '0': case UG_LONG + 2: term = 0; break;
        case UG_LONG + 999: rc = u_help(argv[0]); goto out;
        default: rc = 125; goto out;
        }
    }
    if (ignore) {
        StrList names;
        sl_init(&names);
        var_names(&names, "");
        for (i = 0; i < names.n; i++) {
            Var *v = var_lookup(names.v[i]);
            if (v && (v->flags & V_EXPORT)) {
                v->flags &= ~V_EXPORT;
                sl_adddup(&cleared, names.v[i]);
            }
        }
        sl_free(&names);
    }
    if (g.nops > 0 && strcmp(g.ops[0], "-") == 0 && !ignore) {
        /* "env -" is the same as -i; ops start after it */
    }
    first = 0;
    for (; first < g.nops; first++) {
        char *a = g.ops[first], *eq = strchr(a, '=');
        char *name;
        Var *v;
        if (first == 0 && strcmp(a, "-") == 0) continue;
        if (!eq || eq == a) break;
        name = xstrndup(a, eq - a);
        v = var_lookup(name);
        save[nsave].name = name;
        save[nsave].existed = v != NULL;
        save[nsave].flags = v ? v->flags : 0;
        save[nsave].val = v && v->val ? xstrdup(v->val) : NULL;
        nsave++;
        if (var_set(name, eq + 1, V_EXPORT) != 0) {
            rc = 125;
            goto out;
        }
        v = var_lookup(name);
        if (v) v->flags |= V_EXPORT;
    }
    if (first >= g.nops) {
        print_env(term);
    } else {
        sh_flush_all();
        rc = exec_argv(g.nops - first, g.ops + first);
    }
out:
    for (i = nsave - 1; i >= 0; i--) {
        if (save[i].existed) {
            Var *v;
            var_set(save[i].name, save[i].val ? save[i].val : "", 0);
            v = var_lookup(save[i].name);
            if (v) v->flags = save[i].flags;
        } else {
            var_unset(save[i].name);
        }
        xfree(save[i].name);
        xfree(save[i].val);
    }
    for (i = 0; i < cleared.n; i++) {
        Var *v = var_lookup(cleared.v[i]);
        if (v) v->flags |= V_EXPORT;
    }
    sl_free(&cleared);
    xfree(save);
    ug_free(&g);
    return rc;
}

int util_printenv(int argc, char **argv)
{
    static const char *const lo[] = { "null", NULL };
    UGetopt g;
    int c, i, rc = 0, term = '\n';
    ug_init(&g, argc, argv, "0", lo);
    while ((c = ug_next(&g)) != -1) {
        if (c == '0' || c == UG_LONG) term = 0;
        else if (c == UG_LONG + 999) { ug_free(&g); return u_help(argv[0]); }
        else { ug_free(&g); return 2; }
    }
    if (g.nops == 0) {
        print_env(term);
    } else {
        for (i = 0; i < g.nops; i++) {
            const char *v = is_exported(g.ops[i]) ? var_get(g.ops[i]) : NULL;
            if (v) {
                sh_puts(1, v);
                sh_putc(1, term);
            } else {
                rc = 1;
            }
        }
    }
    ug_free(&g);
    return rc;
}

int util_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_puts(1, "\033[H\033[J");
    return 0;
}

/* ================================================================== */
/* xargs                                                               */
/* ================================================================== */

typedef struct XIn {
    ShFile *f;
    int delim;          /* -1 = whitespace with quotes, else delimiter  */
    int eof;
    int lines;          /* count of logical lines consumed              */
} XIn;

/* read the next argument; returns 1 with word in w, 0 at EOF, -1 error.
 * *eol set when the word ended a line (for -L). */
static int x_word(XIn *in, SBuf *w, int *eol)
{
    int c, q = 0, any = 0;
    sb_clear(w);
    sb_addn(w, "", 0);
    *eol = 0;
    if (in->eof) return 0;
    if (in->delim >= 0) {
        for (;;) {
            c = shf_getc(in->f);
            if (c == -1) {
                in->eof = 1;
                return any;
            }
            any = 1;
            if (c == in->delim) {
                *eol = 1;
                return 1;
            }
            sb_addc(w, c);
        }
    }
    for (;;) {
        c = shf_getc(in->f);
        if (c == -1) {
            in->eof = 1;
            if (q) {
                sh_cmd_error("xargs", "unmatched %s quote; by default quotes are special to xargs unless you use the -0 option",
                             q == '"' ? "double" : "single");
                return -1;
            }
            return any;
        }
        if (q) {
            if (c == q) q = 0;
            else sb_addc(w, c);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n') {
            if (any) {
                if (c == '\n') *eol = 1;
                else {
                    /* peek for end of line after trailing blanks is not
                     * needed for -n; for -L a trailing blank continues
                     * the line in GNU xargs */
                }
                return 1;
            }
            continue;
        }
        any = 1;
        if (c == '\'' || c == '"') {
            q = c;
        } else if (c == '\\') {
            c = shf_getc(in->f);
            if (c == -1) {
                in->eof = 1;
                return 1;
            }
            sb_addc(w, c);
        } else {
            sb_addc(w, c);
        }
    }
}

static char *x_replace(const char *s, const char *rep, const char *with)
{
    SBuf b;
    size_t rl = strlen(rep);
    const char *p;
    if (!rl) return xstrdup(s);
    sb_init(&b);
    sb_addn(&b, "", 0);
    while ((p = strstr(s, rep)) != NULL) {
        sb_addn(&b, s, (int)(p - s));
        sb_adds(&b, with);
        s = p + rl;
    }
    sb_adds(&b, s);
    return sb_take(&b);
}

static int x_run(StrList *cmd, int trace, int *worst)
{
    int st, i;
    if (trace) {
        SBuf b;
        sb_init(&b);
        for (i = 0; i < cmd->n; i++) {
            if (i) sb_addc(&b, ' ');
            sb_adds(&b, cmd->v[i]);
        }
        sb_addc(&b, '\n');
        sh_write(2, b.s, b.len);
        sb_free(&b);
    }
    sh_flush_all();
    st = exec_argv(cmd->n, sl_argv(cmd));
    if (st == 255) {
        sh_cmd_error("xargs", "%s: exited with status 255; aborting", cmd->v[0]);
        *worst = 124;
        return -1;
    }
    if (st == 126 || st == 127) {
        *worst = st;
        return -1;
    }
    if (st != 0 && *worst == 0) *worst = 123;
    return 0;
}

int util_xargs(int argc, char **argv)
{
    static const char *const lo[] = { "max-args=", "replace?", "null", "delimiter=", "no-run-if-empty",
                                      "verbose", "max-lines?", "eof?", "max-procs=", "exit", NULL };
    UGetopt g;
    int c, i, maxargs = 0, maxlines = 0, trace = 0, noempty = 0, ran = 0, worst = 0, rc;
    const char *repl = NULL, *eofstr = NULL;
    XIn in;
    SBuf w;
    StrList base, cmd;
    long v;
    ug_init(&g, argc, argv, "n:I:i::0d:rtL:l::E:e::P:xs:", lo);
    g.stop_nonopt = 1;
    memset(&in, 0, sizeof(in));
    in.delim = -1;
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'n': case UG_LONG + 0:
            if (!u_atoi(g.arg, &v) || v < 1) {
                sh_cmd_error(argv[0], "invalid number \"%s\" for -n option", g.arg);
                ug_free(&g);
                return 1;
            }
            maxargs = (int)v;
            break;
        case 'I': repl = g.arg; break;
        case 'i': case UG_LONG + 1: repl = g.arg ? g.arg : "{}"; break;
        case '0': case UG_LONG + 2: in.delim = 0; break;
        case 'd': case UG_LONG + 3: {
            const char *d = g.arg;
            if (d[0] == '\\' && d[1]) {
                switch (d[1]) {
                case 'n': in.delim = '\n'; break;
                case 't': in.delim = '\t'; break;
                case '0': in.delim = 0; break;
                case '\\': in.delim = '\\'; break;
                default: in.delim = (unsigned char)d[1]; break;
                }
            } else {
                in.delim = (unsigned char)d[0];
            }
            break;
        }
        case 'r': case UG_LONG + 4: noempty = 1; break;
        case 't': case UG_LONG + 5: trace = 1; break;
        case 'L': case 'l': case UG_LONG + 6:
            maxlines = 1;
            if (g.arg && (!u_atoi(g.arg, &v) || v < 1)) {
                sh_cmd_error(argv[0], "invalid number \"%s\" for -L option", g.arg);
                ug_free(&g);
                return 1;
            }
            if (g.arg) maxlines = (int)v;
            break;
        case 'E': case 'e': case UG_LONG + 7: eofstr = g.arg; break;
        case 'P': case 's': case 'x': case UG_LONG + 8: case UG_LONG + 9: break;
        case UG_LONG + 999: ug_free(&g); return u_help(argv[0]);
        default: ug_free(&g); return 1;
        }
    }
    sl_init(&base);
    sl_init(&cmd);
    sb_init(&w);
    if (g.nops == 0) sl_adddup(&base, "echo");
    for (i = 0; i < g.nops; i++) sl_adddup(&base, g.ops[i]);
    ug_free(&g);
    in.f = sh_fd(0);
    if (!in.f) in.f = shf_null();
    else shf_ref(in.f);

    if (repl) {
        SBuf line;
        int nl;
        sb_init(&line);
        while (shf_getline(in.f, &line, &nl)) {
            const char *s = sb_str(&line);
            if (u_break()) { worst = 130; break; }
            while (*s == ' ' || *s == '\t') s++;
            if (!*s) continue;
            if (eofstr && strcmp(s, eofstr) == 0) break;
            sl_free(&cmd);
            for (i = 0; i < base.n; i++) sl_add(&cmd, x_replace(base.v[i], repl, s));
            ran = 1;
            if (x_run(&cmd, trace, &worst) < 0) break;
        }
        sb_free(&line);
        (void)ran;
    } else {
        int eol, nargs = 0, nlines = 0, stop = 0;
        for (i = 0; i < base.n; i++) sl_adddup(&cmd, base.v[i]);
        while (!stop) {
            rc = x_word(&in, &w, &eol);
            if (u_break()) { worst = 130; break; }
            if (rc < 0) { worst = 1; break; }
            if (rc == 1 && eofstr && strcmp(sb_str(&w), eofstr) == 0) rc = 0;
            if (rc == 1) {
                sl_adddup(&cmd, sb_str(&w));
                nargs++;
                if (eol) nlines++;
            }
            if (rc == 0 || (maxargs && nargs >= maxargs) || (maxlines && nlines >= maxlines)
                || nargs >= 4096) {
                if (nargs > 0 || (rc == 0 && !ran && !noempty)) {
                    ran = 1;
                    if (x_run(&cmd, trace, &worst) < 0) break;
                }
                sl_free(&cmd);
                for (i = 0; i < base.n; i++) sl_adddup(&cmd, base.v[i]);
                nargs = nlines = 0;
                if (rc == 0) stop = 1;
            }
        }
    }
    shf_close(in.f);
    sl_free(&cmd);
    sl_free(&base);
    sb_free(&w);
    return worst;
}

/* ================================================================== */
/* uname / whoami / hostname                                           */
/* ================================================================== */

int util_uname(int argc, char **argv)
{
    static const char *const lo[] = { "all", "kernel-name", "nodename", "kernel-release", "kernel-version",
                                      "machine", "processor", "hardware-platform", "operating-system", NULL };
    UGetopt g;
    int c, flags = 0, first = 1, i;
#ifdef AMIGA
    const char *vals[8] = { "AmigaOS", NULL, "3.x", "AmiBash " AMIBASH_SHORTVER, "m68k", "m68k", "Amiga", "AmigaOS" };
#else
    const char *vals[8] = { "Linux", NULL, "host", "AmiBash " AMIBASH_SHORTVER, "x86_64", "unknown", "unknown", "GNU/Linux" };
#endif
    vals[1] = os_hostname();
    ug_init(&g, argc, argv, "asnrvmpio", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'a': case UG_LONG + 0: flags |= 0x1000; break;
        case 's': case UG_LONG + 1: flags |= 1; break;
        case 'n': case UG_LONG + 2: flags |= 2; break;
        case 'r': case UG_LONG + 3: flags |= 4; break;
        case 'v': case UG_LONG + 4: flags |= 8; break;
        case 'm': case UG_LONG + 5: flags |= 16; break;
        case 'p': case UG_LONG + 6: flags |= 32; break;
        case 'i': case UG_LONG + 7: flags |= 64; break;
        case 'o': case UG_LONG + 8: flags |= 128; break;
        case UG_LONG + 999: ug_free(&g); return u_help(argv[0]);
        default: ug_free(&g); return 1;
        }
    }
    if (g.nops) {
        sh_cmd_error(argv[0], "extra operand '%s'", g.ops[0]);
        ug_free(&g);
        return 1;
    }
    ug_free(&g);
    if (flags & 0x1000) flags = 1 | 2 | 4 | 8 | 16 | 128;
    if (!flags) flags = 1;
    for (i = 0; i < 8; i++) {
        if (flags & (1 << i)) {
            if (!first) sh_putc(1, ' ');
            sh_puts(1, vals[i]);
            first = 0;
        }
    }
    sh_putc(1, '\n');
    return 0;
}

int util_whoami(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_printf(1, "%s\n", os_username());
    return 0;
}

int util_hostname(int argc, char **argv)
{
    (void)argc;
    if (argv[1] && strcmp(argv[1], "--help") == 0) return u_help(argv[0]);
    if (argv[1] && argv[1][0] != '-') {
        sh_cmd_error(argv[0], "setting the host name is not supported");
        return 1;
    }
    sh_printf(1, "%s\n", os_hostname());
    return 0;
}

/* ================================================================== */
/* table                                                               */
/* ================================================================== */

const Builtin util_builtins[] = {
    { "basename", util_basename, BI_UTIL, "basename NAME [SUFFIX] | basename -a [-s SUFFIX] [-z] NAME..." },
    { "cat", util_cat, BI_UTIL, "cat [-AbeEnstTv] [FILE]..." },
    { "chmod", util_chmod, BI_UTIL, "chmod [-Rv] (+-=)[hsparwed] | [ugoa][+-=][rwx] | OCTAL FILE..." },
    { "clear", util_clear, BI_UTIL, "clear" },
    { "cp", util_cp, BI_UTIL, "cp [-afinprRuv] [-t DIR] SOURCE... DEST" },
    { "cut", util_cut, BI_UTIL, "cut -b LIST | -c LIST | -f LIST [-d DELIM] [-s] [--complement] [--output-delimiter=S] [FILE]..." },
    { "date", util_date, BI_UTIL, "date [-uR] [-d STRING] [-r FILE] [-I[FMT]] [+FORMAT]" },
    { "df", util_df, BI_UTIL, "df [-hk] [FILE]..." },
    { "dirname", util_dirname, BI_UTIL, "dirname [-z] NAME..." },
    { "du", util_du, BI_UTIL, "du [-achks] [-d N] [FILE]..." },
    { "egrep", util_grep, BI_UTIL, "egrep [OPTION]... PATTERN [FILE]..." },
    { "env", util_env, BI_UTIL, "env [-i0] [-u NAME] [NAME=VALUE]... [COMMAND [ARG]...]" },
    { "fgrep", util_grep, BI_UTIL, "fgrep [OPTION]... STRINGS [FILE]..." },
    { "find", util_find, BI_UTIL, "find [PATH]... [-name P] [-iname P] [-path P] [-type f|d] [-maxdepth N] [-mindepth N] [-newer F] [-size [+-]N[ckMG]] [-empty] [-print] [-print0] [-exec CMD {} ;] [-delete] [! -a -o ( )]" },
    { "grep", util_grep, BI_UTIL, "grep [-EFGHhiLlnoqRrsvwxcZ] [-e PAT]... [-f FILE] [-A N] [-B N] [-C N] [-m N] [--color[=WHEN]] [--include=GLOB] [PATTERN] [FILE]..." },
    { "head", util_head, BI_UTIL, "head [-qv] [-n [-]N] [-c [-]N] [-N] [FILE]..." },
    { "hostname", util_hostname, BI_UTIL, "hostname" },
    { "ln", util_ln, BI_UTIL, "ln [-sf] TARGET LINK_NAME (not supported)" },
    { "ls", util_ls, BI_UTIL, "ls [-1aAdFhlrRStuC] [--color[=WHEN]] [FILE]..." },
    { "mkdir", util_mkdir, BI_UTIL, "mkdir [-pv] DIRECTORY..." },
    { "mv", util_mv, BI_UTIL, "mv [-finuv] [-t DIR] SOURCE... DEST" },
    { "nl", util_nl, BI_UTIL, "nl [-b a|t|n] [-n ln|rn|rz] [-w N] [-s SEP] [-v N] [-i N] [FILE]..." },
    { "paste", util_paste, BI_UTIL, "paste [-s] [-d LIST] [FILE]..." },
    { "printenv", util_printenv, BI_UTIL, "printenv [-0] [NAME]..." },
    { "realpath", util_realpath, BI_UTIL, "realpath [-ems] FILE..." },
    { "rev", util_rev, BI_UTIL, "rev [FILE]..." },
    { "rm", util_rm, BI_UTIL, "rm [-dfirRv] FILE..." },
    { "rmdir", util_rmdir, BI_UTIL, "rmdir [-pv] DIRECTORY..." },
    { "sed", util_sed, BI_UTIL, "sed [-nEr] [-i[SUFFIX]] [-s] [-z] {SCRIPT | -e SCRIPT... | -f FILE...} [FILE]..." },
    { "seq", util_seq, BI_UTIL, "seq [-w] [-s SEP] [-f FMT] [FIRST [INCREMENT]] LAST" },
    { "sleep", util_sleep, BI_UTIL, "sleep NUMBER[smhd]..." },
    { "sort", util_sort, BI_UTIL, "sort [-bcdfghMnruRsVz] [-k POS1[,POS2]]... [-t SEP] [-o FILE] [FILE]..." },
    { "stat", util_stat, BI_UTIL, "stat [-t] [-c FORMAT] FILE..." },
    { "tac", util_tac, BI_UTIL, "tac [FILE]..." },
    { "tail", util_tail, BI_UTIL, "tail [-qv] [-n [+-]N] [-c [+-]N] [-N] [FILE]..." },
    { "tee", util_tee, BI_UTIL, "tee [-ai] [FILE]..." },
    { "touch", util_touch, BI_UTIL, "touch [-acm] [-d DATE] [-t STAMP] [-r FILE] FILE..." },
    { "tr", util_tr, BI_UTIL, "tr [-cdst] SET1 [SET2]" },
    { "uname", util_uname, BI_UTIL, "uname [-asnrvmpio]" },
    { "uniq", util_uniq, BI_UTIL, "uniq [-cdDiuz] [-f N] [-s N] [-w N] [INPUT [OUTPUT]]" },
    { "wc", util_wc, BI_UTIL, "wc [-clmwL] [FILE]..." },
    { "whoami", util_whoami, BI_UTIL, "whoami" },
    { "xargs", util_xargs, BI_UTIL, "xargs [-0prt] [-n N] [-L N] [-I REPL] [-d DELIM] [-E EOF] [COMMAND [ARG]...]" },
    { NULL, NULL, 0, NULL }
};
