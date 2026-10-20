/*
 * utils_text.c - text utilities: cat head tail wc grep sort uniq cut tr
 * sed tee seq rev nl paste.
 *
 * Input is streamed line by line where possible (memory is scarce on
 * the Amiga); sort, tail and uniq hold what they need in memory.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "sh.h"
#include "utils.h"

#define IOBUF 4096

static int is_blank(int c)
{
    return c == ' ' || c == '\t';
}

static const char *disp_name(const char *n)
{
    return strcmp(n, "-") == 0 ? "standard input" : n;
}

/* parse a non-negative count with optional suffix (b k K m M g G) */
static int parse_count(const char *s, double *out)
{
    double v = 0;
    int any = 0;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s++ - '0');
        any = 1;
    }
    if (!any) return 0;
    if (*s) {
        double m = 1;
        switch (*s) {
        case 'b': m = 512; break;
        case 'k': case 'K': m = 1024; break;
        case 'm': case 'M': m = 1048576.0; break;
        case 'g': case 'G': m = 1073741824.0; break;
        default: return 0;
        }
        s++;
        if (*s == 'B' || (*s == 'i' && s[1] == 'B')) s += *s == 'B' ? 1 : 2;
        if (*s) return 0;
        v *= m;
    }
    *out = v;
    return 1;
}

static long to_long(double d)
{
    if (d > 2000000000.0) return 2000000000L;
    return (long)d;
}

/* ================================================================== */
/* cat                                                                 */
/* ================================================================== */

typedef struct CatOpt { int number, nonblank, squeeze, showends, showtabs, shownp; long lineno; int lastblank; int atbol; } CatOpt;

static void cat_vis(SBuf *b, int c, CatOpt *o)
{
    if (c == '\t') {
        if (o->showtabs) sb_adds(b, "^I");
        else sb_addc(b, c);
        return;
    }
    if (!o->shownp) {
        sb_addc(b, c);
        return;
    }
    if (c >= 128) {
        sb_adds(b, "M-");
        c -= 128;
    }
    if (c < 32) {
        sb_addc(b, '^');
        sb_addc(b, c + 64);
    } else if (c == 127) {
        sb_adds(b, "^?");
    } else {
        sb_addc(b, c);
    }
}

static int cat_file(ShFile *f, CatOpt *o)
{
    SBuf line, out;
    int nl, i;
    if (!o->number && !o->nonblank && !o->squeeze && !o->showends && !o->showtabs && !o->shownp) {
        char *buf = xmalloc(IOBUF);
        long n;
        while ((n = shf_read(f, buf, IOBUF)) > 0) {
            sh_write(1, buf, n);
            if (u_break()) break;
        }
        xfree(buf);
        return 0;
    }
    sb_init(&line);
    sb_init(&out);
    while (shf_getline(f, &line, &nl)) {
        int blank = line.len == 0 && nl;
        if (u_break()) break;
        if (o->squeeze && blank && o->lastblank) continue;
        o->lastblank = blank;
        sb_clear(&out);
        if ((o->number && !o->nonblank) || (o->nonblank && line.len > 0)) {
            char *n = u_ultoa((unsigned long)++o->lineno);
            int l = (int)strlen(n);
            for (i = l; i < 6; i++) sb_addc(&out, ' ');
            sb_adds(&out, n);
            sb_addc(&out, '\t');
        }
        for (i = 0; i < line.len; i++) cat_vis(&out, (unsigned char)line.s[i], o);
        if (nl) {
            if (o->showends) sb_addc(&out, '$');
            sb_addc(&out, '\n');
        }
        sh_write(1, out.s, out.len);
    }
    sb_free(&line);
    sb_free(&out);
    return 0;
}

int util_cat(int argc, char **argv)
{
    static const char *const lo[] = { "number", "number-nonblank", "squeeze-blank", "show-ends", "show-tabs",
                                      "show-nonprinting", "show-all", NULL };
    UGetopt g;
    CatOpt o;
    int c, i, rc = 0;
    memset(&o, 0, sizeof(o));
    u_break_reset();
    ug_init(&g, argc, argv, "nbsEeTtvAu", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'n': case UG_LONG + 0: o.number = 1; break;
        case 'b': case UG_LONG + 1: o.nonblank = 1; break;
        case 's': case UG_LONG + 2: o.squeeze = 1; break;
        case 'E': case UG_LONG + 3: o.showends = 1; break;
        case 'T': case UG_LONG + 4: o.showtabs = 1; break;
        case 'v': case UG_LONG + 5: o.shownp = 1; break;
        case 'e': o.shownp = o.showends = 1; break;
        case 't': o.shownp = o.showtabs = 1; break;
        case 'A': case UG_LONG + 6: o.shownp = o.showends = o.showtabs = 1; break;
        case 'u': break;
        case UG_LONG + 999: ug_free(&g); sh_puts(1, "Usage: cat [-AbeEnstTv] [FILE]...\n"); return 0;
        default: ug_free(&g); u_usage(argv[0]); return 1;
        }
    }
    if (g.nops == 0) g.ops[g.nops++] = "-";
    for (i = 0; i < g.nops && !u_break(); i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        cat_file(f, &o);
        shf_close(f);
    }
    ug_free(&g);
    return u_break() ? 130 : rc;
}

/* ================================================================== */
/* head / tail                                                         */
/* ================================================================== */

typedef struct HTOpt { int bytes; int neg; int plus; long n; int hdr; } HTOpt;

static void head_file(ShFile *f, HTOpt *o)
{
    if (o->bytes && !o->neg) {
        char *buf = xmalloc(IOBUF);
        long left = o->n, r;
        while (left > 0 && (r = shf_read(f, buf, left < IOBUF ? left : IOBUF)) > 0) {
            sh_write(1, buf, r);
            left -= r;
        }
        xfree(buf);
    } else if (o->bytes) {
        /* all but the last n bytes: ring buffer */
        long cap = o->n + 1, fill = 0, start = 0;
        char *ring = xmalloc(cap);
        int c;
        while ((c = shf_getc(f)) != -1) {
            if (fill == cap - 1) {
                sh_putc(1, ring[start]);
                ring[start] = (char)c;
                start = (start + 1) % cap;
            } else {
                ring[(start + fill) % cap] = (char)c;
                fill++;
            }
            if (o->n == 0) {
                sh_putc(1, c);
                fill = 0;
            }
        }
        xfree(ring);
    } else if (!o->neg) {
        SBuf line;
        int nl;
        long k = 0;
        sb_init(&line);
        while (k < o->n && shf_getline(f, &line, &nl)) {
            sh_write(1, line.s, line.len);
            if (nl) sh_putc(1, '\n');
            k++;
            if (u_break()) break;
        }
        sb_free(&line);
    } else {
        /* all but the last n lines */
        long cap = o->n, fill = 0, start = 0;
        SBuf *ring = xcalloc((cap + 1) * sizeof(SBuf));
        SBuf line;
        int nl;
        sb_init(&line);
        while (shf_getline(f, &line, &nl)) {
            if (nl) sb_addc(&line, '\n');
            if (cap == 0) {
                sh_write(1, line.s, line.len);
                continue;
            }
            if (fill == cap) {
                SBuf t = ring[start];
                sh_write(1, t.s, t.len);
                ring[start] = line;
                line = t;
                start = (start + 1) % cap;
            } else {
                SBuf t = ring[(start + fill) % cap];
                ring[(start + fill) % cap] = line;
                line = t;
                fill++;
            }
            if (u_break()) break;
        }
        sb_free(&line);
        for (fill = 0; fill < cap; fill++) sb_free(&ring[fill]);
        xfree(ring);
    }
}

static void tail_file(ShFile *f, HTOpt *o)
{
    if (o->plus) {
        /* from line/byte n on */
        long k = 1;
        int c;
        if (o->bytes) {
            char *buf;
            long r;
            while (k < o->n && shf_getc(f) != -1) k++;
            buf = xmalloc(IOBUF);
            while ((r = shf_read(f, buf, IOBUF)) > 0) sh_write(1, buf, r);
            xfree(buf);
        } else {
            char *buf;
            long r;
            while (k < o->n && (c = shf_getc(f)) != -1)
                if (c == '\n') k++;
            buf = xmalloc(IOBUF);
            while ((r = shf_read(f, buf, IOBUF)) > 0) sh_write(1, buf, r);
            xfree(buf);
        }
    } else if (o->bytes) {
        long cap = o->n, fill = 0, start = 0, i;
        char *ring = xmalloc(cap + 1);
        int c;
        while ((c = shf_getc(f)) != -1) {
            if (cap == 0) continue;
            if (fill == cap) {
                ring[start] = (char)c;
                start = (start + 1) % cap;
            } else {
                ring[(start + fill) % cap] = (char)c;
                fill++;
            }
        }
        for (i = 0; i < fill; i++) sh_putc(1, ring[(start + i) % cap]);
        xfree(ring);
    } else {
        long cap = o->n, fill = 0, start = 0, i;
        SBuf *ring = xcalloc((cap + 1) * sizeof(SBuf));
        SBuf line;
        int nl;
        sb_init(&line);
        while (shf_getline(f, &line, &nl)) {
            SBuf t;
            if (nl) sb_addc(&line, '\n');
            if (cap == 0) continue;
            if (fill == cap) {
                t = ring[start];
                ring[start] = line;
                start = (start + 1) % cap;
            } else {
                t = ring[(start + fill) % cap];
                ring[(start + fill) % cap] = line;
                fill++;
            }
            line = t;
            if (u_break()) break;
        }
        for (i = 0; i < fill; i++) {
            SBuf *b = &ring[(start + i) % cap];
            sh_write(1, b->s, b->len);
        }
        sb_free(&line);
        for (i = 0; i < cap; i++) sb_free(&ring[i]);
        xfree(ring);
    }
}

static int head_tail(int argc, char **argv, int tail)
{
    static const char *const lo[] = { "lines=", "bytes=", "quiet", "silent", "verbose", "follow?", NULL };
    UGetopt g;
    HTOpt o;
    int c, i, rc = 0, quiet = 0, verbose = 0, first = 1;
    memset(&o, 0, sizeof(o));
    o.n = 10;
    u_break_reset();
    /* obsolete "tail +N" */
    ug_init(&g, argc, argv, tail ? "n:c:qvfFs:" : "n:c:qv", lo);
    g.allow_num = 1;
    while ((c = ug_next(&g)) != -1) {
        const char *a;
        double d;
        switch (c) {
        case 'n': case 'c': case UG_LONG + 0: case UG_LONG + 1:
            a = g.arg;
            o.bytes = c == 'c' || c == UG_LONG + 1;
            o.neg = o.plus = 0;
            if (*a == '-') {
                if (!tail) o.neg = 1;
                a++;
            } else if (*a == '+') {
                if (tail) o.plus = 1;
                a++;
            }
            if (!parse_count(a, &d)) {
                sh_cmd_error(argv[0], "invalid number of %s: '%s'", o.bytes ? "bytes" : "lines", g.arg);
                ug_free(&g);
                return 1;
            }
            o.n = to_long(d);
            break;
        case UG_NUM:
            o.n = to_long((double)atol(g.arg));
            o.bytes = o.neg = o.plus = 0;
            break;
        case 'q': case UG_LONG + 2: case UG_LONG + 3: quiet = 1; verbose = 0; break;
        case 'v': case UG_LONG + 4: verbose = 1; quiet = 0; break;
        case 'f': case 'F': case 's': case UG_LONG + 5: break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); u_usage(argv[0]); return 1;
        }
    }
    if (tail && g.nops > 0 && g.ops[0][0] == '+' && isdigit((unsigned char)g.ops[0][1]) && g.nops <= 2 &&
        argc > 1 && argv[1] == g.ops[0]) {
        long v;
        if (u_atoi(g.ops[0] + 1, &v)) {
            o.plus = 1;
            o.n = v;
            for (i = 1; i < g.nops; i++) g.ops[i - 1] = g.ops[i];
            g.nops--;
        }
    }
    if (g.nops == 0) g.ops[g.nops++] = "-";
    o.hdr = verbose || (!quiet && g.nops > 1);
    for (i = 0; i < g.nops && !u_break(); i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        if (o.hdr) {
            sh_printf(1, "%s==> %s <==\n", first ? "" : "\n", disp_name(g.ops[i]));
        }
        first = 0;
        if (tail) tail_file(f, &o);
        else head_file(f, &o);
        shf_close(f);
    }
    ug_free(&g);
    return u_break() ? 130 : rc;
}

int util_head(int argc, char **argv)
{
    return head_tail(argc, argv, 0);
}

int util_tail(int argc, char **argv)
{
    return head_tail(argc, argv, 1);
}

/* ================================================================== */
/* wc                                                                  */
/* ================================================================== */

typedef struct WcCnt { double l, w, c, m, L; } WcCnt;

static void wc_count(ShFile *f, WcCnt *k)
{
    char *buf = xmalloc(IOBUF);
    long n, i;
    int inword = 0;
    long col = 0;
    memset(k, 0, sizeof(*k));
    while ((n = shf_read(f, buf, IOBUF)) > 0) {
        for (i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') {
                k->l++;
                if (col > k->L) k->L = (double)col;
                col = 0;
            } else if (ch == '\t') {
                col = (col + 8) & ~7L;
            } else if (ch >= 32 && ch != 127) {
                col++;
            }
            if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v') {
                inword = 0;
            } else if (!inword) {
                inword = 1;
                k->w++;
            }
            if ((ch & 0xc0) != 0x80) k->m++;
        }
        k->c += n;
        if (u_break()) break;
    }
    if (col > k->L) k->L = (double)col;
    xfree(buf);
}

static void wc_print(WcCnt *k, int *show, int width, const char *name)
{
    SBuf b;
    double v[5];
    int i, first = 1;
    v[0] = k->l;
    v[1] = k->w;
    v[2] = k->m;
    v[3] = k->c;
    v[4] = k->L;
    sb_init(&b);
    for (i = 0; i < 5; i++) {
        char *s;
        if (!show[i]) continue;
        s = u_size_str(v[i], 0);
        sb_printf(&b, first ? "%*s" : " %*s", width, s);
        xfree(s);
        first = 0;
    }
    if (name) sb_printf(&b, " %s", name);
    sb_addc(&b, '\n');
    sh_write(1, b.s, b.len);
    sb_free(&b);
}

int util_wc(int argc, char **argv)
{
    static const char *const lo[] = { "lines", "words", "chars", "bytes", "max-line-length", "total=", NULL };
    UGetopt g;
    int c, i, rc = 0, show[5] = { 0, 0, 0, 0, 0 }, any = 0, width = 1, nshow = 0;
    WcCnt *cnt, tot;
    int *ok;
    double totsize = 0;
    int stdin_used = 0;
    u_break_reset();
    ug_init(&g, argc, argv, "lwcmL", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'l': case UG_LONG + 0: show[0] = 1; any = 1; break;
        case 'w': case UG_LONG + 1: show[1] = 1; any = 1; break;
        case 'm': case UG_LONG + 2: show[2] = 1; any = 1; break;
        case 'c': case UG_LONG + 3: show[3] = 1; any = 1; break;
        case 'L': case UG_LONG + 4: show[4] = 1; any = 1; break;
        case UG_LONG + 5: break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); u_usage(argv[0]); return 1;
        }
    }
    if (!any) show[0] = show[1] = show[3] = 1;
    for (i = 0; i < 5; i++) nshow += show[i];
    if (g.nops == 0) {
        WcCnt k;
        ShFile *f = u_open_in(argv[0], "-");
        wc_count(f, &k);
        shf_close(f);
        wc_print(&k, show, nshow == 1 ? 1 : 7, NULL);
        ug_free(&g);
        return 0;
    }
    /* GNU computes the column width from the file sizes before reading */
    for (i = 0; i < g.nops; i++) {
        OsDirEnt e;
        if (strcmp(g.ops[i], "-") == 0) stdin_used = 1;
        else if (sh_stat(g.ops[i], &e) == 0 && e.type == OS_T_FILE) totsize += e.size;
    }
    {
        char *s = u_size_str(totsize, 0);
        width = (int)strlen(s);
        xfree(s);
    }
    if (stdin_used && width < 7) width = 7;
    if (nshow == 1 && g.nops == 1) width = 1;
    cnt = xcalloc(g.nops * sizeof(WcCnt));
    ok = xcalloc(g.nops * sizeof(int));
    memset(&tot, 0, sizeof(tot));
    for (i = 0; i < g.nops && !u_break(); i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        wc_count(f, &cnt[i]);
        shf_close(f);
        ok[i] = 1;
        tot.l += cnt[i].l;
        tot.w += cnt[i].w;
        tot.c += cnt[i].c;
        tot.m += cnt[i].m;
        if (cnt[i].L > tot.L) tot.L = cnt[i].L;
    }
    /* widen if real counts are wider (e.g. stdin, grown files) */
    {
        char *s = u_size_str(tot.c > tot.l ? (tot.c > tot.w ? tot.c : tot.w) : tot.l, 0);
        if ((int)strlen(s) > width && !(nshow == 1 && g.nops == 1)) width = (int)strlen(s);
        xfree(s);
    }
    for (i = 0; i < g.nops; i++)
        if (ok[i]) wc_print(&cnt[i], show, width, g.ops[i]);
    if (g.nops > 1) wc_print(&tot, show, width, "total");
    xfree(cnt);
    xfree(ok);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* rev / tee                                                           */
