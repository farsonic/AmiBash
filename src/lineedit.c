/*
 * lineedit.c - interactive line editing (readline-like), history and
 * tab completion.
 *
 * Works on the Amiga console (raw mode, CSI 0x9B sequences) and on ANSI
 * terminals (ESC [ sequences).
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

/* ------------------------------------------------------------------ */
/* history                                                              */

static char **hist;
static int hcount, hcap, hmax = 500, hbase = 1;

void hist_set_max(int n)
{
    if (n < 0) n = 0;
    hmax = n;
    while (hcount > hmax) hist_delete(0);
}

void hist_add(const char *line)
{
    const char *ctl = var_get("HISTCONTROL");
    if (!*line || hmax == 0) return;
    if (ctl && (strstr(ctl, "ignorespace") || strstr(ctl, "ignoreboth")) && line[0] == ' ') return;
    if (hcount && strcmp(hist[hcount - 1], line) == 0) return;
    if (hcount >= hcap) {
        hcap = hcap ? hcap * 2 : 64;
        hist = xrealloc(hist, hcap * sizeof(char *));
    }
    hist[hcount++] = xstrdup(line);
    while (hcount > hmax) hist_delete(0);
}

int hist_count(void) { return hcount; }
int hist_base(void) { return hbase; }
const char *hist_get(int i) { return i >= 0 && i < hcount ? hist[i] : ""; }

void hist_delete(int i)
{
    if (i < 0 || i >= hcount) return;
    xfree(hist[i]);
    memmove(hist + i, hist + i + 1, (hcount - i - 1) * sizeof(char *));
    hcount--;
    if (i == 0) hbase++;
}

void hist_clear(void)
{
    while (hcount) hist_delete(hcount - 1);
    hbase = 1;
}

void hist_load(const char *file)
{
    ShFile *f = shf_open(file, OS_RD);
    SBuf l;
    if (!f) return;
    sb_init(&l);
    while (shf_getline(f, &l, NULL)) hist_add(sb_str(&l));
    sb_free(&l);
    shf_close(f);
}

void hist_save(const char *file)
{
    ShFile *f = shf_open(file, OS_WR);
    int i, start = 0;
    const char *fs = var_get("HISTFILESIZE");
    if (!f) return;
    if (fs && atoi(fs) > 0 && hcount > atoi(fs)) start = hcount - atoi(fs);
    for (i = start; i < hcount; i++) {
        shf_write(f, hist[i], (long)strlen(hist[i]));
        shf_write(f, "\n", 1);
    }
    shf_close(f);
}

/* split a history line into words (simple, quote-aware) */
static void hist_words(const char *line, StrList *out)
{
    const char *s = line;
    while (*s) {
        const char *e;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (strchr(";&|<>()", *s)) {
            e = s + 1;
            while (*e && strchr(";&|<>", *e) && *e == *s) e++;
        } else {
            e = scan_word_end(s);
            if (!e || e == s) e = s + strlen(s);
        }
        sl_add(out, xstrndup(s, e - s));
        s = e;
    }
}