/* ================================================================== */

int util_rev(int argc, char **argv)
{
    int i, rc = 0, nl;
    SBuf line;
    UGetopt g;
    int c;
    ug_init(&g, argc, argv, "", NULL);
    while ((c = ug_next(&g)) != -1) {
        ug_free(&g);
        if (c == UG_LONG + 999) {
            sh_puts(1, "Usage: rev [FILE]...\n");
            return 0;
        }
        return 1;
    }
    u_break_reset();
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    for (i = 0; i < g.nops; i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        while (shf_getline(f, &line, &nl) && !u_break()) {
            int a = 0, b = line.len - 1;
            while (a < b) {
                char t = line.s[a];
                line.s[a++] = line.s[b];
                line.s[b--] = t;
            }
            if (nl) sb_addc(&line, '\n');
            sh_write(1, line.s, line.len);
        }
        shf_close(f);
    }
    sb_free(&line);
    ug_free(&g);
    return rc;
}

int util_tac(int argc, char **argv)
{
    UGetopt g;
    int c, i, rc = 0, nl, n = 0, cap = 0;
    char **v = NULL;
    SBuf line;
    ug_init(&g, argc, argv, "", NULL);
    while ((c = ug_next(&g)) != -1) {
        ug_free(&g);
        if (c == UG_LONG + 999) {
            sh_puts(1, "Usage: tac [FILE]...\n");
            return 0;
        }
        return 1;
    }
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    u_break_reset();
    for (i = 0; i < g.nops; i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        n = 0;
        while (shf_getline(f, &line, &nl) && !u_break()) {
            if (nl) sb_addc(&line, '\n');
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                v = xrealloc(v, cap * sizeof(char *));
            }
            v[n++] = xstrndup(sb_str(&line), line.len);
        }
        shf_close(f);
        while (n > 0) {
            n--;
            sh_puts(1, v[n]);
            xfree(v[n]);
        }
    }
    xfree(v);
    sb_free(&line);
    ug_free(&g);
    return rc;
}

int util_tee(int argc, char **argv)
{
    static const char *const lo[] = { "append", "ignore-interrupts", "output-error?", NULL };
    UGetopt g;
    int c, i, rc = 0, app = 0, nf = 0;
    ShFile **fs;
    char *buf;
    long n;
    ShFile *in;
    ug_init(&g, argc, argv, "aip", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'a': case UG_LONG + 0: app = 1; break;
        case 'i': case 'p': case UG_LONG + 1: case UG_LONG + 2: break;
        case UG_LONG + 999: ug_free(&g); sh_puts(1, "Usage: tee [-ai] [FILE]...\n"); return 0;
        default: ug_free(&g); return 1;
        }
    }
    u_break_reset();
    fs = xcalloc((g.nops + 1) * sizeof(ShFile *));
    for (i = 0; i < g.nops; i++) {
        ShFile *f = strcmp(g.ops[i], "-") == 0 ? shf_ref(sh_fd(1)) : shf_open(g.ops[i], app ? OS_APP : OS_WR);
        if (!f) {
            sh_cmd_error(argv[0], "%s: %s", g.ops[i], os_errstr());
            rc = 1;
            continue;
        }
        fs[nf++] = f;
    }
    in = sh_fd(0);
    buf = xmalloc(IOBUF);
    if (in) {
        while ((n = shf_read(in, buf, IOBUF)) > 0) {
            sh_write(1, buf, n);
            for (i = 0; i < nf; i++) shf_write(fs[i], buf, n);
            if (in->kind == SHF_OS && in->tty) sh_flush_all();
            if (u_break()) break;
        }
    }
    xfree(buf);
    for (i = 0; i < nf; i++) shf_close(fs[i]);
    xfree(fs);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* seq                                                                 */
/* ================================================================== */

/* parse a decimal number (optionally with exponent) into mantissa and
 * number of decimals; returns decimals or -1 */
static int seq_parse(const char *s, ShInt *mant, int *neg)
{
    int dec = 0, any = 0;
    *mant = 0;
    *neg = 0;
    if (*s == '-') *neg = 1, s++;
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) {
        *mant = *mant * 10 + (*s++ - '0');
        any = 1;
    }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            if (dec < 9) {
                *mant = *mant * 10 + (*s - '0');
                dec++;
            }
            s++;
            any = 1;
        }
    }
    if (any && (*s == 'e' || *s == 'E')) {
        int eneg = 0, ex = 0;
        s++;
        if (*s == '-' || *s == '+') eneg = *s++ == '-';
        if (!isdigit((unsigned char)*s)) return -1;
        while (isdigit((unsigned char)*s)) ex = ex * 10 + (*s++ - '0');
        if (ex > 15) return -1;
        if (eneg) dec += ex;
        else {
            for (; ex > 0 && dec > 0; ex--) dec--;
            for (; ex > 0; ex--) *mant *= 10;
        }
        if (dec > 15) return -1;
    }
    if (!any || *s) return -1;
    return dec;
}

static ShInt seq_scale(ShInt mant, int dec, int neg, int to)
{
    int i;
    for (i = dec; i < to; i++) mant *= 10;
    return neg ? -mant : mant;
}

static void seq_fmt(SBuf *b, ShInt v, int dec, int width)
{
    char tmp[48];
    int i = sizeof(tmp) - 1, neg = v < 0, k;
    unsigned long long u = neg ? (unsigned long long)0 - (unsigned long long)v : (unsigned long long)v;
    tmp[i] = 0;
    for (k = 0; k < dec; k++) {
        tmp[--i] = (char)('0' + (int)(u % 10));
        u /= 10;
    }
    if (dec) tmp[--i] = '.';
    do {
        tmp[--i] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    k = (int)(sizeof(tmp) - 1 - i) + neg;
    if (neg) sb_addc(b, '-');
    for (; k < width; k++) sb_addc(b, '0');
    sb_adds(b, tmp + i);
}

/* apply a printf style format with one numeric conversion (%f %g %e) */
static void seq_userfmt(SBuf *b, const char *f, ShInt v, int dec)
{
    for (; *f; f++) {
        if (*f == '%' && f[1] == '%') {
            sb_addc(b, '%');
            f++;
        } else if (*f == '%') {
            int left = 0, zero = 0, width = 0, prec = -1, plus = 0;
            SBuf num;
            f++;
            while (*f == '-' || *f == '0' || *f == '+' || *f == ' ' || *f == '#') {
                if (*f == '-') left = 1;
                if (*f == '0') zero = 1;
                if (*f == '+') plus = 1;
                f++;
            }
            while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
            if (*f == '.') {
                prec = 0;
                f++;
                while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0');
            }
            sb_init(&num);
            if (*f == 'f' || *f == 'F' || *f == 'e' || *f == 'g' || *f == 'G' || *f == 'E') {
                int p = (*f == 'f' || *f == 'F') ? (prec < 0 ? 6 : prec) : (prec < 0 ? dec : (prec < dec ? prec : dec));
                ShInt w = v;
                int d = dec;
                /* rescale to p decimals (round half away from zero) */
                while (d < p) {
                    w *= 10;
                    d++;
                }
                while (d > p) {
                    ShInt r = w % 10;
                    w /= 10;
                    if (r >= 5) w++;
                    else if (r <= -5) w--;
                    d--;
                }
                if (plus && w >= 0) sb_addc(&num, '+');
                seq_fmt(&num, w, p, 0);
            } else {
                sb_addc(&num, '%');
                if (*f) sb_addc(&num, *f);
            }
            if (!left) {
                int k;
                for (k = num.len; k < width; k++) sb_addc(b, zero ? '0' : ' ');
                sb_addn(b, num.s, num.len);
            } else {
                int k;
                sb_addn(b, num.s, num.len);
                for (k = num.len; k < width; k++) sb_addc(b, ' ');
            }
            sb_free(&num);
            if (!*f) break;
        } else {
            sb_addc(b, *f);
        }
    }
}

int util_seq(int argc, char **argv)
{
    const char *sep = "\n", *ufmt = NULL;
    int eqw = 0, i, nops = 0, dec = 0, decs[3], negs[3], width = 0;
    ShInt mants[3], first, inc, last, v;
    char *ops[3];
    SBuf b;
    long count = 0;
    /* options can't be permuted: negative numbers are operands */
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (nops == 0 && a[0] == '-' && a[1] && !isdigit((unsigned char)a[1]) && a[1] != '.') {
            if (strcmp(a, "--") == 0) {
                i++;
                break;
            }
            if (strcmp(a, "-w") == 0 || strcmp(a, "--equal-width") == 0) eqw = 1;
            else if (strcmp(a, "-s") == 0 || strcmp(a, "-f") == 0) {
                if (i + 1 >= argc) {
                    sh_cmd_error(argv[0], "option requires an argument -- '%c'", a[1]);
                    return 1;
                }
                if (a[1] == 's') sep = argv[++i];
                else ufmt = argv[++i];
            } else if (a[1] == 's') sep = a + 2;
            else if (a[1] == 'f') ufmt = a + 2;
            else if (strncmp(a, "--separator=", 12) == 0) sep = a + 12;
            else if (strncmp(a, "--format=", 9) == 0) ufmt = a + 9;
            else if (strcmp(a, "--help") == 0) {
                sh_puts(1, "Usage: seq [-w] [-s SEP] [-f FMT] [FIRST [INCREMENT]] LAST\n");
                return 0;
            } else {
                sh_cmd_error(argv[0], "invalid option -- '%s'", a + 1);
                return 1;
            }
            continue;
        }
        break;
    }
    for (; i < argc; i++) {
        if (nops >= 3) {
            sh_cmd_error(argv[0], "extra operand '%s'", argv[i]);
            return 1;
        }
        ops[nops++] = argv[i];
    }
    if (nops == 0) {
        sh_cmd_error(argv[0], "missing operand");
        return 1;
    }
    for (i = 0; i < nops; i++) {
        decs[i] = seq_parse(ops[i], &mants[i], &negs[i]);
        if (decs[i] < 0) {
            sh_cmd_error(argv[0], "invalid floating point argument: '%s'", ops[i]);
            return 1;
        }
        if (decs[i] > dec && !(nops == 3 && i == 2 && 0)) dec = decs[i];
    }
    /* GNU uses the precision of FIRST and INCREMENT (LAST does not widen) */
    if (nops == 3 || nops == 2) {
        dec = decs[0];
        if (nops == 3 && decs[1] > dec) dec = decs[1];
        if (decs[nops - 1] > dec) {
            /* last more precise: still need exact comparison */
        }
    }
    {
        int cmpdec = dec;
        for (i = 0; i < nops; i++)
            if (decs[i] > cmpdec) cmpdec = decs[i];
        first = nops >= 2 ? seq_scale(mants[0], decs[0], negs[0], cmpdec) : seq_scale(1, 0, 0, cmpdec);
        inc = nops == 3 ? seq_scale(mants[1], decs[1], negs[1], cmpdec) : seq_scale(1, 0, 0, cmpdec);
        last = seq_scale(mants[nops - 1], decs[nops - 1], negs[nops - 1], cmpdec);
        if (inc == 0) {
            sh_cmd_error(argv[0], "invalid Zero increment value: '%s'", ops[1]);
            return 1;
        }
        sb_init(&b);
        if (eqw) {
            SBuf t;
            ShInt lv = last;
            int k;
            /* width of the widest of first and last at output precision */
            sb_init(&t);
            for (k = cmpdec; k > dec; k--) lv /= 10;
            seq_fmt(&t, lv, dec, 0);
            width = t.len;
            sb_clear(&t);
            lv = first;
            for (k = cmpdec; k > dec; k--) lv /= 10;
            seq_fmt(&t, lv, dec, 0);
            if (t.len > width) width = t.len;
            sb_free(&t);
        }
        u_break_reset();
        for (v = first; inc > 0 ? v <= last : v >= last; v += inc) {
            ShInt ov = v;
            int k;
            for (k = cmpdec; k > dec; k--) ov /= 10;
            sb_clear(&b);
            if (count++) sb_adds(&b, sep);
            if (ufmt) seq_userfmt(&b, ufmt, ov, dec);
            else seq_fmt(&b, ov, dec, width);
            sh_write(1, b.s, b.len);
            if ((count & 255) == 0 && u_break()) break;
        }
        if (count) sh_putc(1, '\n');
        sb_free(&b);
    }
    return 0;
}

/* ================================================================== */
/* nl                                                                  */
/* ================================================================== */