char *hist_expand(const char *line, int *err, int *changed)
{
    SBuf b;
    const char *s = line;
    int sq = 0;
    *err = 0;
    *changed = 0;
    if (!strchr(line, '!') && line[0] != '^') return NULL;
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (line[0] == '^') {
        /* ^old^new^ */
        const char *o = line + 1, *n = strchr(o, '^');
        char *old, *nw, *pos;
        const char *last = hcount ? hist[hcount - 1] : NULL;
        if (!n || !last) {
            *err = 1;
            sb_free(&b);
            return NULL;
        }
        old = xstrndup(o, n - o);
        {
            const char *e = strchr(n + 1, '^');
            nw = e ? xstrndup(n + 1, e - n - 1) : xstrdup(n + 1);
        }
        pos = strstr(last, old);
        if (!pos) {
            sh_error(":s%s: substitution failed", old);
            xfree(old);
            xfree(nw);
            *err = 1;
            sb_free(&b);
            return NULL;
        }
        sb_addn(&b, last, (int)(pos - last));
        sb_adds(&b, nw);
        sb_adds(&b, pos + strlen(old));
        xfree(old);
        xfree(nw);
        *changed = 1;
        return sb_take(&b);
    }
    while (*s) {
        if (*s == '\'' ) sq = !sq;
        if (*s == '\\' && s[1]) {
            sb_addc(&b, *s++);
            sb_addc(&b, *s++);
            continue;
        }
        /* "${!name}" is indirection, never a history event (as in bash) */
        if (*s != '!' || sq || !s[1] || strchr(" \t\n=(\"", s[1]) ||
            (s >= line + 2 && s[-1] == '{' && s[-2] == '$')) {
            sb_addc(&b, *s++);
            continue;
        }
        {
            const char *ev = NULL;
            const char *p = s + 1;
            int idx = -1;
            if (*p == '!') {
                idx = hcount - 1;
                p++;
            } else if (isdigit((unsigned char)*p) || (*p == '-' && isdigit((unsigned char)p[1]))) {
                int neg = *p == '-', n;
                if (neg) p++;
                n = atoi(p);
                while (isdigit((unsigned char)*p)) p++;
                idx = neg ? hcount - n : n - hbase;
            } else if (*p == '$' || *p == '^' || *p == '*' || *p == ':') {
                idx = hcount - 1;
            } else if (*p == '?') {
                const char *e = strchr(p + 1, '?');
                char *pat = e ? xstrndup(p + 1, e - p - 1) : xstrdup(p + 1);
                int i;
                for (i = hcount - 1; i >= 0; i--)
                    if (strstr(hist[i], pat)) break;
                idx = i;
                xfree(pat);
                p = e ? e + 1 : p + strlen(p);
            } else {
                const char *e = p;
                int i;
                while (*e && !strchr(" \t:;&|<>()", *e)) e++;
                for (i = hcount - 1; i >= 0; i--)
                    if (strncmp(hist[i], p, e - p) == 0) break;
                idx = i;
                if (idx < 0) {
                    sh_error("!%.*s: event not found", (int)(e - p), p);
                    *err = 1;
                    sb_free(&b);
                    return NULL;
                }
                p = e;
            }
            if (idx < 0 || idx >= hcount) {
                sh_error("%.*s: event not found", (int)(p - s), s);
                *err = 1;
                sb_free(&b);
                return NULL;
            }
            ev = hist[idx];
            /* word designators */
            if (*p == ':' || *p == '$' || *p == '^' || *p == '*') {
                StrList w;
                int from, to;
                if (*p == ':') p++;
                sl_init(&w);
                hist_words(ev, &w);
                if (*p == '$') from = to = w.n - 1, p++;
                else if (*p == '^') from = to = 1, p++;
                else if (*p == '*') from = 1, to = w.n - 1, p++;
                else if (isdigit((unsigned char)*p)) {
                    from = to = atoi(p);
                    while (isdigit((unsigned char)*p)) p++;
                    if (*p == '-') {
                        p++;
                        if (isdigit((unsigned char)*p)) {
                            to = atoi(p);
                            while (isdigit((unsigned char)*p)) p++;
                        } else if (*p == '$') {
                            to = w.n - 1;
                            p++;
                        } else {
                            to = w.n - 2;
                        }
                    } else if (*p == '*') {
                        to = w.n - 1;
                        p++;
                    }
                } else {
                    from = 0;
                    to = w.n - 1;
                }
                {
                    int i;
                    for (i = from; i <= to && i < w.n; i++) {
                        if (i > from) sb_addc(&b, ' ');
                        if (i >= 0) sb_adds(&b, w.v[i]);
                    }
                }
                sl_free(&w);
            } else {
                sb_adds(&b, ev);
            }
            *changed = 1;
            s = p;
        }
    }
    if (!*changed) {
        sb_free(&b);
        return NULL;
    }
    return sb_take(&b);
}

/* ------------------------------------------------------------------ */
/* completion                                                           */

static int cmd_position(const char *line, int start)
{
    int i = start - 1;
    while (i >= 0 && (line[i] == ' ' || line[i] == '\t')) i--;
    if (i < 0) return 1;
    if (strchr(";|&(`", line[i])) return 1;
    {
        /* preceding keyword */
        int e = i + 1;
        while (i >= 0 && !strchr(" \t;|&(", line[i])) i--;
        {
            char w[16];
            int n = e - i - 1;
            if (n > 0 && n < 15) {
                memcpy(w, line + i + 1, n);
                w[n] = 0;
                if (!strcmp(w, "then") || !strcmp(w, "do") || !strcmp(w, "else") ||
                    !strcmp(w, "if") || !strcmp(w, "while") || !strcmp(w, "until") ||
                    !strcmp(w, "sudo") || !strcmp(w, "time") || !strcmp(w, "command") ||
                    !strcmp(w, "type") || !strcmp(w, "which") || !strcmp(w, "!"))
                    return 1;
            }
        }
    }
    return 0;
}

static int has_prefix(const char *s, const char *p, int nocase)
{
    size_t n = strlen(p);
    return nocase ? str_ncasecmp(s, p, n) == 0 : strncmp(s, p, n) == 0;
}

static void add_unique(StrList *out, const char *s)
{
    int i;
    for (i = 0; i < out->n; i++)
        if (strcmp(out->v[i], s) == 0) return;
    sl_adddup(out, s);
}

typedef struct ResCtx { StrList *out; const char *pre; } ResCtx;

static int res_cb(const char *name, void *ud)
{
    ResCtx *c = ud;
    if (has_prefix(name, c->pre, 1)) add_unique(c->out, name);
    return 0;
}

typedef struct VolCtx { StrList *out; const char *pre; int unix_style; } VolCtx;

static int vol_cb(const char *name, int isvol, void *ud)
{
    VolCtx *c = ud;
    (void)isvol;
    if (has_prefix(name, c->pre, 1)) {
        char *s = c->unix_style ? str_printf("/%s/", name) : str_printf("%s:", name);
        add_unique(c->out, s);
        xfree(s);
    }
    return 0;
}

#ifdef AMIGA
#define NOCASE 1
#else
#define NOCASE 0
#endif

void complete_word(const char *line, int start, int end, StrList *out, int *isfile)
{
    char *word = xstrndup(line + start, end - start);
    int i;
    *isfile = 0;
    /* variables */
    if (word[0] == '$') {
        StrList names;
        const char *pre = word[1] == '{' ? word + 2 : word + 1;
        sl_init(&names);
        var_names(&names, pre);
        for (i = 0; i < names.n; i++) {
            char *s = word[1] == '{' ? str_printf("${%s}", names.v[i]) : str_printf("$%s", names.v[i]);
            sl_add(out, s);
        }
        sl_free(&names);
        xfree(word);
        return;
    }
    if (cmd_position(line, start) && !path_has_dir(word) && word[0] != '~' && word[0] != '.') {
        StrList dirs;
        const char *path = var_get("PATH");
        alias_names(out, word);
        func_names(out, word);
        builtin_names(out, word);
        {
            static const char *const kw[] = {"if", "then", "else", "elif", "fi", "case", "esac", "for",
                "while", "until", "do", "done", "function", "select", "time", NULL};
            int k;
            for (k = 0; kw[k]; k++)
                if (has_prefix(kw[k], word, 0)) add_unique(out, kw[k]);
        }
        {
            ResCtx rc;
            rc.out = out;
            rc.pre = word;
            os_list_resident(res_cb, &rc);
        }
        sl_init(&dirs);
        if (path) {
            const char *s = path, *e;
            char sep = ':';
#ifdef AMIGA
            if (strchr(path, ';')) sep = ';';
#endif
            for (;;) {
                e = strchr(s, sep);
                if (!e) e = s + strlen(s);
                sl_add(&dirs, e == s ? xstrdup(".") : xstrndup(s, e - s));
                if (!*e) break;
                s = e + 1;
            }
        }
        for (i = 0; i < dirs.n; i++) {
            void *d;
            OsDirEnt e;
            if (strcmp(dirs.v[i], ".") == 0) continue;
            d = sh_opendir(dirs.v[i]);
            if (!d) continue;
            while (os_readdir(d, &e)) {
                if (e.type == OS_T_DIR) continue;
                if (str_endswith(e.name, ".info")) continue;
                if (has_prefix(e.name, word, NOCASE) && (e.prot & (OS_P_EXEC | OS_P_SCRIPT)))
                    add_unique(out, e.name);
            }
            os_closedir(d);
        }
        sl_free(&dirs);
#ifdef AMIGA
        if (*word) {
            VolCtx vc;
            vc.out = out;
            vc.pre = word;
            vc.unix_style = 0;
            os_list_volumes(vol_cb, &vc);
        }
#endif
        sl_sort(out, 0);
        if (out->n) {
            xfree(word);
            return;
        }
    }
    /* file names */
    {
        int k = (int)strlen(word);
        char *dir, *base, *ldir;
        void *d;
        OsDirEnt e;
        while (k > 0 && word[k - 1] != '/' && word[k - 1] != ':') k--;
        dir = xstrndup(word, k);
        base = word + k;
        *isfile = 1;
        if (k == 0) {
            ldir = xstrdup(".");
        } else if (dir[0] == '~') {
            ldir = expand_str(dir, X_ASSIGN);
            if (!ldir) ldir = xstrdup(dir);
        } else {
            ldir = xstrdup(dir);
        }
#ifdef AMIGA
        if (k == 0 && *base) {
            VolCtx vc;
            vc.out = out;
            vc.pre = base;
            vc.unix_style = 0;
            os_list_volumes(vol_cb, &vc);
        }
#endif
        d = sh_opendir(ldir);
        if (d) {
            while (os_readdir(d, &e)) {
                if (e.name[0] == '.' && base[0] != '.') continue;
                if (!has_prefix(e.name, base, NOCASE)) continue;
                {
                    char *full;
                    if (path_is_vroot(ldir) || (strcmp(dir, "/") == 0)) {
                        full = str_printf("/%s/", e.name);
                    } else {
                        full = str_printf("%s%s%s", dir, e.name,
                                          (e.type == OS_T_DIR || e.type == OS_T_VOL) ? "/" : "");
                    }
                    add_unique(out, full);
                    xfree(full);
                }
            }
            os_closedir(d);
        }
        xfree(dir);
        xfree(ldir);
        sl_sort(out, NOCASE);
    }
    xfree(word);
}