int util_nl(int argc, char **argv)
{
    static const char *const lo[] = { "body-numbering=", "number-format=", "number-width=", "number-separator=",
                                      "starting-line-number=", "line-increment=", NULL };
    UGetopt g;
    int c, i, rc = 0, nl, width = 6;
    char style = 't', fmt = 'r';
    const char *sep = "\t";
    long start = 1, incr = 1, num;
    Regex *re = NULL;
    SBuf line, out;
    ug_init(&g, argc, argv, "b:n:w:s:v:i:h:f:d:pl:", lo);
    while ((c = ug_next(&g)) != -1) {
        long v;
        switch (c) {
        case 'b': case UG_LONG + 0:
            style = g.arg[0];
            if (style == 'p') {
                char *err = NULL;
                if (re) re_free(re);
                re = re_compile(g.arg + 1, RE_BASIC, &err);
                if (!re) {
                    sh_cmd_error(argv[0], "invalid regex: %s", err ? err : g.arg);
                    xfree(err);
                    ug_free(&g);
                    return 1;
                }
            } else if (!strchr("atn", style) || g.arg[1]) {
                sh_cmd_error(argv[0], "invalid body numbering style: '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            break;
        case 'n': case UG_LONG + 1:
            if (strcmp(g.arg, "ln") == 0) fmt = 'l';
            else if (strcmp(g.arg, "rn") == 0) fmt = 'r';
            else if (strcmp(g.arg, "rz") == 0) fmt = 'z';
            else {
                sh_cmd_error(argv[0], "invalid line numbering format: '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            break;
        case 'w': case UG_LONG + 2:
            if (!u_atoi(g.arg, &v) || v < 1) {
                sh_cmd_error(argv[0], "invalid line number field width: '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            width = (int)v;
            break;
        case 's': case UG_LONG + 3: sep = g.arg; break;
        case 'v': case UG_LONG + 4: if (u_atoi(g.arg, &v)) start = v; break;
        case 'i': case UG_LONG + 5: if (u_atoi(g.arg, &v)) incr = v; break;
        case 'h': case 'f': case 'd': case 'p': case 'l': break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); return 1;
        }
    }
    u_break_reset();
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    sb_init(&out);
    num = start;
    for (i = 0; i < g.nops; i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        while (shf_getline(f, &line, &nl) && !u_break()) {
            int number;
            sb_clear(&out);
            if (style == 'a') number = 1;
            else if (style == 't') number = line.len > 0;
            else if (style == 'p') {
                ReMatch m[1];
                number = re_exec(re, sb_str(&line), 1, m, 0);
            } else number = 0;
            if (number) {
                char tmp[24];
                const char *ds;
                int neg = num < 0, l, k;
                ds = u_ultoa((unsigned long)(neg ? -num : num));
                strcpy(tmp + 1, ds);
                tmp[0] = '-';
                ds = neg ? tmp : tmp + 1;
                l = (int)strlen(ds);
                if (fmt == 'l') {
                    sb_adds(&out, ds);
                    for (k = l; k < width; k++) sb_addc(&out, ' ');
                } else if (fmt == 'z') {
                    if (neg) sb_addc(&out, '-'), ds++;
                    for (k = l; k < width; k++) sb_addc(&out, '0');
                    sb_adds(&out, ds);
                } else {
                    for (k = l; k < width; k++) sb_addc(&out, ' ');
                    sb_adds(&out, ds);
                }
                sb_adds(&out, sep);
                num += incr;
            } else {
                int k, sl = (int)strlen(sep);
                for (k = 0; k < width + sl; k++) sb_addc(&out, ' ');
            }
            sb_addn(&out, line.s, line.len);
            sb_addc(&out, '\n');
            sh_write(1, out.s, out.len);
        }
        shf_close(f);
    }
    if (re) re_free(re);
    sb_free(&line);
    sb_free(&out);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* paste                                                               */
/* ================================================================== */

static char *unescape_list(const char *s, int *len)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': sb_addc(&b, '\n'); break;
            case 't': sb_addc(&b, '\t'); break;
            case '0': sb_addc(&b, 0); break;
            default: sb_addc(&b, *s); break;
            }
        } else {
            sb_addc(&b, *s);
        }
    }
    *len = b.len;
    return sb_take(&b);
}

int util_paste(int argc, char **argv)
{
    static const char *const lo[] = { "delimiters=", "serial", NULL };
    UGetopt g;
    int c, i, rc = 0, serial = 0, nd, nl;
    char *delims = NULL;
    const char *dspec = "\t";
    SBuf line, out;
    ug_init(&g, argc, argv, "d:s", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'd': case UG_LONG + 0: dspec = g.arg; break;
        case 's': case UG_LONG + 1: serial = 1; break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); return 1;
        }
    }
    delims = unescape_list(dspec, &nd);
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    sb_init(&out);
    u_break_reset();
    if (serial) {
        for (i = 0; i < g.nops; i++) {
            ShFile *f = u_open_in(argv[0], g.ops[i]);
            int k = 0, any = 0;
            if (!f) {
                rc = 1;
                continue;
            }
            sb_clear(&out);
            while (shf_getline(f, &line, &nl)) {
                if (any && nd) {
                    if (delims[k % nd]) sb_addc(&out, delims[k % nd]);
                    k++;
                }
                sb_addn(&out, line.s, line.len);
                any = 1;
            }
            sb_addc(&out, '\n');
            sh_write(1, out.s, out.len);
            shf_close(f);
        }
    } else {
        ShFile **fs = xcalloc(g.nops * sizeof(ShFile *));
        int live = 0;
        for (i = 0; i < g.nops; i++) {
            fs[i] = u_open_in(argv[0], g.ops[i]);
            if (!fs[i]) rc = 1;
            else live++;
        }
        while (live > 0 && !u_break()) {
            int got = 0;
            sb_clear(&out);
            for (i = 0; i < g.nops; i++) {
                if (fs[i] && shf_getline(fs[i], &line, &nl)) {
                    sb_addn(&out, line.s, line.len);
                    got = 1;
                } else if (fs[i]) {
                    shf_close(fs[i]);
                    fs[i] = NULL;
                    live--;
                }
                if (i < g.nops - 1 && nd && delims[i % nd]) sb_addc(&out, delims[i % nd]);
            }
            if (!got) break;
            sb_addc(&out, '\n');
            sh_write(1, out.s, out.len);
        }
        for (i = 0; i < g.nops; i++)
            if (fs[i]) shf_close(fs[i]);
        xfree(fs);
    }
    xfree(delims);
    sb_free(&line);
    sb_free(&out);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* uniq                                                                */
/* ================================================================== */

typedef struct UniqOpt { int count, dups, alldups, uniq, icase, skipf, skipc, width; } UniqOpt;

static const char *uniq_key(const char *s, int len, UniqOpt *o, int *klen)
{
    const char *e = s + len;
    int f;
    for (f = 0; f < o->skipf && s < e; f++) {
        while (s < e && is_blank((unsigned char)*s)) s++;
        while (s < e && !is_blank((unsigned char)*s)) s++;
    }
    for (f = 0; f < o->skipc && s < e; f++) s++;
    *klen = (int)(e - s);
    if (o->width && *klen > o->width) *klen = o->width;
    return s;
}

static int uniq_eq(SBuf *a, SBuf *b, UniqOpt *o)
{
    int la, lb;
    const char *ka = uniq_key(a->s, a->len, o, &la), *kb = uniq_key(b->s, b->len, o, &lb);
    int i;
    if (la != lb) return 0;
    if (o->icase) {
        for (i = 0; i < la; i++)
            if (tolower((unsigned char)ka[i]) != tolower((unsigned char)kb[i])) return 0;
        return 1;
    }
    return memcmp(ka, kb, la) == 0;
}

static void uniq_out(ShFile *out, SBuf *l, long n, UniqOpt *o)
{
    SBuf b;
    if (o->dups && n < 2) return;
    if (o->uniq && n > 1) return;
    sb_init(&b);
    if (o->count) sb_printf(&b, "%7ld ", n);
    sb_addn(&b, l->s, l->len);
    sb_addc(&b, '\n');
    shf_write(out, b.s, b.len);
    sb_free(&b);
}

int util_uniq(int argc, char **argv)
{
    static const char *const lo[] = { "count", "repeated", "all-repeated?", "unique", "ignore-case",
                                      "skip-fields=", "skip-chars=", "check-chars=", NULL };
    UGetopt g;
    UniqOpt o;
    int c, nl, have = 0, rc = 0;
    long n = 0, v;
    SBuf prev, cur;
    ShFile *in, *out;
    memset(&o, 0, sizeof(o));
    ug_init(&g, argc, argv, "cdDuif:s:w:z", lo);
    g.allow_num = 1;
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'c': case UG_LONG + 0: o.count = 1; break;
        case 'd': case UG_LONG + 1: o.dups = 1; break;
        case 'D': case UG_LONG + 2: o.alldups = 1; break;
        case 'u': case UG_LONG + 3: o.uniq = 1; break;
        case 'i': case UG_LONG + 4: o.icase = 1; break;
        case 'f': case 's': case 'w': case UG_LONG + 5: case UG_LONG + 6: case UG_LONG + 7:
            if (!u_atoi(g.arg, &v) || v < 0) {
                sh_cmd_error(argv[0], "%s: invalid number", g.arg);
                ug_free(&g);
                return 1;
            }
            if (c == 'f' || c == UG_LONG + 5) o.skipf = (int)v;
            else if (c == 's' || c == UG_LONG + 6) o.skipc = (int)v;
            else o.width = (int)v;
            break;
        case UG_NUM: o.skipf = atoi(g.arg); break;
        case 'z': break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); return 1;
        }
    }
    if (g.nops > 2) {
        sh_cmd_error(argv[0], "extra operand '%s'", g.ops[2]);
        ug_free(&g);
        return 1;
    }
    in = u_open_in(argv[0], g.nops > 0 ? g.ops[0] : "-");
    if (!in) {
        ug_free(&g);
        return 1;
    }
    if (g.nops > 1 && strcmp(g.ops[1], "-") != 0) {
        out = shf_open(g.ops[1], OS_WR);
        if (!out) {
            sh_cmd_error(argv[0], "%s: %s", g.ops[1], os_errstr());
            shf_close(in);
            ug_free(&g);
            return 1;
        }
    } else {
        out = shf_ref(sh_fd(1));
    }
    sb_init(&prev);
    sb_init(&cur);
    u_break_reset();
    while (shf_getline(in, &cur, &nl) && !u_break()) {
        if (have && uniq_eq(&prev, &cur, &o)) {
            n++;
            if (o.alldups) {
                if (n == 2) {
                    shf_write(out, prev.s, prev.len);
                    shf_write(out, "\n", 1);
                }
                shf_write(out, cur.s, cur.len);
                shf_write(out, "\n", 1);
            }
            continue;
        }
        if (have && !o.alldups) uniq_out(out, &prev, n, &o);
        {
            SBuf t = prev;
            prev = cur;
            cur = t;
        }
        have = 1;
        n = 1;
    }
    if (have && !o.alldups) uniq_out(out, &prev, n, &o);
    sb_free(&prev);
    sb_free(&cur);
    shf_close(in);
    shf_close(out);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* cut                                                                 */
/* ================================================================== */

typedef struct CutRange { long lo, hi; } CutRange;

static CutRange *cut_list(const char *cmd, const char *s, int *n)
{
    CutRange *r = NULL;
    int cnt = 0, cap = 0;
    while (*s) {
        long lo = 0, hi = 0;
        int haslo = 0, dash = 0;
        while (isdigit((unsigned char)*s)) {
            lo = lo * 10 + (*s++ - '0');
            haslo = 1;
        }
        if (*s == '-') {
            dash = 1;
            s++;
            if (isdigit((unsigned char)*s)) {
                while (isdigit((unsigned char)*s)) hi = hi * 10 + (*s++ - '0');
            } else {
                hi = 0x7fffffffL;
            }
            if (!haslo) lo = 1;
        } else {
            hi = lo;
        }
        if ((!haslo && !dash) || (*s && *s != ',' && *s != ' ') || lo == 0 || (dash && !haslo && hi == 0x7fffffffL)) {
            if (lo == 0 && (haslo || dash)) sh_cmd_error(cmd, "fields and positions are numbered from 1");
            else sh_cmd_error(cmd, "invalid field range");
            xfree(r);
            return NULL;
        }
        if (hi < lo) {
            sh_cmd_error(cmd, "invalid decreasing range");
            xfree(r);
            return NULL;
        }
        if (cnt == cap) {
            cap = cap ? cap * 2 : 8;
            r = xrealloc(r, cap * sizeof(CutRange));
        }
        r[cnt].lo = lo;
        r[cnt].hi = hi;
        cnt++;
        if (*s) s++;
    }
    if (!cnt) {
        sh_cmd_error(cmd, "invalid field range");
        return NULL;
    }
    *n = cnt;
    return r;
}

static int cut_sel(CutRange *r, int n, long i, int compl)
{
    int k;
    for (k = 0; k < n; k++)
        if (i >= r[k].lo && i <= r[k].hi) return !compl;
    return compl;
}

int util_cut(int argc, char **argv)
{
    static const char *const lo[] = { "bytes=", "characters=", "fields=", "delimiter=", "only-delimited",
                                      "complement", "output-delimiter=", NULL };
    UGetopt g;
    int c, i, rc = 0, mode = 0, nr = 0, onlydelim = 0, compl = 0, nl;
    const char *list = NULL, *outdelim = NULL;
    char delim = '\t';
    CutRange *r;
    SBuf line, out;
    ug_init(&g, argc, argv, "b:c:f:d:snz", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'b': case 'c': case 'f': case UG_LONG + 0: case UG_LONG + 1: case UG_LONG + 2:
            if (mode) {
                sh_cmd_error(argv[0], "only one type of list may be specified");
                ug_free(&g);
                return 1;
            }
            mode = c < UG_LONG ? c : (c == UG_LONG + 0 ? 'b' : c == UG_LONG + 1 ? 'c' : 'f');
            list = g.arg;
            break;
        case 'd': case UG_LONG + 3:
            if (strlen(g.arg) > 1) {
                sh_cmd_error(argv[0], "the delimiter must be a single character");
                ug_free(&g);
                return 1;
            }
            delim = g.arg[0];
            break;
        case 's': case UG_LONG + 4: onlydelim = 1; break;
        case UG_LONG + 5: compl = 1; break;
        case UG_LONG + 6: outdelim = g.arg; break;
        case 'n': case 'z': break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); return 1;
        }
    }
    if (!mode) {
        sh_cmd_error(argv[0], "you must specify a list of bytes, characters, or fields");
        ug_free(&g);
        return 1;
    }
    r = cut_list(argv[0], list, &nr);
    if (!r) {
        ug_free(&g);
        return 1;
    }
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    sb_init(&out);
    u_break_reset();
    for (i = 0; i < g.nops; i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 1;
            continue;
        }
        while (shf_getline(f, &line, &nl) && !u_break()) {
            sb_clear(&out);
            if (mode == 'f') {
                const char *s = line.s ? line.s : "", *e = s + line.len, *p;
                long fld = 1;
                int first = 1;
                if (!memchr(s, delim, line.len)) {
                    if (onlydelim) continue;
                    sb_addn(&out, s, line.len);
                } else {
                    while (s <= e) {
                        p = memchr(s, delim, e - s);
                        if (!p) p = e;
                        if (cut_sel(r, nr, fld, compl)) {
                            if (!first) {
                                if (outdelim) sb_adds(&out, outdelim);
                                else sb_addc(&out, delim);
                            }
                            sb_addn(&out, s, (int)(p - s));
                            first = 0;
                        }
                        fld++;
                        s = p + 1;
                    }
                }
            } else {
                long k;
                long last = -1;
                for (k = 0; k < line.len; k++) {
                    if (cut_sel(r, nr, k + 1, compl)) {
                        if (outdelim && last >= 0 && last != k - 1) sb_adds(&out, outdelim);
                        sb_addc(&out, line.s[k]);
                        last = k;
                    }
                }
            }
            sb_addc(&out, '\n');
            sh_write(1, out.s, out.len);
        }
        shf_close(f);
    }
    xfree(r);
    sb_free(&line);
    sb_free(&out);
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* tr                                                                  */
/* ================================================================== */

static int tr_esc(const char **sp)
{
    const char *s = *sp;
    int c;
    if (*s != '\\' || !s[1]) {
        *sp = s + 1;
        return (unsigned char)*s;
    }
    s++;
    switch (*s) {
    case 'n': c = '\n'; s++; break;
    case 't': c = '\t'; s++; break;
    case 'r': c = '\r'; s++; break;
    case 'a': c = 7; s++; break;
    case 'b': c = 8; s++; break;
    case 'f': c = 12; s++; break;
    case 'v': c = 11; s++; break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
        int k = 0;
        c = 0;
        while (k < 3 && *s >= '0' && *s <= '7') {
            c = c * 8 + (*s++ - '0');
            k++;
        }
        c &= 255;
        break;
    }
    default: c = (unsigned char)*s++; break;
    }
    *sp = s;
    return c;
}

static int tr_class(const char *name, int ch)
{
    if (strcmp(name, "upper") == 0) return isupper(ch);
    if (strcmp(name, "lower") == 0) return islower(ch);
    if (strcmp(name, "digit") == 0) return isdigit(ch);
    if (strcmp(name, "alpha") == 0) return isalpha(ch);
    if (strcmp(name, "alnum") == 0) return isalnum(ch);
    if (strcmp(name, "space") == 0) return isspace(ch);
    if (strcmp(name, "blank") == 0) return ch == ' ' || ch == '\t';
    if (strcmp(name, "punct") == 0) return ispunct(ch);
    if (strcmp(name, "print") == 0) return isprint(ch);
    if (strcmp(name, "graph") == 0) return isgraph(ch);
    if (strcmp(name, "cntrl") == 0) return iscntrl(ch);
    if (strcmp(name, "xdigit") == 0) return isxdigit(ch);
    return -1;
}

/* expand a set into out (max 4096); returns length or -1.  repeat
 * constructs [c*n] are allowed in set2 (n == 0: fill, marked *fill) */
static int tr_expand(const char *cmd, const char *s, unsigned char *out, int *fillpos, int *fillch, int set2)
{
    int n = 0;
    *fillpos = -1;
    while (*s && n < 4000) {
        if (s[0] == '[' && s[1] == ':') {
            const char *e = strstr(s + 2, ":]");
            if (e) {
                char name[16];
                int len = (int)(e - s - 2), ch;
                if (len < (int)sizeof(name)) {
                    memcpy(name, s + 2, len);
                    name[len] = 0;
                    if (tr_class(name, 'a') >= 0) {
                        for (ch = 0; ch < 256; ch++)
                            if (tr_class(name, ch)) out[n++] = (unsigned char)ch;
                        s = e + 2;
                        continue;
                    }
                    sh_cmd_error(cmd, "invalid character class '%s'", name);
                    return -1;
                }
            }
        }
        if (s[0] == '[' && s[1] == '=' && s[2] && s[3] == '=' && s[4] == ']') {
            out[n++] = (unsigned char)s[2];
            s += 5;
            continue;
        }
        if (set2 && s[0] == '[' && s[1]) {
            const char *p = s + 1;
            int ch = tr_esc(&p);
            if (*p == '*') {
                const char *q = p + 1;
                long cnt = 0;
                int octal = *q == '0';
                while (isdigit((unsigned char)*q)) cnt = cnt * (octal ? 8 : 10) + (*q++ - '0');
                if (*q == ']') {
                    if (cnt == 0) {
                        *fillpos = n;
                        *fillch = ch;
                    } else {
                        while (cnt-- > 0 && n < 4000) out[n++] = (unsigned char)ch;
                    }
                    s = q + 1;
                    continue;
                }
            }
        }
        {
            int a = tr_esc(&s);
            if (*s == '-' && s[1]) {
                const char *p = s + 1;
                int b = tr_esc(&p);
                if (b < a) {
                    sh_cmd_error(cmd, "range-endpoints of '%c-%c' are in reverse collating sequence order", a, b);
                    return -1;
                }
                for (; a <= b; a++) out[n++] = (unsigned char)a;
                s = p;
            } else {
                out[n++] = (unsigned char)a;
            }
        }
    }
    return n;
}