/* ------------------------------------------------------------------ */
/* terminal output helpers                                              */

static OsHandle tin, tout;
static int cols = 80;

static SBuf obuf;

static void out_s(const char *s)
{
    sb_adds(&obuf, s);
}

static void out_n(const char *s, int n)
{
    sb_addn(&obuf, s, n);
}

static void out_flush(void)
{
    if (obuf.len) os_write(tout, obuf.s, obuf.len);
    sb_clear(&obuf);
}

static void out_csi(int n, char c)
{
    char b[16];
    if (n <= 0) return;
    sprintf(b, "\033[%d%c", n, c);
    out_s(b);
}

/* visible width of prompt (skip \001..\002 and ESC sequences) */
static int visible_width(const char *p, int *lastline)
{
    int w = 0, inv = 0;
    *lastline = 0;
    for (; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == 1) {
            inv = 1;
            continue;
        }
        if (c == 2) {
            inv = 0;
            continue;
        }
        if (inv) continue;
        if (c == 27 && p[1] == '[') {
            p += 2;
            while (*p && !isalpha((unsigned char)*p)) p++;
            if (!*p) break;
            continue;
        }
        if (c == 0x9b) {
            p++;
            while (*p && !isalpha((unsigned char)*p)) p++;
            if (!*p) break;
            continue;
        }
        if (c == '\n') {
            w = 0;
            continue;
        }
        if (c == '\r') {
            w = 0;
            continue;
        }
#ifndef AMIGA
        if ((c & 0xC0) == 0x80) continue;    /* UTF-8 continuation */
#endif
        if (c >= 32) w++;
    }
    return w;
}

static void out_prompt(const char *p)
{
    for (; *p; p++)
        if (*p != 1 && *p != 2) out_n(p, 1);
}

/* ------------------------------------------------------------------ */
/* editor state                                                         */

typedef struct Ed {
    SBuf buf;
    int pos;
    const char *prompt;
    int pw;                 /* prompt width                             */
    int oldrow;             /* cursor row relative to prompt start      */
    int hidx;               /* history browsing index                   */
    char *saved;            /* line being edited before browsing        */
} Ed;

static char *killbuf;
int le_cancelled;      /* ^C pressed: caller discards pending input */

static void refresh(Ed *e)
{
    int len = e->buf.len;
    int total = e->pw + len;
    int rows, crow, ccol;
    int lastline;
    /* go to start of prompt */
    if (e->oldrow > 0) out_csi(e->oldrow, 'A');
    out_s("\r");
    out_s("\033[J");
    out_prompt(e->prompt);
    (void)lastline;
    out_n(e->buf.s ? e->buf.s : "", len);
    rows = total / cols;
    /* if exactly at the right margin, force the wrap */
    if (total > 0 && total % cols == 0) {
        out_s("\n");
    }
    crow = (e->pw + e->pos) / cols;
    ccol = (e->pw + e->pos) % cols;
    if (rows - crow > 0) out_csi(rows - crow, 'A');
    out_s("\r");
    out_csi(ccol, 'C');
    e->oldrow = crow;
    out_flush();
}

static void ed_insert(Ed *e, const char *s, int n)
{
    SBuf nb;
    sb_init(&nb);
    sb_addn(&nb, e->buf.s ? e->buf.s : "", e->pos);
    sb_addn(&nb, s, n);
    sb_addn(&nb, e->buf.s ? e->buf.s + e->pos : "", e->buf.len - e->pos);
    sb_free(&e->buf);
    e->buf = nb;
    e->pos += n;
}