int util_tr(int argc, char **argv)
{
    static const char *const lo[] = { "complement", "delete", "squeeze-repeats", "truncate-set1", NULL };
    UGetopt g;
    int c, compl = 0, del = 0, squeeze = 0, trunc = 0, n1 = 0, n2 = 0, i, f1, f2, fc1, fc2;
    unsigned char *s1, *s2, map[256], in1[256], insq[256];
    ShFile *in;
    char *buf, *obuf;
    long n;
    int last = -1;
    ug_init(&g, argc, argv, "cCdst", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'c': case 'C': case UG_LONG + 0: compl = 1; break;
        case 'd': case UG_LONG + 1: del = 1; break;
        case 's': case UG_LONG + 2: squeeze = 1; break;
        case 't': case UG_LONG + 3: trunc = 1; break;
        case UG_LONG + 999: ug_free(&g); u_usage(argv[0]); return 0;
        default: ug_free(&g); return 1;
        }
    }
    if (g.nops < 1 || (g.nops < 2 && !del && !squeeze)) {
        sh_cmd_error(argv[0], g.nops < 1 ? "missing operand" : "missing operand after '%s'", g.ops[0]);
        ug_free(&g);
        return 1;
    }
    if (g.nops > 2 || (del && !squeeze && g.nops > 1)) {
        sh_cmd_error(argv[0], "extra operand '%s'", g.ops[del && !squeeze ? 1 : 2]);
        ug_free(&g);
        return 1;
    }
    s1 = xmalloc(4100);
    s2 = xmalloc(4100);
    n1 = tr_expand(argv[0], g.ops[0], s1, &f1, &fc1, 0);
    if (n1 >= 0 && g.nops > 1) n2 = tr_expand(argv[0], g.ops[1], s2, &f2, &fc2, 1);
    if (n1 < 0 || n2 < 0) {
        xfree(s1);
        xfree(s2);
        ug_free(&g);
        return 1;
    }
    memset(in1, 0, sizeof(in1));
    for (i = 0; i < n1; i++) in1[s1[i]] = 1;
    if (compl) {
        int k = 0;
        for (i = 0; i < 256; i++) {
            in1[i] = !in1[i];
            if (in1[i]) s1[k++] = (unsigned char)i;
        }
        n1 = k;
    }
    for (i = 0; i < 256; i++) map[i] = (unsigned char)i;
    if (g.nops > 1 && !del) {
        /* fill [c*] and pad set2 */
        if (f2 >= 0 && n2 < n1) {
            int need = n1 - n2;
            memmove(s2 + f2 + need, s2 + f2, n2 - f2);
            memset(s2 + f2, fc2, need);
            n2 = n1;
        }
        if (trunc && n1 > n2) n1 = n2;
        if (n2 == 0 && n1 > 0) {
            sh_cmd_error(argv[0], "when not truncating set1, string2 must be non-empty");
            xfree(s1);
            xfree(s2);
            ug_free(&g);
            return 1;
        }
        for (i = 0; i < n1; i++) map[s1[i]] = s2[i < n2 ? i : n2 - 1];
    }
    memset(insq, 0, sizeof(insq));
    if (squeeze) {
        if (g.nops > 1) {
            for (i = 0; i < n2; i++) insq[s2[i]] = 1;
        } else {
            for (i = 0; i < 256; i++) insq[i] = in1[i];
        }
    }
    in = sh_fd(0);
    buf = xmalloc(IOBUF);
    obuf = xmalloc(IOBUF);
    u_break_reset();
    while (in && (n = shf_read(in, buf, IOBUF)) > 0) {
        long k, o = 0;
        for (k = 0; k < n; k++) {
            int ch = (unsigned char)buf[k];
            if (del && in1[ch]) continue;
            if (!del) ch = map[ch];
            if (squeeze && insq[ch] && ch == last) continue;
            last = ch;
            obuf[o++] = (char)ch;
        }
        sh_write(1, obuf, o);
        if (u_break()) break;
    }
    xfree(buf);
    xfree(obuf);
    xfree(s1);
    xfree(s2);
    ug_free(&g);
    return 0;
}

/* ================================================================== */
/* grep                                                                */
/* ================================================================== */

typedef struct GPat {
    char *s; int len;          /* fixed string                         */
    Regex *re;
    int anchored;              /* -x applied in the regex              */
    int empty;                 /* empty pattern: matches everywhere    */
} GPat;

typedef struct Grep {
    const char *cmd;
    int icase, invert, count, lfiles, lnofiles, number, fname, only, quiet, nomsg;
    int recursive, word, line, mode, after, before, color, nullname, byteoff;
    long maxcount;
    GPat *pats; int npats;
    StrList include, exclude, exclude_dir;
    const char *label;
    int matched, err, stop;
    int printed_any;           /* for "--" group separators            */
} Grep;

static int g_wordc(int c)
{
    return isalnum(c) || c == '_';
}

static int g_fixed_find(GPat *p, const char *s, int len, int start, int icase, int *so)
{
    int i, k;
    if (p->len == 0) {
        *so = start;
        return 1;
    }
    for (i = start; i + p->len <= len; i++) {
        if (icase) {
            for (k = 0; k < p->len; k++)
                if (tolower((unsigned char)s[i + k]) != tolower((unsigned char)p->s[k])) break;
        } else {
            if (s[i] != p->s[0]) continue;
            for (k = 1; k < p->len; k++)
                if (s[i + k] != p->s[k]) break;
        }
        if (k == p->len) {
            *so = i;
            return 1;
        }
    }
    return 0;
}

/* first raw match of one pattern at or after start */
static int g_raw(Grep *g, GPat *p, const char *s, int len, int start, int *so, int *eo)
{
    if (p->empty) {
        *so = *eo = start;
        return start <= len;
    }
    if (g->mode == 2) {
        if (!g_fixed_find(p, s, len, start, g->icase, so)) return 0;
        *eo = *so + p->len;
        return 1;
    } else {
        ReMatch m[1];
        if (start > len) return 0;
        if (!re_exec(p->re, s + start, 1, m, start > 0 ? RE_NOTBOL : 0)) return 0;
        *so = start + m[0].so;
        *eo = start + m[0].eo;
        return 1;
    }
}

/* match of one pattern honouring -w / -x */
static int g_one(Grep *g, GPat *p, const char *s, int len, int start, int *so, int *eo)
{
    int pos = start;
    while (pos <= len) {
        if (!g_raw(g, p, s, len, pos, so, eo)) return 0;
        if (g->line && !p->anchored) {
            if (*so == 0 && *eo == len) return 1;
            if (g->mode == 2 || p->empty) return 0;
        } else if (g->word) {
            int ok = (*so == 0 || !g_wordc((unsigned char)s[*so - 1])) &&
                     (*eo >= len || !g_wordc((unsigned char)s[*eo]));
            if (ok && (*eo > *so || p->empty)) return 1;
            /* try a shorter match at the same start for regexes */
            if (g->mode != 2 && !p->empty && *eo > *so) {
                int e;
                for (e = *eo - 1; e > *so; e--) {
                    if (g_wordc((unsigned char)s[e]) && e < len && g_wordc((unsigned char)s[e - 1])) continue;
                    {
                        char save = ((char *)s)[e];
                        ReMatch m[1];
                        int r;
                        ((char *)s)[e] = 0;
                        r = re_exec(p->re, s + *so, 1, m, *so > 0 ? RE_NOTBOL : 0);
                        ((char *)s)[e] = save;
                        if (r && m[0].so == 0 && *so + m[0].eo == e &&
                            (e >= len || !g_wordc((unsigned char)s[e]))) {
                            *eo = e;
                            return 1;
                        }
                    }
                }
            }
        } else {
            return 1;
        }
        pos = *so + 1;
    }
    return 0;
}

/* leftmost (then longest) match among all patterns */
static int g_match(Grep *g, const char *s, int len, int start, int *so, int *eo)
{
    int i, found = 0, bs = 0, be = 0;
    for (i = 0; i < g->npats; i++) {
        int a, b;
        if (g_one(g, &g->pats[i], s, len, start, &a, &b)) {
            if (!found || a < bs || (a == bs && b > be)) {
                bs = a;
                be = b;
                found = 1;
            }
        }
    }
    *so = bs;
    *eo = be;
    return found;
}

static void g_prefix(Grep *g, SBuf *b, const char *name, long lineno, int sep)
{
    if (g->fname) {
        if (g->color) sb_printf(b, "\033[35m\033[K%s\033[m\033[K", name);
        else sb_adds(b, name);
        if (g->color) sb_printf(b, "\033[36m\033[K%c\033[m\033[K", sep);
        else sb_addc(b, sep);
    }
    if (g->number) {
        if (g->color) sb_printf(b, "\033[32m\033[K%ld\033[m\033[K\033[36m\033[K%c\033[m\033[K", lineno, sep);
        else sb_printf(b, "%ld%c", lineno, sep);
    }
}

static void g_print_line(Grep *g, SBuf *b, const char *name, long lineno, const char *s, int len, int sep)
{
    sb_clear(b);
    g_prefix(g, b, name, lineno, sep);
    if (g->color && sep == ':' && !g->invert) {
        int pos = 0, so, eo;
        while (pos <= len && g_match(g, s, len, pos, &so, &eo)) {
            if (eo == so) {
                if (so >= len) break;
                sb_addn(b, s + pos, so + 1 - pos);
                pos = so + 1;
                continue;
            }
            sb_addn(b, s + pos, so - pos);
            sb_adds(b, "\033[01;31m\033[K");
            sb_addn(b, s + so, eo - so);
            sb_adds(b, "\033[m\033[K");
            pos = eo;
        }
        if (pos < len) sb_addn(b, s + pos, len - pos);
    } else {
        sb_addn(b, s, len);
    }
    sb_addc(b, '\n');
    sh_write(1, b->s, b->len);
}

typedef struct GCtxLine { SBuf s; long no; } GCtxLine;

static int g_file(Grep *g, ShFile *f, const char *name)
{
    SBuf line, out;
    int nl;
    long lineno = 0, count = 0, lastprinted = 0, afterleft = 0;
    GCtxLine *ring = NULL;
    int rfill = 0, rstart = 0, ctx = g->after > 0 || g->before > 0;
    sb_init(&line);
    sb_init(&out);
    if (g->before > 0) ring = xcalloc(g->before * sizeof(GCtxLine));
    while (shf_getline(f, &line, &nl)) {
        int so, eo, sel;
        const char *s = sb_str(&line);
        lineno++;
        if ((lineno & 63) == 0 && u_break()) {
            g->stop = 1;
            break;
        }
        if (g->maxcount >= 0 && count >= g->maxcount) {
            /* only trailing context remains */
            if (afterleft > 0 && !g->count && !g->lfiles && !g->lnofiles && !g->quiet) {
                g_print_line(g, &out, name, lineno, s, line.len, '-');
                lastprinted = lineno;
                afterleft--;
                continue;
            }
            break;
        }
        sel = g_match(g, s, line.len, 0, &so, &eo);
        if (g->invert) sel = !sel;
        if (sel) {
            count++;
            g->matched = 1;
            if (g->quiet) {
                g->stop = 1;
                break;
            }
            if (g->lfiles || g->lnofiles) break;
            if (g->count) continue;
            if (ctx) {
                long firstno = rfill ? ring[rstart].no : lineno;
                if (g->printed_any && (lastprinted == 0 || firstno > lastprinted + 1))
                    sh_puts(1, g->color ? "\033[36m\033[K--\033[m\033[K\n" : "--\n");
                while (rfill > 0) {
                    GCtxLine *c = &ring[rstart];
                    g_print_line(g, &out, name, c->no, sb_str(&c->s), c->s.len, '-');
                    rstart = (rstart + 1) % g->before;
                    rfill--;
                }
                g->printed_any = 1;
            }
            if (g->only) {
                int pos = 0;
                if (!g->invert) {
                    while (pos <= line.len && g_match(g, s, line.len, pos, &so, &eo)) {
                        if (eo == so) {
                            pos = so + 1;
                            continue;
                        }
                        sb_clear(&out);
                        g_prefix(g, &out, name, lineno, ':');
                        if (g->color) sb_adds(&out, "\033[01;31m\033[K");
                        sb_addn(&out, s + so, eo - so);
                        if (g->color) sb_adds(&out, "\033[m\033[K");
                        sb_addc(&out, '\n');
                        sh_write(1, out.s, out.len);
                        pos = eo;
                    }
                }
            } else {
                g_print_line(g, &out, name, lineno, s, line.len, ':');
            }
            lastprinted = lineno;
            afterleft = g->after;
        } else if (ctx && !g->count && !g->lfiles && !g->lnofiles && !g->quiet) {
            if (afterleft > 0) {
                g_print_line(g, &out, name, lineno, s, line.len, '-');
                lastprinted = lineno;
                afterleft--;
            } else if (g->before > 0) {
                GCtxLine *c;
                if (rfill == g->before) {
                    c = &ring[rstart];
                    rstart = (rstart + 1) % g->before;
                } else {
                    c = &ring[(rstart + rfill) % g->before];
                    rfill++;
                }
                sb_clear(&c->s);
                sb_addn(&c->s, s, line.len);
                c->no = lineno;
            }
        }
    }
    if (g->count) {
        sb_clear(&out);
        if (g->fname) {
            if (g->color) sb_printf(&out, "\033[35m\033[K%s\033[m\033[K\033[36m\033[K:\033[m\033[K", name);
            else sb_printf(&out, "%s:", name);
        }
        sb_printf(&out, "%ld\n", count);
        sh_write(1, out.s, out.len);
    }
    if ((g->lfiles && count > 0) || (g->lnofiles && count == 0)) {
        if (g->color) sh_printf(1, "\033[35m\033[K%s\033[m\033[K", name);
        else sh_puts(1, name);
        if (g->nullname) sh_write(1, "", 1);
        else sh_putc(1, '\n');
    }
    if (ring) {
        int i;
        for (i = 0; i < g->before; i++) sb_free(&ring[i].s);
        xfree(ring);
    }
    sb_free(&line);
    sb_free(&out);
    return count > 0;
}

static int g_glob_list(StrList *l, const char *name)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (pat_match(l->v[i], name, 0)) return 1;
    return 0;
}

static void g_path(Grep *g, const char *path, const char *disp, int top);

static void g_dir(Grep *g, const char *path, const char *disp)
{
    void *d = sh_opendir(path);
    OsDirEnt *e;
    StrList names;
    int i;
    if (!d) {
        if (!g->nomsg) sh_cmd_error(g->cmd, "%s: %s", disp, os_errstr());
        g->err = 1;
        return;
    }
    sl_init(&names);
    e = xmalloc(sizeof(OsDirEnt));
    while (os_readdir(d, e)) sl_adddup(&names, e->name);
    os_closedir(d);
    xfree(e);
    sl_sort(&names, 0);
    for (i = 0; i < names.n && !g->stop; i++) {
        char *p = path_join(path, names.v[i]);
        char *dp = disp ? path_join(disp, names.v[i]) : xstrdup(names.v[i]);
        g_path(g, p, dp, 0);
        xfree(p);
        xfree(dp);
    }
    sl_free(&names);
}

static void g_path(Grep *g, const char *path, const char *disp, int top)
{
    OsDirEnt e;
    ShFile *f;
    const char *dd = disp ? disp : path;
    if (u_break()) {
        g->stop = 1;
        return;
    }
    if (strcmp(path, "-") == 0 && top) {
        f = u_open_in(g->cmd, "-");
        g_file(g, f, g->label ? g->label : "(standard input)");
        shf_close(f);
        return;
    }
    if (sh_stat(path, &e) != 0) {
        if (!g->nomsg) sh_cmd_error(g->cmd, "%s: %s", dd, os_errstr());
        g->err = 1;
        return;
    }
    if (e.type == OS_T_DIR || e.type == OS_T_VOL) {
        if (!g->recursive) {
            if (!g->nomsg) sh_cmd_error(g->cmd, "%s: Is a directory", dd);
            return;
        }
        if (!top && g->exclude_dir.n && g_glob_list(&g->exclude_dir, path_basename(path))) return;
        g_dir(g, path, disp);
        return;
    }
    if (!top || g->recursive) {
        const char *b = path_basename(path);
        if (g->include.n && !g_glob_list(&g->include, b)) return;
        if (g->exclude.n && g_glob_list(&g->exclude, b)) return;
    }
    f = shf_open(path, OS_RD);
    if (!f) {
        if (!g->nomsg) sh_cmd_error(g->cmd, "%s: %s", dd, os_errstr());
        g->err = 1;
        return;
    }
    g_file(g, f, dd);
    shf_close(f);
}

static void g_addpat(StrList *pats, const char *s)
{
    /* newline separated list */
    const char *e;
    for (;;) {
        e = strchr(s, '\n');
        if (!e) {
            sl_adddup(pats, s);
            return;
        }
        sl_add(pats, xstrndup(s, e - s));
        s = e + 1;
    }
}

static int has_backref(const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            if (s[1] >= '1' && s[1] <= '9') return 1;
            s++;
        }
    }
    return 0;
}

int util_grep(int argc, char **argv)
{
    static const char *const lo[] = {
        "extended-regexp", "fixed-strings", "basic-regexp", "regexp=", "file=", "ignore-case",
        "invert-match", "word-regexp", "line-regexp", "count", "files-with-matches",
        "files-without-match", "line-number", "with-filename", "no-filename", "only-matching",
        "quiet", "silent", "no-messages", "recursive", "dereference-recursive", "after-context=",
        "before-context=", "context=", "max-count=", "color?", "colour?", "include=", "exclude=",
        "exclude-dir=", "null", "label=", "text", "binary-files=", "byte-offset", "perl-regexp",
        "no-ignore-case", "line-buffered", NULL };
    UGetopt g;
    Grep gr;
    StrList pats;
    int c, i, havepat = 0, colorwhen = 0, fnameset = -1;
    long v;
    const char *base = path_basename(argv[0]);
    memset(&gr, 0, sizeof(gr));
    gr.cmd = argv[0];
    gr.maxcount = -1;
    if (strcmp(base, "egrep") == 0) gr.mode = 1;
    else if (strcmp(base, "fgrep") == 0) gr.mode = 2;
    sl_init(&pats);
    sl_init(&gr.include);
    sl_init(&gr.exclude);
    sl_init(&gr.exclude_dir);
    u_break_reset();
    ug_init(&g, argc, argv, "EFGPe:f:iyvwxclLnHhoqsrRA:B:C:m:ZaIbUuz", lo);
    g.allow_num = 1;
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'E': case UG_LONG + 0: gr.mode = 1; break;
        case 'P': case UG_LONG + 35: gr.mode = 1; break;
        case 'F': case UG_LONG + 1: gr.mode = 2; break;
        case 'G': case UG_LONG + 2: gr.mode = 0; break;
        case 'e': case UG_LONG + 3: g_addpat(&pats, g.arg); havepat = 1; break;
        case 'f': case UG_LONG + 4: {
            ShFile *f = u_open_in(argv[0], g.arg);
            SBuf l;
            int nl;
            if (!f) {
                ug_free(&g);
                sl_free(&pats);
                return 2;
            }
            sb_init(&l);
            while (shf_getline(f, &l, &nl)) sl_adddup(&pats, sb_str(&l));
            sb_free(&l);
            shf_close(f);
            havepat = 1;
            break;
        }
        case 'i': case 'y': case UG_LONG + 5: gr.icase = 1; break;
        case UG_LONG + 36: gr.icase = 0; break;
        case 'v': case UG_LONG + 6: gr.invert = 1; break;
        case 'w': case UG_LONG + 7: gr.word = 1; break;
        case 'x': case UG_LONG + 8: gr.line = 1; break;
        case 'c': case UG_LONG + 9: gr.count = 1; break;
        case 'l': case UG_LONG + 10: gr.lfiles = 1; gr.lnofiles = 0; break;
        case 'L': case UG_LONG + 11: gr.lnofiles = 1; gr.lfiles = 0; break;
        case 'n': case UG_LONG + 12: gr.number = 1; break;
        case 'H': case UG_LONG + 13: fnameset = 1; break;
        case 'h': case UG_LONG + 14: fnameset = 0; break;
        case 'o': case UG_LONG + 15: gr.only = 1; break;
        case 'q': case UG_LONG + 16: case UG_LONG + 17: gr.quiet = 1; break;
        case 's': case UG_LONG + 18: gr.nomsg = 1; break;
        case 'r': case 'R': case UG_LONG + 19: case UG_LONG + 20: gr.recursive = 1; break;
        case 'A': case 'B': case 'C': case UG_LONG + 21: case UG_LONG + 22: case UG_LONG + 23:
            if (!u_atoi(g.arg, &v) || v < 0) {
                sh_cmd_error(argv[0], "%s: invalid context length argument", g.arg);
                ug_free(&g);
                sl_free(&pats);
                return 2;
            }
            if (c == 'A' || c == UG_LONG + 21 || c == 'C' || c == UG_LONG + 23) gr.after = (int)v;
            if (c == 'B' || c == UG_LONG + 22 || c == 'C' || c == UG_LONG + 23) gr.before = (int)v;
            break;
        case UG_NUM: gr.after = gr.before = atoi(g.arg); break;
        case 'm': case UG_LONG + 24:
            if (!u_atoi(g.arg, &v)) {
                sh_cmd_error(argv[0], "invalid max count");
                ug_free(&g);
                sl_free(&pats);
                return 2;
            }
            gr.maxcount = v < 0 ? -1 : v;
            break;
        case UG_LONG + 25: case UG_LONG + 26:
            if (!g.arg || strcmp(g.arg, "always") == 0 || strcmp(g.arg, "yes") == 0 || strcmp(g.arg, "force") == 0)
                colorwhen = 1;
            else if (strcmp(g.arg, "auto") == 0 || strcmp(g.arg, "tty") == 0 || strcmp(g.arg, "if-tty") == 0)
                colorwhen = 2;
            else colorwhen = 0;
            break;
        case UG_LONG + 27: sl_adddup(&gr.include, g.arg); break;
        case UG_LONG + 28: sl_adddup(&gr.exclude, g.arg); break;
        case UG_LONG + 29: sl_adddup(&gr.exclude_dir, g.arg); break;
        case 'Z': case UG_LONG + 30: gr.nullname = 1; break;
        case UG_LONG + 31: gr.label = g.arg; break;
        case 'b': case UG_LONG + 34: gr.byteoff = 1; break;
        case 'a': case 'I': case 'U': case 'u': case 'z': case UG_LONG + 32: case UG_LONG + 33: case UG_LONG + 37: break;
        case UG_LONG + 999:
            ug_free(&g);
            sl_free(&pats);
            sh_puts(1, "Usage: grep [OPTION]... PATTERNS [FILE]...\n");
            return 0;
        default:
            ug_free(&g);
            sl_free(&pats);
            sh_puts(2, "Usage: grep [OPTION]... PATTERNS [FILE]...\n");
            return 2;
        }
    }
    i = 0;
    if (!havepat) {
        if (g.nops == 0) {
            sh_puts(2, "Usage: grep [OPTION]... PATTERNS [FILE]...\n");
            ug_free(&g);
            return 2;
        }
        g_addpat(&pats, g.ops[0]);
        i = 1;
    }
    gr.color = colorwhen == 1 || (colorwhen == 2 && u_out_is_tty(NULL));
    /* compile */
    gr.pats = xcalloc((pats.n + 1) * sizeof(GPat));
    gr.npats = pats.n;
    for (c = 0; c < pats.n; c++) {
        GPat *p = &gr.pats[c];
        p->s = pats.v[c];
        p->len = (int)strlen(p->s);
        if (gr.mode == 2) continue;
        if (p->len == 0) {
            p->empty = 1;
            continue;
        }
        {
            char *err = NULL, *src = p->s;
            int fl = (gr.mode == 0 ? RE_BASIC : 0) | (gr.icase ? RE_ICASE : 0);
            if (gr.line && !has_backref(p->s)) {
                src = gr.mode == 0 ? str_printf("^\\(%s\\)$", p->s) : str_printf("^(%s)$", p->s);
                p->anchored = 1;
            }
            p->re = re_compile(src, fl, &err);
            if (src != p->s) xfree(src);
            if (!p->re && p->anchored) {
                /* odd pattern: fall back to whole-line check */
                xfree(err);
                err = NULL;
                p->anchored = 0;
                p->re = re_compile(p->s, fl, &err);
            }
            if (!p->re) {
                sh_cmd_error(argv[0], "%s", err ? err : "invalid regular expression");
                xfree(err);
                gr.err = 2;
                break;
            }
        }
    }
    if (gr.err != 2) {
        int nfiles = g.nops - i;
        if (fnameset >= 0) gr.fname = fnameset;
        else gr.fname = nfiles > 1 || gr.recursive;
        if (nfiles == 0) {
            if (gr.recursive) g_path(&gr, ".", NULL, 1);
            else g_path(&gr, "-", "-", 1);
        } else {
            for (; i < g.nops && !gr.stop; i++) g_path(&gr, g.ops[i], g.ops[i], 1);
        }
    }
    for (c = 0; c < gr.npats; c++)
        if (gr.pats[c].re) re_free(gr.pats[c].re);
    xfree(gr.pats);
    sl_free(&pats);
    sl_free(&gr.include);
    sl_free(&gr.exclude);
    sl_free(&gr.exclude_dir);
    ug_free(&g);
    if (gr.err == 2) return 2;
    if (gr.quiet && gr.matched) return 0;
    if (gr.err) return 2;
    return gr.matched ? 0 : 1;
}

/* ================================================================== */
/* sort                                                                */
/* ================================================================== */

#define SK_B_START 0x001       /* skip leading blanks at key start     */
#define SK_B_END   0x002       /* skip leading blanks at key end       */
#define SK_DICT    0x004
#define SK_FOLD    0x008
#define SK_IGNNP   0x010
#define SK_NUM     0x020
#define SK_GEN     0x040
#define SK_HUMAN   0x080
#define SK_MONTH   0x100
#define SK_VER     0x200
#define SK_REV     0x400
#define SK_ORDER   (SK_DICT | SK_FOLD | SK_IGNNP | SK_NUM | SK_GEN | SK_HUMAN | SK_MONTH | SK_VER | SK_REV | SK_B_START | SK_B_END)

typedef struct SortKey { long sword, schar, eword, echar; int flags; int hasend; } SortKey;
typedef struct SortLine { char *s; int len; } SortLine;
typedef struct SortCtx {
    SortKey *keys; int nkeys;
    int gflags;
    int tab;               /* -1 = blank separated */
    int stable, uniq;
} SortCtx;

static const char *sk_begin(SortCtx *c, SortKey *k, const char *p, const char *lim)
{
    long sword = k->sword;
    if (c->tab >= 0) {
        while (p < lim && sword--) {
            while (p < lim && *p != c->tab) ++p;
            if (p < lim) ++p;
        }
    } else {
        while (p < lim && sword--) {
            while (p < lim && is_blank((unsigned char)*p)) ++p;
            while (p < lim && !is_blank((unsigned char)*p)) ++p;
        }
    }
    if (k->flags & SK_B_START)
        while (p < lim && is_blank((unsigned char)*p)) ++p;
    p = (lim - p) < k->schar ? lim : p + k->schar;
    return p;
}

static const char *sk_end(SortCtx *c, SortKey *k, const char *p, const char *lim)
{
    long eword = k->eword, echar = k->echar;
    if (!k->hasend) return lim;
    if (echar == 0) eword++;
    if (c->tab >= 0) {
        while (p < lim && eword--) {
            while (p < lim && *p != c->tab) ++p;
            if (p < lim && (eword || echar)) ++p;
        }
    } else {
        while (p < lim && eword--) {
            while (p < lim && is_blank((unsigned char)*p)) ++p;
            while (p < lim && !is_blank((unsigned char)*p)) ++p;
        }
    }
    if (echar != 0) {
        if (k->flags & SK_B_END)
            while (p < lim && is_blank((unsigned char)*p)) ++p;
        p = (lim - p) < echar ? lim : p + echar;
    }
    return p;
}

/* numeric string compare (-n), no floating point */
static void num_parse(const char *a, const char *e, int *neg, const char **ip, int *il, const char **fp, int *fl)
{
    while (a < e && is_blank((unsigned char)*a)) a++;
    *neg = 0;
    if (a < e && *a == '-') {
        *neg = 1;
        a++;
    }
    while (a < e && *a == '0') a++;
    *ip = a;
    while (a < e && isdigit((unsigned char)*a)) a++;
    *il = (int)(a - *ip);
    *fp = a;
    *fl = 0;
    if (a < e && *a == '.') {
        a++;
        *fp = a;
        while (a < e && isdigit((unsigned char)*a)) a++;
        *fl = (int)(a - *fp);
        while (*fl > 0 && (*fp)[*fl - 1] == '0') (*fl)--;
    }
}

static int num_cmp(const char *a, const char *ae, const char *b, const char *be)
{
    int na, nb, ila, ilb, fla, flb, r = 0, za, zb, i;
    const char *ia, *ib, *fa, *fb;
    num_parse(a, ae, &na, &ia, &ila, &fa, &fla);
    num_parse(b, be, &nb, &ib, &ilb, &fb, &flb);
    za = ila == 0 && fla == 0;
    zb = ilb == 0 && flb == 0;
    if (za) na = 0;
    if (zb) nb = 0;
    if (na != nb) return na ? -1 : 1;
    if (ila != ilb) r = ila < ilb ? -1 : 1;
    else {
        r = memcmp(ia, ib, ila);
        if (!r) {
            for (i = 0; i < fla && i < flb; i++)
                if (fa[i] != fb[i]) break;
            if (i < fla && i < flb) r = (unsigned char)fa[i] < (unsigned char)fb[i] ? -1 : 1;
            else if (fla != flb) r = fla < flb ? -1 : 1;
        }
    }
    if (r) r = r < 0 ? -1 : 1;
    return na ? -r : r;
}

static int human_rank(const char *a, const char *e)
{
    static const char suf[] = "KMGTPEZY";
    const char *p;
    while (a < e && is_blank((unsigned char)*a)) a++;
    if (a < e && *a == '-') a++;
    while (a < e && (isdigit((unsigned char)*a) || *a == '.')) a++;
    if (a < e && *a) {
        if (*a == 'k') return 1;
        p = strchr(suf, *a);
        if (p) return (int)(p - suf) + 1;
    }
    return 0;
}

static int human_cmp(const char *a, const char *ae, const char *b, const char *be)
{
    int ra = human_rank(a, ae), rb = human_rank(b, be), na, nb;
    const char *p = a, *q = b;
    while (p < ae && is_blank((unsigned char)*p)) p++;
    while (q < be && is_blank((unsigned char)*q)) q++;
    na = p < ae && *p == '-';
    nb = q < be && *q == '-';
    if (na != nb) return na ? -1 : 1;
    if (ra != rb) return na ? (ra < rb ? 1 : -1) : (ra < rb ? -1 : 1);
    return num_cmp(a, ae, b, be);
}

/* general numeric: own parser to double.  Rank: 0 not a number,
 * 1 nan, 2 -inf, 3 finite, 4 +inf (GNU order) */
static int gen_parse(const char *a, const char *e, double *out)
{
    double v = 0, scale = 1;
    int neg = 0, any = 0, ex = 0, eneg = 0;
    while (a < e && isspace((unsigned char)*a)) a++;
    if (a < e && (*a == '-' || *a == '+')) neg = *a++ == '-';
    if (e - a >= 3 && str_ncasecmp(a, "inf", 3) == 0) return neg ? 2 : 4;
    if (e - a >= 3 && str_ncasecmp(a, "nan", 3) == 0) return 1;
    while (a < e && isdigit((unsigned char)*a)) {
        v = v * 10 + (*a++ - '0');
        any = 1;
    }
    if (a < e && *a == '.') {
        a++;
        while (a < e && isdigit((unsigned char)*a)) {
            scale /= 10;
            v += (*a++ - '0') * scale;
            any = 1;
        }
    }
    if (!any) return 0;
    if (a < e && (*a == 'e' || *a == 'E')) {
        const char *p = a + 1;
        if (p < e && (*p == '-' || *p == '+')) eneg = *p++ == '-';
        if (p < e && isdigit((unsigned char)*p)) {
            while (p < e && isdigit((unsigned char)*p)) ex = ex * 10 + (*p++ - '0');
            if (ex > 300) return eneg ? 3 : (neg ? 2 : 4);
            while (ex-- > 0) v = eneg ? v / 10 : v * 10;
        }
    }
    *out = neg ? -v : v;
    return 3;
}