static void ed_delete(Ed *e, int from, int to)
{
    if (from < 0) from = 0;
    if (to > e->buf.len) to = e->buf.len;
    if (from >= to) return;
    memmove(e->buf.s + from, e->buf.s + to, e->buf.len - to + 1);
    e->buf.len -= to - from;
    if (e->pos > to) e->pos -= to - from;
    else if (e->pos > from) e->pos = from;
}

static void ed_set(Ed *e, const char *s)
{
    sb_clear(&e->buf);
    sb_adds(&e->buf, s);
    e->pos = e->buf.len;
}

static void set_kill(const char *s, int n)
{
    xfree(killbuf);
    killbuf = xstrndup(s, n);
}

static int word_left(Ed *e)
{
    int p = e->pos;
    while (p > 0 && !isalnum((unsigned char)e->buf.s[p - 1])) p--;
    while (p > 0 && isalnum((unsigned char)e->buf.s[p - 1])) p--;
    return p;
}

static int word_right(Ed *e)
{
    int p = e->pos;
    while (p < e->buf.len && !isalnum((unsigned char)e->buf.s[p])) p++;
    while (p < e->buf.len && isalnum((unsigned char)e->buf.s[p])) p++;
    return p;
}

static int getkey(void)
{
    unsigned char c;
    int tries = 0;
    for (;;) {
        long r;
        /* block in WaitForChar(): on AROS a raw-mode console Read() can
         * return 0 instead of waiting */
        while (!os_waitchar(tin, 5000000))
            ;
        r = os_read(tin, &c, 1);
        if (r == 1) return c;
        /* a console in raw mode has no end-of-file: a 0/-1 read is a
         * transient condition (seen on AROS), so retry a little */
        if (++tries > 20 || !os_isatty(tin)) {
            if (getenv("AMIBASH_DEBUG")) sh_printf(2, "[getkey: read=%ld]\n", r);
            return -1;
        }
        os_sleep_ms(20);
    }
}

/* key codes beyond bytes */
enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_DEL, K_WLEFT, K_WRIGHT,
       K_HELP, K_IGNORE, K_SUP, K_SDOWN };

static int read_seq(int intro)
{
    /* intro: 0x9B (Amiga CSI) or '[' / 'O' after ESC */
    char seq[16];
    int n = 0, c;
    for (;;) {
        if (!os_waitchar(tin, 300000)) break;
        c = getkey();
        if (c < 0) break;
        if (n < 15) seq[n++] = (char)c;
        if ((c >= 0x40 && c <= 0x7E) && !(n == 1 && c == '[')) break;
    }
    seq[n] = 0;
    if (intro == 'O') {
        switch (seq[0]) {
        case 'H': return K_HOME;
        case 'F': return K_END;
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        }
        return K_IGNORE;
    }
    if (n == 1) {
        switch (seq[0]) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case 'T': return K_SUP;      /* Amiga shift-up */
        case 'S': return K_SDOWN;    /* Amiga shift-down */
        }
        return K_IGNORE;
    }
    if (strcmp(seq, " A") == 0) return K_HOME;      /* Amiga shift-left */
    if (strcmp(seq, " @") == 0) return K_END;       /* Amiga shift-right */
    if (strcmp(seq, "?~") == 0) return K_HELP;
    if (strcmp(seq, "1~") == 0 || strcmp(seq, "7~") == 0) return K_HOME;
    if (strcmp(seq, "4~") == 0 || strcmp(seq, "8~") == 0) return K_END;
    if (strcmp(seq, "3~") == 0) return K_DEL;
    if (strcmp(seq, "1;5D") == 0 || strcmp(seq, "1;3D") == 0) return K_WLEFT;
    if (strcmp(seq, "1;5C") == 0 || strcmp(seq, "1;3C") == 0) return K_WRIGHT;
    if (strcmp(seq, "44~") == 0) return K_HOME;
    if (strcmp(seq, "45~") == 0) return K_END;
    return K_IGNORE;
}

static void quote_completion(SBuf *b, const char *s)
{
    for (; *s; s++) {
        if (strchr(" \t'\"\\$`&|;()<>!*?[]{}#~", *s) && !(*s == '~' && b->len == 0)) sb_addc(b, '\\');
        sb_addc(b, *s);
    }
}

static void list_candidates(Ed *e, StrList *c)
{
    int i, w = 0, per, rows, r;
    for (i = 0; i < c->n; i++) {
        const char *nm = c->v[i];
        int l;
        size_t L = strlen(nm);
        /* show basename only for paths */
        if (L > 1) {
            const char *b = nm + L - 1;
            while (b > nm && b[-1] != '/' && b[-1] != ':') b--;
            if (b > nm && (b == nm + L - 1) && (nm[L - 1] == '/' || nm[L - 1] == ':')) {
                b--;
                while (b > nm && b[-1] != '/' && b[-1] != ':') b--;
            }
            nm = b;
        }
        l = (int)strlen(nm);
        if (l > w) w = l;
    }
    w += 2;
    per = cols / w;
    if (per < 1) per = 1;
    rows = (c->n + per - 1) / per;
    out_s("\n");
    if (e->oldrow) {
        /* we were on a later row: newline from the cursor position is fine */
    }
    for (r = 0; r < rows; r++) {
        for (i = r; i < c->n; i += rows) {
            const char *nm = c->v[i];
            size_t L = strlen(nm);
            int l, k;
            if (L > 1) {
                const char *b = nm + L - 1;
                while (b > nm && b[-1] != '/' && b[-1] != ':') b--;
                if (b > nm && (b == nm + L - 1) && (nm[L - 1] == '/' || nm[L - 1] == ':')) {
                    b--;
                    while (b > nm && b[-1] != '/' && b[-1] != ':') b--;
                }
                nm = b;
            }
            out_s(nm);
            l = (int)strlen(nm);
            if (i + rows < c->n)
                for (k = l; k < w; k++) out_s(" ");
        }
        out_s("\n");
    }
    e->oldrow = 0;
}

static void do_complete(Ed *e, int *lasttab)
{
    int start = e->pos, i;
    StrList c;
    int isfile = 0;
    /* find word start (respect backslash escapes) */
    while (start > 0) {
        char ch = e->buf.s[start - 1];
        if (strchr(" \t;|&(<>`", ch) && !(start >= 2 && e->buf.s[start - 2] == '\\')) break;
        start--;
    }
    sl_init(&c);
    {
        /* unescape the word for matching */
        SBuf w;
        char *line;
        int k;
        sb_init(&w);
        sb_addn(&w, e->buf.s, start);
        for (k = start; k < e->pos; k++) {
            if (e->buf.s[k] == '\\' && k + 1 < e->pos) k++;
            sb_addc(&w, e->buf.s[k]);
        }
        line = sb_take(&w);
        complete_word(line, start, (int)strlen(line), &c, &isfile);
        xfree(line);
    }
    if (c.n == 0) {
        out_s("\007");
        out_flush();
        sl_free(&c);
        return;
    }
    {
        /* common prefix */
        int lcp = (int)strlen(c.v[0]);
        for (i = 1; i < c.n; i++) {
            int k = 0;
            while (k < lcp && c.v[i][k] && (NOCASE ? tolower((unsigned char)c.v[i][k]) == tolower((unsigned char)c.v[0][k]) : c.v[i][k] == c.v[0][k])) k++;
            lcp = k;
        }
        {
            char *pre = xstrndup(c.v[0], lcp);
            SBuf q;
            int curlen;
            sb_init(&q);
            sb_addn(&q, "", 0);
            quote_completion(&q, pre);
            curlen = e->pos - start;
            if (c.n == 1) {
                size_t L = strlen(pre);
                if (!(L && (pre[L - 1] == '/' || pre[L - 1] == ':'))) sb_addc(&q, ' ');
            }
            if (q.len > curlen || c.n == 1 || (q.len == curlen && strncmp(q.s, e->buf.s + start, curlen) != 0)) {
                ed_delete(e, start, e->pos);
                ed_insert(e, q.s, q.len);
                /* still ambiguous: the next Tab lists the candidates */
                *lasttab = c.n > 1;
            } else if (*lasttab) {
                list_candidates(e, &c);
                *lasttab = 0;
            } else {
                out_s("\007");
                *lasttab = 1;
            }
            sb_free(&q);
            xfree(pre);
        }
    }
    sl_free(&c);
    refresh(e);
}