static int gen_cmp(const char *a, const char *ae, const char *b, const char *be)
{
    double x = 0, y = 0;
    int ha = gen_parse(a, ae, &x), hb = gen_parse(b, be, &y);
    if (ha != hb) return ha < hb ? -1 : 1;
    if (ha != 3) return 0;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int month_of(const char *a, const char *e)
{
    static const char *const m[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int i;
    while (a < e && is_blank((unsigned char)*a)) a++;
    if (e - a < 3) return 0;
    for (i = 0; i < 12; i++)
        if (toupper((unsigned char)a[0]) == m[i][0] && toupper((unsigned char)a[1]) == m[i][1] &&
            toupper((unsigned char)a[2]) == m[i][2])
            return i + 1;
    return 0;
}

static int ver_order(int c)
{
    if (c == 0) return 0;
    if (isalpha(c)) return c;
    if (c == '~') return -1;
    return c + 256;
}

static int ver_cmp(const char *a, const char *ae, const char *b, const char *be)
{
    while (a < ae || b < be) {
        int first = 0;
        while ((a < ae && !isdigit((unsigned char)*a)) || (b < be && !isdigit((unsigned char)*b))) {
            int ac = a < ae && !isdigit((unsigned char)*a) ? ver_order((unsigned char)*a) : 0;
            int bc = b < be && !isdigit((unsigned char)*b) ? ver_order((unsigned char)*b) : 0;
            if (ac != bc) return ac < bc ? -1 : 1;
            if (a < ae && !isdigit((unsigned char)*a)) a++;
            if (b < be && !isdigit((unsigned char)*b)) b++;
        }
        while (a < ae && *a == '0') a++;
        while (b < be && *b == '0') b++;
        while (a < ae && b < be && isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            if (!first) first = *a - *b;
            a++;
            b++;
        }
        if (a < ae && isdigit((unsigned char)*a)) return 1;
        if (b < be && isdigit((unsigned char)*b)) return -1;
        if (first) return first < 0 ? -1 : 1;
    }
    return 0;
}

static int text_cmp(const char *a, const char *ae, const char *b, const char *be, int fl)
{
    if (!(fl & (SK_DICT | SK_FOLD | SK_IGNNP))) {
        int la = (int)(ae - a), lb = (int)(be - b);
        int r = memcmp(a, b, la < lb ? la : lb);
        if (r) return r < 0 ? -1 : 1;
        return la < lb ? -1 : la > lb;
    }
    for (;;) {
        int ca, cb;
        if (fl & (SK_DICT | SK_IGNNP)) {
            while (a < ae && ((fl & SK_DICT) ? !(isalnum((unsigned char)*a) || is_blank((unsigned char)*a))
                                             : !isprint((unsigned char)*a))) a++;
            while (b < be && ((fl & SK_DICT) ? !(isalnum((unsigned char)*b) || is_blank((unsigned char)*b))
                                             : !isprint((unsigned char)*b))) b++;
        }
        if (a >= ae || b >= be) return (a < ae) - (b < be);
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (fl & SK_FOLD) {
            ca = toupper(ca);
            cb = toupper(cb);
        }
        if (ca != cb) return ca < cb ? -1 : 1;
    }
}

static int key_cmp(const char *a, const char *ae, const char *b, const char *be, int fl)
{
    if (fl & SK_NUM) return num_cmp(a, ae, b, be);
    if (fl & SK_HUMAN) return human_cmp(a, ae, b, be);
    if (fl & SK_GEN) return gen_cmp(a, ae, b, be);
    if (fl & SK_MONTH) {
        int ma = month_of(a, ae), mb = month_of(b, be);
        return ma < mb ? -1 : ma > mb;
    }
    if (fl & SK_VER) return ver_cmp(a, ae, b, be);
    return text_cmp(a, ae, b, be, fl);
}

static int sort_keycmp(SortCtx *c, const SortLine *x, const SortLine *y)
{
    int i, r;
    if (c->nkeys == 0) {
        const char *a = x->s, *ae = x->s + x->len, *b = y->s, *be = y->s + y->len;
        if (c->gflags & SK_B_START) {
            while (a < ae && is_blank((unsigned char)*a)) a++;
            while (b < be && is_blank((unsigned char)*b)) b++;
        }
        r = key_cmp(a, ae, b, be, c->gflags);
        return (c->gflags & SK_REV) ? -r : r;
    }
    for (i = 0; i < c->nkeys; i++) {
        SortKey *k = &c->keys[i];
        const char *xl = x->s + x->len, *yl = y->s + y->len;
        const char *a = sk_begin(c, k, x->s, xl), *ae = sk_end(c, k, x->s, xl);
        const char *b = sk_begin(c, k, y->s, yl), *be = sk_end(c, k, y->s, yl);
        if (ae < a) ae = a;
        if (be < b) be = b;
        r = key_cmp(a, ae, b, be, k->flags);
        if (r) return (k->flags & SK_REV) ? -r : r;
    }
    return 0;
}

static int sort_cmp(const void *pa, const void *pb, void *ud)
{
    SortCtx *c = ud;
    const SortLine *x = pa, *y = pb;
    int r = sort_keycmp(c, x, y);
    if (r || c->stable || c->uniq) return r;
    if (c->nkeys == 0 && !(c->gflags & (SK_ORDER & ~SK_REV))) return r;
    r = text_cmp(x->s, x->s + x->len, y->s, y->s + y->len, 0);
    return (c->gflags & SK_REV) ? -r : r;
}

/* parse "F[.C][opts]" */
static int sk_parse_pos(const char *s, long *w, long *ch, int *flags, const char **end, int isend)
{
    long v = 0;
    int any = 0;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s++ - '0');
        any = 1;
    }
    if (!any) return -1;
    *w = v;
    *ch = isend ? 0 : 1;
    if (*s == '.') {
        long cv = 0;
        s++;
        any = 0;
        while (isdigit((unsigned char)*s)) {
            cv = cv * 10 + (*s++ - '0');
            any = 1;
        }
        if (!any) return -1;
        *ch = cv;
    }
    while (*s && strchr("bdfgiMhnRrV", *s)) {
        switch (*s) {
        case 'b': *flags |= isend ? SK_B_END : SK_B_START; break;
        case 'd': *flags |= SK_DICT; break;
        case 'f': *flags |= SK_FOLD; break;
        case 'g': *flags |= SK_GEN; break;
        case 'i': *flags |= SK_IGNNP; break;
        case 'M': *flags |= SK_MONTH; break;
        case 'h': *flags |= SK_HUMAN; break;
        case 'n': *flags |= SK_NUM; break;
        case 'r': *flags |= SK_REV; break;
        case 'V': *flags |= SK_VER; break;
        case 'R': break;
        }
        s++;
    }
    *end = s;
    return 0;
}

static int sort_addkey(SortCtx *c, const char *cmd, const char *spec)
{
    SortKey k;
    const char *s = spec;
    memset(&k, 0, sizeof(k));
    if (sk_parse_pos(s, &k.sword, &k.schar, &k.flags, &s, 0) != 0 || k.sword == 0 || k.schar == 0) {
        sh_cmd_error(cmd, "invalid number at field start: invalid count at start of '%s'", spec);
        return -1;
    }
    k.sword--;
    k.schar--;
    if (*s == ',') {
        s++;
        if (sk_parse_pos(s, &k.eword, &k.echar, &k.flags, &s, 1) != 0 || k.eword == 0) {
            sh_cmd_error(cmd, "invalid number after ',': invalid count at start of '%s'", spec);
            return -1;
        }
        k.eword--;
        k.hasend = 1;
    }
    if (*s) {
        sh_cmd_error(cmd, "stray character in field spec: invalid field specification '%s'", spec);
        return -1;
    }
    c->keys = xrealloc(c->keys, (c->nkeys + 1) * sizeof(SortKey));
    c->keys[c->nkeys++] = k;
    return 0;
}

int util_sort(int argc, char **argv)
{
    static const char *const lo[] = { "reverse", "numeric-sort", "unique", "ignore-case", "ignore-leading-blanks",
                                      "key=", "field-separator=", "output=", "human-numeric-sort", "stable",
                                      "general-numeric-sort", "dictionary-order", "ignore-nonprinting",
                                      "month-sort", "version-sort", "check?", "merge", "random-sort",
                                      "zero-terminated", "buffer-size=", "parallel=", "temporary-directory=", NULL };
    UGetopt g;
    SortCtx c;
    int ch, i, rc = 0, check = 0, n = 0, cap = 0, nl;
    const char *outfile = NULL;
    SortLine **lines = NULL;
    SBuf line;
    ShFile *out;
    memset(&c, 0, sizeof(c));
    c.tab = -1;
    u_break_reset();
    ug_init(&g, argc, argv, "rnufbk:t:o:hsgdiMVcCmRzS:T:", lo);
    while ((ch = ug_next(&g)) != -1) {
        switch (ch) {
        case 'r': case UG_LONG + 0: c.gflags |= SK_REV; break;
        case 'n': case UG_LONG + 1: c.gflags |= SK_NUM; break;
        case 'u': case UG_LONG + 2: c.uniq = 1; break;
        case 'f': case UG_LONG + 3: c.gflags |= SK_FOLD; break;
        case 'b': case UG_LONG + 4: c.gflags |= SK_B_START | SK_B_END; break;
        case 'k': case UG_LONG + 5:
            if (sort_addkey(&c, argv[0], g.arg) != 0) {
                xfree(c.keys);
                ug_free(&g);
                return 2;
            }
            break;
        case 't': case UG_LONG + 6:
            if (strlen(g.arg) != 1 && strcmp(g.arg, "\\0") != 0) {
                sh_cmd_error(argv[0], "multi-character tab '%s'", g.arg);
                xfree(c.keys);
                ug_free(&g);
                return 2;
            }
            c.tab = g.arg[1] ? 0 : (unsigned char)g.arg[0];
            break;
        case 'o': case UG_LONG + 7: outfile = g.arg; break;
        case 'h': case UG_LONG + 8: c.gflags |= SK_HUMAN; break;
        case 's': case UG_LONG + 9: c.stable = 1; break;
        case 'g': case UG_LONG + 10: c.gflags |= SK_GEN; break;
        case 'd': case UG_LONG + 11: c.gflags |= SK_DICT; break;
        case 'i': case UG_LONG + 12: c.gflags |= SK_IGNNP; break;
        case 'M': case UG_LONG + 13: c.gflags |= SK_MONTH; break;
        case 'V': case UG_LONG + 14: c.gflags |= SK_VER; break;
        case 'c': case 'C': case UG_LONG + 15: check = ch == 'C' ? 2 : 1; break;
        case 'm': case 'R': case 'z': case 'S': case 'T': case UG_LONG + 16: case UG_LONG + 17:
        case UG_LONG + 18: case UG_LONG + 19: case UG_LONG + 20: case UG_LONG + 21: break;
        case UG_LONG + 999: xfree(c.keys); ug_free(&g); u_usage(argv[0]); return 0;
        default: xfree(c.keys); ug_free(&g); u_usage(argv[0]); return 2;
        }
    }
    /* keys without ordering options inherit the global ones */
    for (i = 0; i < c.nkeys; i++)
        if (!(c.keys[i].flags & SK_ORDER)) c.keys[i].flags |= c.gflags;
    if (g.nops == 0) g.ops[g.nops++] = "-";
    sb_init(&line);
    for (i = 0; i < g.nops && !u_break(); i++) {
        ShFile *f = u_open_in(argv[0], g.ops[i]);
        if (!f) {
            rc = 2;
            continue;
        }
        while (shf_getline(f, &line, &nl)) {
            SortLine *l;
            if (n == cap) {
                cap = cap ? cap * 2 : 256;
                lines = xrealloc(lines, cap * sizeof(SortLine *));
            }
            l = xmalloc(sizeof(SortLine) + line.len + 1);
            l->s = (char *)(l + 1);
            l->len = line.len;
            memcpy(l->s, sb_str(&line), line.len + 1);
            lines[n++] = l;
            if ((n & 1023) == 0 && u_break()) break;
        }
        shf_close(f);
    }
    sb_free(&line);
    if (rc == 0 && !u_break()) {
        if (check) {
            for (i = 1; i < n; i++) {
                int r = sort_cmp(lines[i - 1], lines[i], &c);
                if (r > 0 || (c.uniq && r == 0)) {
                    if (check == 1)
                        sh_printf(2, "%s: %s:%d: disorder: %s\n", argv[0], strcmp(g.ops[0], "-") == 0 ? "-" : g.ops[0],
                                  i + 1, lines[i]->s);
                    rc = 1;
                    break;
                }
            }
        } else {
            u_msort((void **)lines, n, sort_cmp, &c);
            if (outfile) {
                out = shf_open(outfile, OS_WR);
                if (!out) {
                    sh_cmd_error(argv[0], "open failed: %s: %s", outfile, os_errstr());
                    rc = 2;
                }
            } else {
                out = shf_ref(sh_fd(1));
            }
            if (out) {
                for (i = 0; i < n; i++) {
                    if (c.uniq && i > 0 && sort_keycmp(&c, lines[i - 1], lines[i]) == 0) continue;
                    shf_write(out, lines[i]->s, lines[i]->len);
                    shf_write(out, "\n", 1);
                }
                shf_close(out);
            }
        }
    }
    for (i = 0; i < n; i++) xfree(lines[i]);
    xfree(lines);
    xfree(c.keys);
    ug_free(&g);
    if (u_break()) return 130;
    return rc;
}

/* ================================================================== */
/* sed                                                                 */
/* ================================================================== */

enum { SA_NONE, SA_LINE, SA_LAST, SA_RE, SA_STEP, SA_PLUS, SA_MULT, SA_ZERO };

typedef struct SedAddr { int type; long n, step; Regex *re; } SedAddr;

typedef struct SedCmd {
    SedAddr a1, a2;
    int neg;
    int cmd;
    int active; long rend;
    char *text;            /* a i c text, labels, file names          */
    Regex *re; int re_empty;
    char *repl;
    int gflag, pflag, nth;
    ShFile *wf;            /* w / W / s///w / R                        */
    unsigned char *ytab;
    int jump;              /* '{': index of '}'; b t T: target or -1   */
    long ival;             /* q Q exit code, l width                   */
} SedCmd;

typedef struct SedFile { char *name; ShFile *f; int rd; } SedFile;

typedef struct Sed {
    const char *cmd;
    SedCmd *c; int nc, cap;
    int nflag, ere, separate, inplace;
    SedFile *files_w; int nfw;
    /* parse */
    const char *src; int pos, len;
    int err;
    /* runtime */
    SBuf ps, hs, app;
    long lineno;
    int quit, exitcode, tflag;
    Regex *last_re;
    ShFile *out; int missing_nl;
    char **files; int nfiles, fidx;
    ShFile *in; const char *inname, *curname;
    SBuf next; int have_next, next_nl, cur_nl, next_first;
    int ioerr;
} Sed;

static void sed_perr(Sed *s, const char *msg)
{
    if (!s->err) sh_cmd_error(s->cmd, "-e expression #1, char %d: %s", s->pos, msg);
    s->err = 1;
}

static int sp_peek(Sed *s)
{
    return s->pos < s->len ? (unsigned char)s->src[s->pos] : -1;
}

static int sp_get(Sed *s)
{
    return s->pos < s->len ? (unsigned char)s->src[s->pos++] : -1;
}

static void sp_ws(Sed *s)
{
    while (s->pos < s->len && (s->src[s->pos] == ' ' || s->src[s->pos] == '\t')) s->pos++;
}

static ShFile *sed_wfile(Sed *s, const char *name)
{
    int i;
    ShFile *f;
    for (i = 0; i < s->nfw; i++)
        if (!s->files_w[i].rd && strcmp(s->files_w[i].name, name) == 0) return s->files_w[i].f;
    if (strcmp(name, "/dev/stdout") == 0) f = shf_ref(sh_fd(1));
    else if (strcmp(name, "/dev/stderr") == 0) f = shf_ref(sh_fd(2));
    else f = shf_open(name, OS_WR);
    if (!f) {
        sh_cmd_error(s->cmd, "couldn't open file %s: %s", name, os_errstr());
        s->err = 1;
        return NULL;
    }
    s->files_w = xrealloc(s->files_w, (s->nfw + 1) * sizeof(SedFile));
    s->files_w[s->nfw].name = xstrdup(name);
    s->files_w[s->nfw].f = f;
    s->files_w[s->nfw].rd = 0;
    s->nfw++;
    return f;
}

/* read delimited regex text; converts \delim and \n */
static char *sp_delimited(Sed *s, int delim, int is_regex)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (;;) {
        int c = sp_get(s);
        if (c < 0) {
            sb_free(&b);
            return NULL;
        }
        if (c == delim) break;
        if (c == '\n' && is_regex) {
            sb_free(&b);
            return NULL;
        }
        if (c == '\\') {
            int d = sp_get(s);
            if (d < 0) {
                sb_free(&b);
                return NULL;
            }
            if (d == delim) {
                sb_addc(&b, d);
            } else if (d == 'n' && is_regex) {
                sb_addc(&b, '\n');
            } else if (d == '\n') {
                sb_addc(&b, '\n');
            } else if (d == 't' && is_regex) {
                sb_addc(&b, '\t');
            } else {
                sb_addc(&b, '\\');
                sb_addc(&b, d);
            }
            continue;
        }
        sb_addc(&b, c);
    }
    return sb_take(&b);
}

static Regex *sed_compile(Sed *s, const char *pat, int icase)
{
    char *err = NULL;
    Regex *re = re_compile(pat, (s->ere ? 0 : RE_BASIC) | (icase ? RE_ICASE : 0), &err);
    if (!re) {
        sed_perr(s, err ? err : "invalid regular expression");
        xfree(err);
    }
    return re;
}

static int sp_num(Sed *s, long *out)
{
    long v = 0;
    int any = 0;
    while (sp_peek(s) >= '0' && sp_peek(s) <= '9') {
        v = v * 10 + (sp_get(s) - '0');
        any = 1;
    }
    *out = v;
    return any;
}

/* returns 1 if an address was parsed */
static int sp_addr(Sed *s, SedAddr *a, int second)
{
    int c = sp_peek(s);
    memset(a, 0, sizeof(*a));
    if (c >= '0' && c <= '9') {
        sp_num(s, &a->n);
        a->type = SA_LINE;
        if (sp_peek(s) == '~' && !second) {
            s->pos++;
            sp_num(s, &a->step);
            a->type = SA_STEP;
        } else if (a->n == 0 && !second) {
            a->type = SA_ZERO;
        }
        return 1;
    }
    if (second && (c == '+' || c == '~')) {
        s->pos++;
        if (!sp_num(s, &a->n)) {
            sed_perr(s, "expected number");
            return 0;
        }
        a->type = c == '+' ? SA_PLUS : SA_MULT;
        return 1;
    }
    if (c == '$') {
        s->pos++;
        a->type = SA_LAST;
        return 1;
    }
    if (c == '/' || c == '\\') {
        int delim = '/', icase = 0;
        char *pat;
        s->pos++;
        if (c == '\\') delim = sp_get(s);
        pat = sp_delimited(s, delim, 1);
        if (!pat) {
            sed_perr(s, "unterminated address regex");
            return 0;
        }
        while (sp_peek(s) == 'I' || sp_peek(s) == 'M') {
            if (sp_get(s) == 'I') icase = 1;
        }
        a->type = SA_RE;
        if (*pat) a->re = sed_compile(s, pat, icase);
        xfree(pat);
        return 1;
    }
    return 0;
}

static int sp_endcmd(Sed *s)
{
    int c;
    sp_ws(s);
    c = sp_peek(s);
    if (c < 0 || c == '}' || c == '#') return 1;
    if (c == ';' || c == '\n') {
        s->pos++;
        return 1;
    }
    sed_perr(s, "extra characters after command");
    return 0;
}