/* Ctrl-R incremental search */
static void reverse_search(Ed *e)
{
    SBuf pat;
    int idx = hcount;
    sb_init(&pat);
    sb_addn(&pat, "", 0);
    for (;;) {
        int c, i, found = -1;
        for (i = idx - 1; i >= 0; i--)
            if (strstr(hist[i], pat.s)) {
                found = i;
                break;
            }
        if (found < 0 && pat.len == 0) found = -1;
        if (e->oldrow > 0) out_csi(e->oldrow, 'A');
        e->oldrow = 0;
        out_s("\r\033[J(reverse-i-search)`");
        out_s(pat.s);
        out_s("': ");
        if (found >= 0) out_s(hist[found]);
        out_flush();
        c = getkey();
        if (c == 18) {
            /* next match */
            if (found >= 0) idx = found;
            continue;
        }
        if (c == 8 || c == 127) {
            if (pat.len) pat.s[--pat.len] = 0;
            idx = hcount;
            continue;
        }
        if (c >= 32 && c < 127) {
            sb_addc(&pat, c);
            continue;
        }
        if (c == 7 || c == 3) {
            /* abort */
            break;
        }
        if (found >= 0) ed_set(e, hist[found]);
        break;
    }
    {
        /* recompute: search line occupied possibly several rows */
        int w = 22 + pat.len + (e->buf.len);
        int rows = w / cols;
        if (rows) out_csi(rows, 'A');
        out_s("\r\033[J");
        e->oldrow = 0;
    }
    sb_free(&pat);
    refresh(e);
}