/* text for a i c */
static char *sp_text(Sed *s)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    sp_ws(s);
    if (sp_peek(s) == '\\') {
        s->pos++;
        if (sp_peek(s) == '\n') s->pos++;
    }
    for (;;) {
        int c = sp_get(s);
        if (c < 0 || c == '\n') break;
        if (c == '\\') {
            c = sp_get(s);
            if (c < 0) break;
        }
        sb_addc(&b, c);
    }
    return sb_take(&b);
}

static char *sp_label(Sed *s, int forbranch)
{
    int st;
    sp_ws(s);
    st = s->pos;
    while (s->pos < s->len && s->src[s->pos] != '\n' && s->src[s->pos] != ';' &&
           !(forbranch && s->src[s->pos] == '}'))
        s->pos++;
    {
        int e = s->pos;
        while (e > st && (s->src[e - 1] == ' ' || s->src[e - 1] == '\t')) e--;
        return xstrndup(s->src + st, e - st);
    }
}

static char *sp_filename(Sed *s)
{
    int st;
    sp_ws(s);
    st = s->pos;
    while (s->pos < s->len && s->src[s->pos] != '\n') s->pos++;
    return xstrndup(s->src + st, s->pos - st);
}

static int sed_parse(Sed *s)
{
    int *stack = NULL, sp = 0, i, j;
    while (!s->err) {
        SedCmd *c;
        int ch;
        /* skip blanks and separators */
        while (s->pos < s->len && (isspace((unsigned char)s->src[s->pos]) || s->src[s->pos] == ';')) s->pos++;
        if (s->pos >= s->len) break;
        if (s->src[s->pos] == '#') {
            while (s->pos < s->len && s->src[s->pos] != '\n') s->pos++;
            continue;
        }
        if (s->nc == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 16;
            s->c = xrealloc(s->c, s->cap * sizeof(SedCmd));
        }
        c = &s->c[s->nc];
        memset(c, 0, sizeof(*c));
        c->jump = -1;
        if (sp_addr(s, &c->a1, 0)) {
            sp_ws(s);
            if (sp_peek(s) == ',') {
                s->pos++;
                sp_ws(s);
                if (!sp_addr(s, &c->a2, 1)) {
                    sed_perr(s, "unexpected `,'");
                    break;
                }
            }
        }
        if (s->err) break;
        if (c->a1.type == SA_ZERO && c->a2.type != SA_RE) {
            sed_perr(s, "invalid usage of line address 0");
            break;
        }
        sp_ws(s);
        while (sp_peek(s) == '!') {
            c->neg = 1;
            s->pos++;
            sp_ws(s);
        }
        ch = sp_get(s);
        if (ch < 0) {
            sed_perr(s, "missing command");
            break;
        }
        c->cmd = ch;
        s->nc++;
        switch (ch) {
        case '{':
            stack = xrealloc(stack, (sp + 1) * sizeof(int));
            stack[sp++] = s->nc - 1;
            break;
        case '}':
            if (sp == 0) {
                sed_perr(s, "unexpected `}'");
                break;
            }
            if (c->a1.type != SA_NONE) {
                sed_perr(s, "} doesn't want any addresses");
                break;
            }
            s->c[stack[--sp]].jump = s->nc - 1;
            sp_endcmd(s);
            break;
        case '=': case 'd': case 'D': case 'g': case 'G': case 'h': case 'H': case 'n': case 'N':
        case 'p': case 'P': case 'x': case 'z': case 'F':
            sp_endcmd(s);
            break;
        case 'l': case 'L': case 'q': case 'Q':
            sp_ws(s);
            c->ival = ch == 'l' ? 70 : 0;
            if (sp_peek(s) >= '0' && sp_peek(s) <= '9') sp_num(s, &c->ival);
            if (ch == 'L') c->cmd = 'l';
            if ((ch == 'q' || ch == 'Q') && c->a2.type != SA_NONE) {
                sed_perr(s, "command only uses one address");
                break;
            }
            sp_endcmd(s);
            break;
        case 'a': case 'i': case 'c':
            c->text = sp_text(s);
            break;
        case ':':
            if (c->a1.type != SA_NONE) {
                sed_perr(s, ": doesn't want any addresses");
                break;
            }
            c->text = sp_label(s, 0);
            if (!*c->text) sed_perr(s, "\":\" lacks a label");
            if (sp_peek(s) == ';') s->pos++;
            break;
        case 'b': case 't': case 'T':
            c->text = sp_label(s, 1);
            if (sp_peek(s) == ';') s->pos++;
            break;
        case 'r': case 'R': case 'w': case 'W':
            c->text = sp_filename(s);
            if (!*c->text) {
                sed_perr(s, "missing filename in r/R/w/W commands");
                break;
            }
            if (ch == 'w' || ch == 'W') c->wf = sed_wfile(s, c->text);
            break;
        case 'e': case 'v':
            sp_filename(s);
            break;
        case 's': {
            int delim = sp_get(s), icase = 0;
            char *pat, *rep;
            if (delim < 0 || delim == '\n' || delim == '\\') {
                sed_perr(s, "unterminated `s' command");
                break;
            }
            pat = sp_delimited(s, delim, 1);
            if (!pat) {
                sed_perr(s, "unterminated `s' command");
                break;
            }
            /* replacement: keep escapes, but resolve \delim and \newline */
            {
                SBuf b;
                int ok = 0;
                sb_init(&b);
                sb_addn(&b, "", 0);
                for (;;) {
                    int d = sp_get(s);
                    if (d < 0) break;
                    if (d == delim) {
                        ok = 1;
                        break;
                    }
                    if (d == '\\') {
                        int e = sp_get(s);
                        if (e < 0) break;
                        if (e == delim && delim != '&') sb_addc(&b, e);
                        else if (e == '\n') sb_addc(&b, '\n');
                        else {
                            sb_addc(&b, '\\');
                            sb_addc(&b, e);
                        }
                        continue;
                    }
                    sb_addc(&b, d);
                }
                rep = sb_take(&b);
                if (!ok) {
                    xfree(pat);
                    xfree(rep);
                    sed_perr(s, "unterminated `s' command");
                    break;
                }
            }
            c->repl = rep;
            c->nth = 1;
            for (;;) {
                int f = sp_peek(s);
                if (f == 'g') c->gflag = 1;
                else if (f == 'p') c->pflag = 1;
                else if (f == 'i' || f == 'I') icase = 1;
                else if (f == 'm' || f == 'M' || f == 'e') ;
                else if (f >= '0' && f <= '9') {
                    long v;
                    sp_num(s, &v);
                    if (v == 0) {
                        sed_perr(s, "number option to `s' command may not be zero");
                        break;
                    }
                    c->nth = (int)v;
                    continue;
                } else if (f == 'w') {
                    s->pos++;
                    c->text = sp_filename(s);
                    c->wf = sed_wfile(s, c->text);
                    break;
                } else break;
                s->pos++;
            }
            if (s->err) {
                xfree(pat);
                break;
            }
            if (*pat) c->re = sed_compile(s, pat, icase);
            else c->re_empty = 1;
            xfree(pat);
            if (!s->err && c->re) {
                /* validate back references */
                const char *r = c->repl;
                for (; *r; r++) {
                    if (*r == '\\' && r[1]) {
                        if (r[1] >= '1' && r[1] <= '9' && r[1] - '0' > re_nsub(c->re)) {
                            sed_perr(s, "invalid reference on `s' command's RHS");
                            break;
                        }
                        r++;
                    }
                }
            }
            if (!s->err && !c->wf) sp_endcmd(s);
            break;
        }
        case 'y': {
            int delim = sp_get(s), k;
            char *a, *b;
            int la, lb;
            a = delim > 0 ? sp_delimited(s, delim, 0) : NULL;
            b = a ? sp_delimited(s, delim, 0) : NULL;
            if (!a || !b) {
                xfree(a);
                sed_perr(s, "unterminated `y' command");
                break;
            }
            /* unescape \\ and \n */
            for (k = 0; k < 2; k++) {
                char *p = k ? b : a, *q = p;
                while (*p) {
                    if (*p == '\\' && p[1]) {
                        p++;
                        *q++ = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
                        p++;
                    } else {
                        *q++ = *p++;
                    }
                }
                *q = 0;
            }
            la = (int)strlen(a);
            lb = (int)strlen(b);
            if (la != lb) {
                xfree(a);
                xfree(b);
                sed_perr(s, "strings for `y' command are different lengths");
                break;
            }
            c->ytab = xmalloc(256);
            for (k = 0; k < 256; k++) c->ytab[k] = (unsigned char)k;
            for (k = 0; k < la; k++) c->ytab[(unsigned char)a[k]] = (unsigned char)b[k];
            xfree(a);
            xfree(b);
            sp_endcmd(s);
            break;
        }
        default: {
            char msg[40];
            s->pos--;
            msg[0] = 0;
            strcpy(msg, "unknown command: `");
            msg[18] = (char)ch;
            msg[19] = '\'';
            msg[20] = 0;
            s->pos++;
            sed_perr(s, msg);
            break;
        }
        }
    }
    if (!s->err && sp > 0) {
        sed_perr(s, "unmatched `{'");
    }
    xfree(stack);
    if (s->err) return -1;
    /* resolve branch targets */
    for (i = 0; i < s->nc; i++) {
        SedCmd *c = &s->c[i];
        if ((c->cmd == 'b' || c->cmd == 't' || c->cmd == 'T') && c->text && *c->text) {
            for (j = 0; j < s->nc; j++)
                if (s->c[j].cmd == ':' && strcmp(s->c[j].text, c->text) == 0) break;
            if (j == s->nc) {
                sh_cmd_error(s->cmd, "can't find label for jump to `%s'", c->text);
                return -4;
            }
            c->jump = j;
        }
    }
    return 0;
}

static void sed_free(Sed *s)
{
    int i;
    for (i = 0; i < s->nc; i++) {
        SedCmd *c = &s->c[i];
        if (c->a1.re) re_free(c->a1.re);
        if (c->a2.re) re_free(c->a2.re);
        if (c->re) re_free(c->re);
        xfree(c->text);
        xfree(c->repl);
        xfree(c->ytab);
    }
    xfree(s->c);
    for (i = 0; i < s->nfw; i++) {
        shf_close(s->files_w[i].f);
        xfree(s->files_w[i].name);
    }
    xfree(s->files_w);
    sb_free(&s->ps);
    sb_free(&s->hs);
    sb_free(&s->app);
    sb_free(&s->next);
}

/* ---- input ---- */

static void sed_fill(Sed *s)
{
    int nl, opened = 0;
    s->have_next = 0;
    for (;;) {
        if (!s->in) {
            opened = 1;
            if (s->fidx >= s->nfiles) return;
            s->inname = s->files[s->fidx++];
            if (strcmp(s->inname, "-") != 0 && sh_is_dir(s->inname)) {
                sh_cmd_error(s->cmd, "couldn't edit %s: not a regular file", s->inname);
                s->ioerr = 2;
                continue;
            }
            if (strcmp(s->inname, "-") == 0) s->in = shf_ref(sh_fd(0) ? sh_fd(0) : shf_null());
            else s->in = shf_open(s->inname, OS_RD);
            if (!s->in) {
                sh_cmd_error(s->cmd, "can't read %s: %s", s->inname, os_errstr());
                s->ioerr = 2;
                continue;
            }
        }
        if (shf_getline(s->in, &s->next, &nl)) {
            s->have_next = 1;
            s->next_nl = nl;
            s->next_first = opened && s->separate;
            return;
        }
        shf_close(s->in);
        s->in = NULL;
        if (s->separate) return;
    }
}

static int sed_read(Sed *s, SBuf *dst)
{
    SBuf t;
    if (!s->have_next) sed_fill(s);
    if (!s->have_next) return 0;
    t = *dst;
    *dst = s->next;
    s->next = t;
    s->cur_nl = s->next_nl;
    s->curname = s->inname;
    if (s->next_first) {
        int i;
        s->lineno = 0;
        for (i = 0; i < s->nc; i++) s->c[i].active = s->c[i].a1.type == SA_ZERO;
    }
    s->lineno++;
    sed_fill(s);
    return 1;
}

static int sed_is_last(Sed *s)
{
    return !s->have_next;
}

/* ---- output ---- */

static void sed_write(Sed *s, const char *p, int len, int nl)
{
    if (s->missing_nl) {
        shf_write(s->out, "\n", 1);
        s->missing_nl = 0;
    }
    shf_write(s->out, p, len);
    if (nl) shf_write(s->out, "\n", 1);
}

static void sed_print_ps(Sed *s, const char *p, int len)
{
    sed_write(s, p, len, s->cur_nl);
    if (!s->cur_nl) s->missing_nl = 1;
}

static void sed_dump_append(Sed *s)
{
    if (s->app.len) {
        sed_write(s, s->app.s, s->app.len, 0);
        sb_clear(&s->app);
    }
}

static void sed_queue_file(Sed *s, const char *name)
{
    ShFile *f = shf_open(name, OS_RD);
    char *buf;
    long n;
    if (!f) return;
    buf = xmalloc(IOBUF);
    while ((n = shf_read(f, buf, IOBUF)) > 0) sb_addn(&s->app, buf, (int)n);
    xfree(buf);
    shf_close(f);
}

static void sed_queue_line(Sed *s, SedCmd *c)
{
    SBuf l;
    int nl;
    if (!c->wf) {
        c->wf = shf_open(c->text, OS_RD);
        if (!c->wf) c->wf = shf_null();
    }
    sb_init(&l);
    if (shf_getline(c->wf, &l, &nl)) {
        sb_addn(&s->app, l.s, l.len);
        sb_addc(&s->app, '\n');
    }
    sb_free(&l);
}

static void sed_l(Sed *s, long width)
{
    SBuf b;
    int i, col = 0;
    sb_init(&b);
    for (i = 0; i < s->ps.len; i++) {
        unsigned char ch = (unsigned char)s->ps.s[i];
        char tmp[8];
        int tl;
        switch (ch) {
        case '\\': strcpy(tmp, "\\\\"); break;
        case 7: strcpy(tmp, "\\a"); break;
        case 8: strcpy(tmp, "\\b"); break;
        case 12: strcpy(tmp, "\\f"); break;
        case '\n': strcpy(tmp, "\\n"); break;
        case '\r': strcpy(tmp, "\\r"); break;
        case '\t': strcpy(tmp, "\\t"); break;
        case 11: strcpy(tmp, "\\v"); break;
        default:
            if (ch < 32 || ch >= 127) {
                tmp[0] = '\\';
                tmp[1] = (char)('0' + (ch >> 6));
                tmp[2] = (char)('0' + ((ch >> 3) & 7));
                tmp[3] = (char)('0' + (ch & 7));
                tmp[4] = 0;
            } else {
                tmp[0] = (char)ch;
                tmp[1] = 0;
            }
        }
        tl = (int)strlen(tmp);
        if (width > 1 && col + tl > width - 1) {
            sb_adds(&b, "\\\n");
            col = 0;
        }
        sb_adds(&b, tmp);
        col += tl;
    }
    sb_adds(&b, "$");
    sed_write(s, b.s, b.len, 1);
    sb_free(&b);
}

static int sed_addr_match(Sed *s, SedAddr *a)
{
    ReMatch m[1];
    switch (a->type) {
    case SA_LINE: return s->lineno == a->n;
    case SA_LAST: return sed_is_last(s);
    case SA_STEP:
        if (a->step <= 0) return s->lineno == a->n;
        return s->lineno >= a->n && (s->lineno - a->n) % a->step == 0;
    case SA_RE: {
        Regex *re = a->re ? a->re : s->last_re;
        if (!re) {
            sh_cmd_error(s->cmd, "no previous regular expression");
            s->quit = 1;
            s->exitcode = 1;
            return 0;
        }
        s->last_re = re;
        return re_exec(re, sb_str(&s->ps), 1, m, 0);
    }
    }
    return 0;
}

static int sed_selected(Sed *s, SedCmd *c)
{
    int r;
    if (c->a1.type == SA_NONE) {
        r = 1;
    } else if (c->a2.type == SA_NONE) {
        r = sed_addr_match(s, &c->a1);
    } else if (c->active) {
        r = 1;
        switch (c->a2.type) {
        case SA_LINE: if (s->lineno >= c->a2.n) c->active = 0; break;
        case SA_PLUS: if (s->lineno >= c->rend) c->active = 0; break;
        case SA_MULT: if (c->a2.n <= 0 || s->lineno % c->a2.n == 0) c->active = 0; break;
        case SA_LAST: if (sed_is_last(s)) c->active = 0; break;
        case SA_RE: if (sed_addr_match(s, &c->a2)) c->active = 0; break;
        }
    } else if (sed_addr_match(s, &c->a1)) {
        r = 1;
        switch (c->a2.type) {
        case SA_LINE: if (c->a2.n > s->lineno) c->active = 1; break;
        case SA_PLUS: if (c->a2.n > 0) { c->active = 1; c->rend = s->lineno + c->a2.n; } break;
        case SA_MULT: if (c->a2.n > 0 && s->lineno % c->a2.n != 0) c->active = 1; break;
        case SA_LAST: if (!sed_is_last(s)) c->active = 1; break;
        case SA_RE: c->active = 1; break;
        }
    } else {
        r = 0;
    }
    return c->neg ? !r : r;
}