char *le_readline(const char *prompt)
{
    Ed e;
    int lasttab = 0, lastlen;
    int rows;
    tin = os_stdin();
    tout = os_stdout();
    sb_init(&obuf);
    if (os_console_size(tout, &rows, &cols) != 0 || cols < 10) cols = 80;
    memset(&e, 0, sizeof(e));
    sb_init(&e.buf);
    sb_addn(&e.buf, "", 0);
    e.prompt = prompt;
    e.pw = visible_width(prompt, &lastlen) % cols;
    e.hidx = hcount;
    {
        /* print the prompt (may contain newlines: only the last line is
         * redrawn on refresh) */
        const char *nl = strrchr(prompt, '\n');
        if (nl) {
            out_n(prompt, (int)(nl - prompt) + 1);
            e.prompt = nl + 1;
        }
    }
    sh_flush_all();
    if (os_rawmode(tin, 1) != 0) {
        /* console without raw mode (e.g. a pipe-like handler or vamos):
         * plain line input, and say why the editing keys do nothing */
        static int warned;
        SBuf l;
        unsigned char ch;
        if (!warned++)
            out_s("bash: this console has no raw mode; line editing and history keys are off\n");
        out_prompt(e.prompt);
        out_flush();
        sb_init(&l);
        sb_addn(&l, "", 0);
        for (;;) {
            if (os_read(tin, &ch, 1) != 1) {
                sb_free(&l);
                sb_free(&e.buf);
                return NULL;
            }
            if (ch == '\n' || ch == '\r') break;
            sb_addc(&l, ch);
        }
        sb_free(&e.buf);
        return sb_take(&l);
    }
    refresh(&e);
    for (;;) {
        int c = getkey();
        if (c < 0) {
            os_rawmode(tin, 0);
            sb_free(&e.buf);
            xfree(e.saved);
            return NULL;
        }
        if (c == 27) {
            int n2;
            if (!os_waitchar(tin, 300000)) continue;
            n2 = getkey();
            if (n2 == '[' || n2 == 'O') c = read_seq(n2);
            else if (n2 == 'b' || n2 == 'B') c = K_WLEFT;
            else if (n2 == 'f' || n2 == 'F') c = K_WRIGHT;
            else if (n2 == 'd') {
                int to = word_right(&e);
                set_kill(e.buf.s + e.pos, to - e.pos);
                ed_delete(&e, e.pos, to);
                refresh(&e);
                continue;
            } else if (n2 == 127 || n2 == 8) {
                int from = word_left(&e);
                set_kill(e.buf.s + from, e.pos - from);
                ed_delete(&e, from, e.pos);
                refresh(&e);
                continue;
            } else if (n2 == '.' || n2 == '_') {
                if (hcount) {
                    StrList w;
                    sl_init(&w);
                    hist_words(hist[hcount - 1], &w);
                    if (w.n) ed_insert(&e, w.v[w.n - 1], (int)strlen(w.v[w.n - 1]));
                    sl_free(&w);
                    refresh(&e);
                }
                continue;
            } else continue;
        } else if (c == 0x9b) {
            c = read_seq(0x9b);
        }
        if (c != 9) lasttab = 0;
        switch (c) {
        case '\r':
        case '\n': {
            char *r;
            e.pos = e.buf.len;
            refresh(&e);
            out_s("\n");
            out_flush();
            os_rawmode(tin, 0);
            r = xstrdup(e.buf.s);
            sb_free(&e.buf);
            xfree(e.saved);
            return r;
        }
        case 1:          /* ^A */
        case K_HOME:
            e.pos = 0;
            break;
        case 5:          /* ^E */
        case K_END:
            e.pos = e.buf.len;
            break;
        case 2:          /* ^B */
        case K_LEFT:
            if (e.pos > 0) e.pos--;
            break;
        case 6:          /* ^F */
        case K_RIGHT:
            if (e.pos < e.buf.len) e.pos++;
            break;
        case K_WLEFT:
            e.pos = word_left(&e);
            break;
        case K_WRIGHT:
            e.pos = word_right(&e);
            break;
        case 8:          /* backspace */
#ifndef AMIGA
        case 127:
#endif
            if (e.pos > 0) ed_delete(&e, e.pos - 1, e.pos);
            break;
#ifdef AMIGA
        case 127:        /* Amiga Del key: delete under cursor */
#endif
        case K_DEL:
            ed_delete(&e, e.pos, e.pos + 1);
            break;
        case 4:          /* ^D */
            if (getenv("AMIBASH_DEBUG")) sh_printf(2, "[getkey: ^D]\n");
            if (e.buf.len == 0) {
                out_s("\n");
                out_flush();
                os_rawmode(tin, 0);
                sb_free(&e.buf);
                xfree(e.saved);
                return NULL;
            }
            ed_delete(&e, e.pos, e.pos + 1);
            break;
        case 3: {        /* ^C: abandon the line (and any continuation) */
            e.pos = e.buf.len;
            refresh(&e);
            out_s("^C\n");
            out_flush();
            os_check_break();
            os_break_flag = 0;
            os_rawmode(tin, 0);
            sb_free(&e.buf);
            xfree(e.saved);
            sh.laststatus = 130;
            le_cancelled = 1;
            return xstrdup("");
        }
        case 11:         /* ^K */
            set_kill(e.buf.s + e.pos, e.buf.len - e.pos);
            ed_delete(&e, e.pos, e.buf.len);
            break;
        case 21:         /* ^U */
            set_kill(e.buf.s, e.pos);
            ed_delete(&e, 0, e.pos);
            break;
        case 23: {       /* ^W: kill previous whitespace-delimited word */
            int p = e.pos;
            while (p > 0 && e.buf.s[p - 1] == ' ') p--;
            while (p > 0 && e.buf.s[p - 1] != ' ') p--;
            set_kill(e.buf.s + p, e.pos - p);
            ed_delete(&e, p, e.pos);
            break;
        }
        case 25:         /* ^Y */
            if (killbuf) ed_insert(&e, killbuf, (int)strlen(killbuf));
            break;
        case 20:         /* ^T */
            if (e.pos > 0 && e.buf.len >= 2) {
                int p = e.pos == e.buf.len ? e.pos - 1 : e.pos;
                char t = e.buf.s[p];
                e.buf.s[p] = e.buf.s[p - 1];
                e.buf.s[p - 1] = t;
                e.pos = p + 1;
            }
            break;
        case 12:         /* ^L */
            out_s("\033[H\033[J");
#ifdef AMIGA
            out_s("\014");
#endif
            e.oldrow = 0;
            break;
        case 16:         /* ^P */
        case K_UP:
            if (e.hidx > 0) {
                if (e.hidx == hcount) {
                    xfree(e.saved);
                    e.saved = xstrdup(e.buf.s);
                }
                e.hidx--;
                ed_set(&e, hist[e.hidx]);
            }
            break;
        case 14:         /* ^N */
        case K_DOWN:
            if (e.hidx < hcount) {
                e.hidx++;
                ed_set(&e, e.hidx == hcount ? (e.saved ? e.saved : "") : hist[e.hidx]);
            }
            break;
        case K_SUP:
            if (hcount) {
                if (e.hidx == hcount) {
                    xfree(e.saved);
                    e.saved = xstrdup(e.buf.s);
                }
                e.hidx = 0;
                ed_set(&e, hist[0]);
            }
            break;
        case K_SDOWN:
            e.hidx = hcount;
            ed_set(&e, e.saved ? e.saved : "");
            break;
        case 18:         /* ^R */
            reverse_search(&e);
            break;
        case 9:          /* tab */
            do_complete(&e, &lasttab);
            continue;
        case K_HELP: {
            /* Help key: show help */
            break;
        }
        case K_IGNORE:
            continue;
        default:
            if (c >= 32 && c < 256 && c != 127 && !(c >= 0x80 && c < 0xA0)) {
                char ch = (char)c;
                if (e.pos == e.buf.len && (e.pw + e.buf.len + 1) % cols != 0) {
                    /* fast path: append at end */
                    ed_insert(&e, &ch, 1);
                    out_n(&ch, 1);
                    out_flush();
                    continue;
                }
                ed_insert(&e, &ch, 1);
            } else {
                continue;
            }
        }
        refresh(&e);
    }
}