static void case_add(SBuf *b, int ch, int *one, int mode)
{
    if (*one == 'u') {
        ch = toupper(ch);
        *one = 0;
    } else if (*one == 'l') {
        ch = tolower(ch);
        *one = 0;
    } else if (mode == 'U') {
        ch = toupper(ch);
    } else if (mode == 'L') {
        ch = tolower(ch);
    }
    sb_addc(b, ch);
}

static void sed_repl(SBuf *out, const char *rep, const char *str, ReMatch *m, int nm)
{
    int one = 0, mode = 0, i;
    for (; *rep; rep++) {
        int ch = (unsigned char)*rep;
        if (ch == '&') {
            for (i = m[0].so; i < m[0].eo; i++) case_add(out, (unsigned char)str[i], &one, mode);
            continue;
        }
        if (ch == '\\' && rep[1]) {
            ch = (unsigned char)*++rep;
            if (ch >= '0' && ch <= '9') {
                int g = ch - '0';
                if (g < nm && m[g].so >= 0)
                    for (i = m[g].so; i < m[g].eo; i++) case_add(out, (unsigned char)str[i], &one, mode);
                continue;
            }
            switch (ch) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case 'a': ch = 7; break;
            case 'f': ch = 12; break;
            case 'v': ch = 11; break;
            case 'U': case 'L': mode = ch; continue;
            case 'E': mode = 0; one = 0; continue;
            case 'u': case 'l': one = ch; continue;
            default: break;
            }
        }
        case_add(out, ch, &one, mode);
    }
}

static int sed_subst(Sed *s, SedCmd *c)
{
    Regex *re = c->re_empty ? s->last_re : c->re;
    ReMatch m[10];
    SBuf out;
    const char *str;
    int len, pos = 0, copied = 0, count = 0, did = 0, lastend = -1, i;
    if (!re) {
        sh_cmd_error(s->cmd, "no previous regular expression");
        s->quit = 1;
        s->exitcode = 1;
        return 0;
    }
    s->last_re = re;
    str = sb_str(&s->ps);
    len = s->ps.len;
    sb_init(&out);
    sb_addn(&out, "", 0);
    while (pos <= len) {
        int so, eo;
        for (i = 0; i < 10; i++) m[i].so = m[i].eo = -1;
        if (!re_exec(re, str + pos, 10, m, pos > 0 ? RE_NOTBOL : 0)) break;
        for (i = 0; i < 10; i++)
            if (m[i].so >= 0) {
                m[i].so += pos;
                m[i].eo += pos;
            }
        so = m[0].so;
        eo = m[0].eo;
        if (so == eo && so == lastend) {
            /* no empty match right after a previous match */
            if (so >= len) break;
            pos = so + 1;
            continue;
        }
        count++;
        if (count >= c->nth) {
            sb_addn(&out, str + copied, so - copied);
            sed_repl(&out, c->repl, str, m, 10);
            copied = eo;
            did = 1;
            if (!c->gflag) break;
        }
        lastend = eo;
        if (eo == so) {
            if (so >= len) break;
            pos = so + 1;
        } else {
            pos = eo;
        }
    }
    if (did) {
        sb_addn(&out, str + copied, len - copied);
        sb_free(&s->ps);
        s->ps = out;
        s->tflag = 1;
        if (c->pflag) sed_print_ps(s, s->ps.s, s->ps.len);
        if (c->wf) {
            shf_write(c->wf, s->ps.s, s->ps.len);
            shf_write(c->wf, "\n", 1);
        }
    } else {
        sb_free(&out);
    }
    return did;
}

/* run the script over all input; returns exit status */
static int sed_run(Sed *s)
{
    int i;
    for (i = 0; i < s->nc; i++) s->c[i].active = s->c[i].a1.type == SA_ZERO;
    while (!s->quit && sed_read(s, &s->ps)) {
        int pc = 0, autoprint = !s->nflag;
        s->tflag = 0;
again:
        while (pc < s->nc && !s->quit) {
            SedCmd *c = &s->c[pc];
            if (c->cmd == '}' || c->cmd == ':') {
                pc++;
                continue;
            }
            if (!sed_selected(s, c)) {
                pc = c->cmd == '{' ? c->jump + 1 : pc + 1;
                continue;
            }
            pc++;
            switch (c->cmd) {
            case '{': break;
            case '=': {
                SBuf b;
                sb_init(&b);
                sb_printf(&b, "%ld", s->lineno);
                sed_write(s, b.s, b.len, 1);
                sb_free(&b);
                break;
            }
            case 'a':
                sb_adds(&s->app, c->text);
                sb_addc(&s->app, '\n');
                break;
            case 'i':
                sed_write(s, c->text, (int)strlen(c->text), 1);
                break;
            case 'c':
                if (c->neg || c->a2.type == SA_NONE || !c->active)
                    sed_write(s, c->text, (int)strlen(c->text), 1);
                autoprint = 0;
                pc = s->nc;
                break;
            case 'd':
                autoprint = 0;
                pc = s->nc;
                break;
            case 'D': {
                char *nlp = s->ps.s ? memchr(s->ps.s, '\n', s->ps.len) : NULL;
                if (!nlp) {
                    autoprint = 0;
                    pc = s->nc;
                    break;
                }
                {
                    int cut = (int)(nlp - s->ps.s) + 1;
                    memmove(s->ps.s, s->ps.s + cut, s->ps.len - cut + 1);
                    s->ps.len -= cut;
                }
                sed_dump_append(s);
                pc = 0;
                s->tflag = 0;
                goto again;
            }
            case 'g':
                sb_clear(&s->ps);
                sb_addn(&s->ps, s->hs.s, s->hs.len);
                break;
            case 'G':
                sb_addc(&s->ps, '\n');
                sb_addn(&s->ps, s->hs.s, s->hs.len);
                break;
            case 'h':
                sb_clear(&s->hs);
                sb_addn(&s->hs, s->ps.s, s->ps.len);
                break;
            case 'H':
                sb_addc(&s->hs, '\n');
                sb_addn(&s->hs, s->ps.s, s->ps.len);
                break;
            case 'x': {
                SBuf t = s->ps;
                s->ps = s->hs;
                s->hs = t;
                if (!s->ps.s) sb_addn(&s->ps, "", 0);
                break;
            }
            case 'l':
                sed_l(s, c->ival);
                break;
            case 'n':
                if (sed_is_last(s) && (s->fidx >= s->nfiles || s->separate)) {
                    s->quit = 1;
                    break;
                }
                if (!s->nflag) sed_print_ps(s, s->ps.s, s->ps.len);
                sed_dump_append(s);
                if (!sed_read(s, &s->ps)) {
                    autoprint = 0;
                    s->quit = 1;
                }
                break;
            case 'N':
                if (sed_is_last(s) && (s->fidx >= s->nfiles || s->separate)) {
                    s->quit = 1;
                    break;
                }
                sed_dump_append(s);
                {
                    SBuf t;
                    sb_init(&t);
                    if (sed_read(s, &t)) {
                        sb_addc(&s->ps, '\n');
                        sb_addn(&s->ps, t.s, t.len);
                    } else {
                        s->quit = 1;
                    }
                    sb_free(&t);
                }
                break;
            case 'p':
                sed_print_ps(s, sb_str(&s->ps), s->ps.len);
                break;
            case 'P': {
                char *nlp = s->ps.s ? memchr(s->ps.s, '\n', s->ps.len) : NULL;
                sed_write(s, sb_str(&s->ps), nlp ? (int)(nlp - s->ps.s) : s->ps.len, 1);
                break;
            }
            case 'q':
                s->exitcode = (int)c->ival;
                s->quit = 1;
                break;
            case 'Q':
                s->exitcode = (int)c->ival;
                s->quit = 1;
                autoprint = 0;
                break;
            case 'r':
                sed_queue_file(s, c->text);
                break;
            case 'R':
                sed_queue_line(s, c);
                break;
            case 'w':
                if (c->wf) {
                    shf_write(c->wf, sb_str(&s->ps), s->ps.len);
                    shf_write(c->wf, "\n", 1);
                }
                break;
            case 'W':
                if (c->wf) {
                    char *nlp = s->ps.s ? memchr(s->ps.s, '\n', s->ps.len) : NULL;
                    shf_write(c->wf, sb_str(&s->ps), nlp ? (int)(nlp - s->ps.s) : s->ps.len);
                    shf_write(c->wf, "\n", 1);
                }
                break;
            case 's':
                sed_subst(s, c);
                break;
            case 't':
                if (s->tflag) {
                    s->tflag = 0;
                    pc = c->jump < 0 ? s->nc : c->jump;
                }
                break;
            case 'T':
                if (!s->tflag) pc = c->jump < 0 ? s->nc : c->jump;
                else s->tflag = 0;
                break;
            case 'b':
                pc = c->jump < 0 ? s->nc : c->jump;
                break;
            case 'y': {
                int k;
                for (k = 0; k < s->ps.len; k++) s->ps.s[k] = (char)c->ytab[(unsigned char)s->ps.s[k]];
                break;
            }
            case 'z':
                sb_clear(&s->ps);
                break;
            case 'F':
                sed_write(s, s->curname && strcmp(s->curname, "-") != 0 ? s->curname : "-",
                          (int)strlen(s->curname && strcmp(s->curname, "-") != 0 ? s->curname : "-"), 1);
                break;
            default:
                break;
            }
        }
        if (autoprint) sed_print_ps(s, sb_str(&s->ps), s->ps.len);
        sed_dump_append(s);
        if ((s->lineno & 63) == 0 && u_break()) {
            s->exitcode = 130;
            break;
        }
    }
    if (s->in) {
        shf_close(s->in);
        s->in = NULL;
    }
    return s->exitcode;
}

static int sed_inplace(Sed *s, const char *file, const char *suffix)
{
    static long tmpno;
    OsDirEnt e;
    char *dir, *tmpname, *tmp, *nf;
    ShFile *out;
    int rc;
    if (sh_stat(file, &e) != 0) {
        sh_cmd_error(s->cmd, "can't read %s: %s", file, os_errstr());
        return 2;
    }
    if (e.type != OS_T_FILE) {
        sh_cmd_error(s->cmd, "couldn't edit %s: not a regular file", file);
        return 4;
    }
    dir = path_dirname(file);
    tmpname = str_printf("sed%ld_%ld.tmp", os_pid(), ++tmpno);
    tmp = strcmp(dir, ".") == 0 && !path_has_dir(file) ? xstrdup(tmpname) : path_join(dir, tmpname);
    xfree(dir);
    xfree(tmpname);
    out = shf_open(tmp, OS_WR);
    if (!out) {
        sh_cmd_error(s->cmd, "couldn't open temporary file %s: %s", tmp, os_errstr());
        xfree(tmp);
        return 4;
    }
    s->out = out;
    s->missing_nl = 0;
    s->files = (char **)&file;
    s->nfiles = 1;
    s->fidx = 0;
    s->have_next = 0;
    s->lineno = 0;
    rc = sed_run(s);
    shf_close(out);
    s->out = NULL;
    if (suffix && *suffix) {
        char *bak;
        if (strchr(suffix, '*')) {
            SBuf b;
            const char *p;
            const char *base = path_basename(file);
            char *d = path_dirname(file);
            sb_init(&b);
            for (p = suffix; *p; p++) {
                if (*p == '*') sb_adds(&b, base);
                else sb_addc(&b, *p);
            }
            if (!strchr(suffix, '/') && !strchr(suffix, ':') && path_has_dir(file)) {
                bak = path_join(d, sb_str(&b));
                sb_free(&b);
            } else {
                bak = sb_take(&b);
            }
            xfree(d);
        } else {
            bak = str_cat(file, suffix);
        }
        sh_remove(bak);
        if (sh_rename(file, bak) != 0) {
            sh_cmd_error(s->cmd, "cannot rename %s: %s", file, os_errstr());
            xfree(bak);
            sh_remove(tmp);
            xfree(tmp);
            return 4;
        }
        xfree(bak);
    } else {
        sh_remove(file);
    }
    if (sh_rename(tmp, file) != 0) {
        if (u_copy_file(s->cmd, tmp, file, 0) != 0) {
            xfree(tmp);
            return 4;
        }
        sh_remove(tmp);
    }
    nf = path_native(file);
    os_setprot(nf, e.prot);
    xfree(nf);
    xfree(tmp);
    return rc;
}

int util_sed(int argc, char **argv)
{
    static const char *const lo[] = { "quiet", "silent", "expression=", "file=", "regexp-extended", "in-place?",
                                      "separate", "posix", "debug", "null-data", "unbuffered", "line-length=",
                                      "sandbox", "follow-symlinks", "version", NULL };
    UGetopt g;
    Sed s;
    SBuf script;
    int c, i, rc = 0, havescript = 0;
    const char *suffix = NULL;
    memset(&s, 0, sizeof(s));
    s.cmd = argv[0];
    sb_init(&script);
    u_break_reset();
    ug_init(&g, argc, argv, "ne:f:Eri::sul:z", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'n': case UG_LONG + 0: case UG_LONG + 1: s.nflag = 1; break;
        case 'e': case UG_LONG + 2:
            if (havescript) sb_addc(&script, '\n');
            sb_adds(&script, g.arg);
            havescript = 1;
            break;
        case 'f': case UG_LONG + 3: {
            ShFile *f = u_open_in(argv[0], g.arg);
            char *buf;
            long n;
            if (!f) {
                sb_free(&script);
                ug_free(&g);
                return 1;
            }
            if (havescript) sb_addc(&script, '\n');
            buf = xmalloc(IOBUF);
            while ((n = shf_read(f, buf, IOBUF)) > 0) sb_addn(&script, buf, (int)n);
            xfree(buf);
            shf_close(f);
            if (script.len && script.s[script.len - 1] == '\n') script.s[--script.len] = 0;
            havescript = 1;
            break;
        }
        case 'E': case 'r': case UG_LONG + 4: s.ere = 1; break;
        case 'i': case UG_LONG + 5: s.inplace = 1; s.separate = 1; suffix = g.arg; break;
        case 's': case UG_LONG + 6: s.separate = 1; break;
        case 'u': case 'l': case 'z': case UG_LONG + 7: case UG_LONG + 8: case UG_LONG + 9: case UG_LONG + 10:
        case UG_LONG + 11: case UG_LONG + 12: case UG_LONG + 13: break;
        case UG_LONG + 14: sh_puts(1, "sed (AmiBash) 4.9\n"); sb_free(&script); ug_free(&g); return 0;
        case UG_LONG + 999:
            sb_free(&script);
            ug_free(&g);
            sh_puts(1, "Usage: sed [-nEr] [-i[SUFFIX]] [-s] {SCRIPT | -e SCRIPT... | -f FILE...} [FILE]...\n");
            return 0;
        default:
            sb_free(&script);
            ug_free(&g);
            sh_puts(2, "Usage: sed [-nEr] [-i[SUFFIX]] [-s] {SCRIPT | -e SCRIPT... | -f FILE...} [FILE]...\n");
            return 1;
        }
    }
    i = 0;
    if (!havescript) {
        if (g.nops == 0) {
            sh_puts(2, "Usage: sed [-nEr] [-i[SUFFIX]] [-s] {SCRIPT | -e SCRIPT... | -f FILE...} [FILE]...\n");
            sb_free(&script);
            ug_free(&g);
            return 1;
        }
        sb_adds(&script, g.ops[0]);
        i = 1;
    }
    s.src = sb_str(&script);
    s.len = script.len;
    if (s.len >= 2 && s.src[0] == '#' && s.src[1] == 'n' && (s.len == 2 || s.src[2] == '\n')) s.nflag = 1;
    sb_init(&s.ps);
    sb_init(&s.hs);
    sb_init(&s.app);
    sb_init(&s.next);
    sb_addn(&s.ps, "", 0);
    sb_addn(&s.hs, "", 0);
    if ((rc = sed_parse(&s)) != 0) {
        sed_free(&s);
        sb_free(&script);
        ug_free(&g);
        return rc == -4 ? 4 : 1;   /* GNU: missing label is a panic (4) */
    }
    if (s.inplace) {
        if (i >= g.nops) {
            sh_cmd_error(argv[0], "no input files");
            rc = 1;
        }
        for (; i < g.nops && !s.quit; i++) {
            int r = sed_inplace(&s, g.ops[i], suffix);
            if (r > rc) rc = r;
        }
        if (s.quit && s.exitcode) rc = s.exitcode;
    } else {
        char *dash[1];
        dash[0] = "-";
        s.out = shf_ref(sh_fd(1));
        if (i < g.nops) {
            s.files = g.ops + i;
            s.nfiles = g.nops - i;
        } else {
            s.files = dash;
            s.nfiles = 1;
        }
        rc = sed_run(&s);
        if (s.ioerr && !rc) rc = s.ioerr;
        shf_close(s.out);
    }
    sed_free(&s);
    sb_free(&script);
    ug_free(&g);
    return rc;
}
