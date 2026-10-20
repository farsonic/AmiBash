/*
 * builtins.c - shell builtin commands.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

int keep_redirs;     /* set by "exec" without a command */

#define ERR2 2

/* ------------------------------------------------------------------ */
/* cd / pwd / pushd / popd / dirs                                       */

void sh_set_pwd(const char *p)
{
    if (sh.pwd && sh.subshell == 0) xfree(sh.pwd);
    sh.pwd = xstrdup(p);
    var_set("PWD", p, 0);
}

int sh_chdir(const char *path, int quiet)
{
    char *abs = path_absolute(path);
    char *np;
    const char *old = sh.pwd;
    if (!path_is_vroot(abs)) {
        np = path_native(abs);
        if (os_chdir(np) != 0) {
            OsDirEnt e;
            if (!quiet) {
                if (os_stat(np, &e) == 0 && e.type != OS_T_DIR)
                    sh_error("cd: %s: Not a directory", path);
                else
                    sh_error("cd: %s: No such file or directory", path);
            }
            xfree(np);
            xfree(abs);
            return 1;
        }
        xfree(np);
    }
    if (old) var_set("OLDPWD", old, 0);
    sh_set_pwd(abs);
    xfree(abs);
    return 0;
}

static int bi_cd(int argc, char **argv)
{
    const char *dir;
    int i = 1, print = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "-") != 0) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "-L") && strcmp(argv[i], "-P") && strcmp(argv[i], "-e") && strcmp(argv[i], "-@")) {
            sh_cmd_error("cd", "%s: invalid option", argv[i]);
            sh_puts(2, "cd: usage: cd [-L|[-P [-e]] [-@]] [dir]\n");
            return 2;
        }
        i++;
    }
    if (argc - i > 1) {
        sh_cmd_error("cd", "too many arguments");
        return 1;
    }
    if (i >= argc) {
        dir = var_get("HOME");
        if (!dir) {
            sh_cmd_error("cd", "HOME not set");
            return 1;
        }
    } else if (strcmp(argv[i], "-") == 0) {
        dir = var_get("OLDPWD");
        if (!dir) {
            sh_cmd_error("cd", "OLDPWD not set");
            return 1;
        }
        print = 1;
    } else {
        dir = argv[i];
    }
    if (!*dir) return 0;
    /* CDPATH */
    if (!path_is_absolute(dir) && dir[0] != '.' && var_get("CDPATH")) {
        const char *cp = var_get("CDPATH");
        const char *s = cp, *e;
        for (;;) {
            char *d, *cand;
            e = strchr(s, ';');
            if (!e) e = strchr(s, ',');
            if (!e) e = s + strlen(s);
            d = xstrndup(s, e - s);
            cand = path_join(*d ? d : ".", dir);
            xfree(d);
            if (sh_is_dir(cand)) {
                int r = sh_chdir(cand, 0);
                if (!r && *d) sh_printf(1, "%s\n", sh.pwd);
                xfree(cand);
                return r;
            }
            xfree(cand);
            if (!*e) break;
            s = e + 1;
        }
    }
    {
        char *copy = xstrdup(dir);
        int r = sh_chdir(copy, 0);
        xfree(copy);
        if (!r && print) sh_printf(1, "%s\n", sh.pwd);
        return r;
    }
}

static int bi_pwd(int argc, char **argv)
{
    int phys = argc > 1 && strcmp(argv[1], "-P") == 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] && strcmp(argv[1], "-P") != 0 &&
        strcmp(argv[1], "-L") != 0 && strcmp(argv[1], "--") != 0) {
        sh_cmd_error("pwd", "%s: invalid option", argv[1]);
        sh_puts(2, "pwd: usage: pwd [-LP]\n");
        return 2;
    }
    if (phys) {
        char buf[512];
        if (!path_is_vroot(sh.pwd) && os_getcwd(buf, sizeof(buf))) {
            sh_printf(1, "%s\n", buf);
            return 0;
        }
    }
    sh_printf(1, "%s\n", sh.pwd ? sh.pwd : "");
    return 0;
}

static void print_dirs(int vertical, int numbered)
{
    int i, n = sh.dirstack.n;
    const char *home = var_get("HOME");
    for (i = -1; i < n; i++) {
        const char *d = i < 0 ? sh.pwd : sh.dirstack.v[n - 1 - i];
        if (numbered) sh_printf(1, "%2d  ", i + 1);
        if (home && *home && strncmp(d, home, strlen(home)) == 0 && !vertical) {
            size_t hl = strlen(home);
            sh_putc(1, '~');
            if (d[hl] && d[hl] != '/' && home[hl - 1] != '/') sh_putc(1, '/');
            sh_puts(1, d + hl);
        } else {
            sh_puts(1, d);
        }
        sh_putc(1, vertical || numbered ? '\n' : (i + 1 < n ? ' ' : '\n'));
    }
}

static int bi_dirs(int argc, char **argv)
{
    int i, v = 0, p = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            sl_free(&sh.dirstack);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0) v = 1;
        else if (strcmp(argv[i], "-p") == 0) p = 1;
        else if (strcmp(argv[i], "-l") == 0) p = p;
    }
    print_dirs(p, v);
    return 0;
}

static int bi_pushd(int argc, char **argv)
{
    char *old = xstrdup(sh.pwd);
    if (argc < 2) {
        char *top;
        if (sh.dirstack.n == 0) {
            sh_cmd_error("pushd", "no other directory");
            xfree(old);
            return 1;
        }
        top = sh.dirstack.v[sh.dirstack.n - 1];
        if (sh_chdir(top, 0)) {
            xfree(old);
            return 1;
        }
        xfree(sh.dirstack.v[sh.dirstack.n - 1]);
        sh.dirstack.v[sh.dirstack.n - 1] = old;
    } else {
        if (sh_chdir(argv[1], 0)) {
            xfree(old);
            return 1;
        }
        sl_add(&sh.dirstack, old);
    }
    print_dirs(0, 0);
    return 0;
}

static int bi_popd(int argc, char **argv)
{
    char *top;
    (void)argc;
    (void)argv;
    if (sh.dirstack.n == 0) {
        sh_cmd_error("popd", "directory stack empty");
        return 1;
    }
    top = sh.dirstack.v[sh.dirstack.n - 1];
    if (sh_chdir(top, 0)) return 1;
    xfree(top);
    sh.dirstack.n--;
    sh.dirstack.v[sh.dirstack.n] = NULL;
    print_dirs(0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* echo / printf                                                        */

static int bi_echo(int argc, char **argv)
{
    int i = 1, nonl = 0, esc = sh.sh_xpg_echo;
    SBuf b;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        const char *p = argv[i] + 1;
        int ok = 1;
        for (; *p; p++)
            if (*p != 'n' && *p != 'e' && *p != 'E') ok = 0;
        if (!ok) break;
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'n') nonl = 1;
            else if (*p == 'e') esc = 1;
            else esc = 0;
        }
        i++;
    }
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (; i < argc; i++) {
        if (esc) {
            int stop = 0;
            char *d = ansi_decode(argv[i], (int)strlen(argv[i]), 1, &stop);
            sb_addn(&b, d, ansi_decode_len);
            xfree(d);
            if (stop) {
                nonl = 1;
                break;
            }
        } else {
            sb_adds(&b, argv[i]);
        }
        if (i + 1 < argc) sb_addc(&b, ' ');
    }
    if (!nonl) sb_addc(&b, '\n');
    if (!sh_fd(1)) {
        sb_free(&b);
        sh_cmd_error("echo", "write error: Bad file descriptor");
        return 1;
    }
    sh_write(1, b.s, b.len);
    sb_free(&b);
    return 0;
}

static ShInt pf_int(const char *s, int *err)
{
    ShInt v = 0;
    const char *p = s;
    int neg = 0;
    if (*s == '\'' || *s == '"') return (unsigned char)s[1];
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') neg = 1, p++;
    else if (*p == '+') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        while (isxdigit((unsigned char)*p)) {
            v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : tolower((unsigned char)*p) - 'a' + 10);
            p++;
        }
    } else if (p[0] == '0') {
        while (*p >= '0' && *p <= '7') v = v * 8 + (*p++ - '0');
    } else {
        while (isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
    }
    if (*p || (p == s && *s)) {
        sh_cmd_error("printf", "%s: invalid number", s);
        *err = 1;
    }
    return neg ? -v : v;
}

static void fmt_int(SBuf *out, const char *flags, int width, int prec, int conv, ShInt v)
{
    char digits[72];
    char *d = digits + sizeof(digits) - 1;
    unsigned long long u;
    int neg = 0, base = 10, len, pad, zeros = 0;
    const char *prefix = "";
    int left = strchr(flags, '-') != NULL, zero = strchr(flags, '0') != NULL;
    *d = 0;
    if (conv == 'd' || conv == 'i') {
        neg = v < 0;
        u = neg ? (unsigned long long)0 - (unsigned long long)v : (unsigned long long)v;
    } else {
        u = (unsigned long long)v;
        if (conv == 'o') base = 8;
        else if (conv == 'x' || conv == 'X') base = 16;
    }
    if (!(prec == 0 && u == 0)) {
        do {
            int dg = (int)(u % base);
            *--d = (char)(dg < 10 ? '0' + dg : (conv == 'X' ? 'A' : 'a') + dg - 10);
            u /= base;
        } while (u);
    }
    len = (int)strlen(d);
    if (prec > len) zeros = prec - len;
    if (neg) prefix = "-";
    else if (strchr(flags, '+') && (conv == 'd' || conv == 'i')) prefix = "+";
    else if (strchr(flags, ' ') && (conv == 'd' || conv == 'i')) prefix = " ";
    if (strchr(flags, '#') && v != 0) {
        if (conv == 'x') prefix = "0x";
        else if (conv == 'X') prefix = "0X";
        else if (conv == 'o' && zeros == 0) prefix = "0";
    }
    pad = width - len - zeros - (int)strlen(prefix);
    if (zero && !left && prec < 0) {
        zeros += pad > 0 ? pad : 0;
        pad = 0;
    }
    if (!left) while (pad-- > 0) sb_addc(out, ' ');
    sb_adds(out, prefix);
    while (zeros-- > 0) sb_addc(out, '0');
    sb_adds(out, d);
    if (left) while (pad-- > 0) sb_addc(out, ' ');
}

static void fmt_strn(SBuf *out, const char *flags, int width, int prec, const char *s, int len)
{
    int pad;
    if (prec >= 0 && prec < len) len = prec;
    pad = width - len;
    if (!strchr(flags, '-')) while (pad-- > 0) sb_addc(out, ' ');
    sb_addn(out, s, len);
    if (strchr(flags, '-')) while (pad-- > 0) sb_addc(out, ' ');
}

static void fmt_str(SBuf *out, const char *flags, int width, int prec, const char *s)
{
    int len = (int)strlen(s), pad;
    if (prec >= 0 && prec < len) len = prec;
    pad = width - len;
    if (!strchr(flags, '-')) while (pad-- > 0) sb_addc(out, ' ');
    sb_addn(out, s, len);
    if (strchr(flags, '-')) while (pad-- > 0) sb_addc(out, ' ');
}

/* printf with one format pass; returns number of args consumed */
/* printf %q: quote like bash (backslashes, or $'...' for control chars) */
static char *printf_quote(const char *s)
{
    SBuf b;
    const unsigned char *p;
    int ansi = 0;
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (!*s) {
        sb_adds(&b, "''");
        return sb_take(&b);
    }
    for (p = (const unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p >= 0x7f) ansi = 1;
    if (ansi) {
        sb_adds(&b, "$'");
        for (p = (const unsigned char *)s; *p; p++) {
            switch (*p) {
            case 7: sb_adds(&b, "\\a"); break;
            case 8: sb_adds(&b, "\\b"); break;
            case 27: sb_adds(&b, "\\E"); break;
            case 12: sb_adds(&b, "\\f"); break;
            case '\n': sb_adds(&b, "\\n"); break;
            case '\r': sb_adds(&b, "\\r"); break;
            case '\t': sb_adds(&b, "\\t"); break;
            case 11: sb_adds(&b, "\\v"); break;
            case '\\': sb_adds(&b, "\\\\"); break;
            case '\'': sb_adds(&b, "\\'"); break;
            default:
                if (*p < 0x20 || *p >= 0x7f) sb_printf(&b, "\\%03o", *p);
                else sb_addc(&b, *p);
            }
        }
        sb_addc(&b, '\'');
        return sb_take(&b);
    }
    for (p = (const unsigned char *)s; *p; p++) {
        if (strchr(" \t'\"\\|&;()<>!{}*[?]^$`,", *p) || ((*p == '#' || *p == '~') && p == (const unsigned char *)s))
            sb_addc(&b, '\\');
        sb_addc(&b, *p);
    }
    return sb_take(&b);
}

static int do_printf(SBuf *out, const char *fmt, char **args, int nargs, int *err, int *stop)
{
    int ai = 0;
    const char *f = fmt;
    while (*f && !*stop) {
        if (*f == '\\') {
            /* escapes in format */
            const char *e = f + 1;
            char *d;
            if (*e == 'x') {
                e++;
                while (e < f + 4 && isxdigit((unsigned char)*e)) e++;
            } else if (*e >= '0' && *e <= '7') {
                int k = 0;
                while (k < 3 && *e >= '0' && *e <= '7') e++, k++;
            } else if (*e == 'u' || *e == 'U') {
                int k = 0, mx = *e == 'u' ? 4 : 8;
                e++;
                while (k < mx && isxdigit((unsigned char)*e)) e++, k++;
            } else if (*e) {
                e++;
            }
            if (f[1] == 'c') {
                /* bash prints \c in a format literally (only %b stops) */
                sb_adds(out, "\\c");
                f += 2;
                continue;
            }
            d = ansi_decode(f, (int)(e - f), 0, NULL);
            sb_addn(out, d, ansi_decode_len);
            xfree(d);
            f = e;
            continue;
        }
        if (*f != '%') {
            sb_addc(out, *f++);
            continue;
        }
        f++;
        if (*f == '%') {
            sb_addc(out, '%');
            f++;
            continue;
        }
        {
            char flags[8];
            int nf = 0, width = 0, prec = -1, conv;
            const char *arg;
            while (*f && strchr("-+ #0", *f) && nf < 7) flags[nf++] = *f++;
            flags[nf] = 0;
            if (*f == '*') {
                width = (int)pf_int(ai < nargs ? args[ai] : "0", err);
                ai++;
                f++;
                if (width < 0) {
                    width = -width;
                    if (nf < 7) flags[nf++] = '-', flags[nf] = 0;
                }
            } else {
                while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
            }
            if (*f == '.') {
                f++;
                prec = 0;
                if (*f == '*') {
                    prec = (int)pf_int(ai < nargs ? args[ai] : "0", err);
                    ai++;
                    f++;
                } else {
                    while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0');
                }
            }
            while (*f == 'l' || *f == 'h' || *f == 'L' || *f == 'j' || *f == 'z' || *f == 't') f++;
            if (*f == '(') {
                /* %(fmt)T */
                const char *e = strchr(f, ')');
                if (e && e[1] == 'T') {
                    char tf[100], tb[256];
                    int l = (int)(e - f - 1);
                    ShTm tm;
                    ShInt t;
                    arg = ai < nargs ? args[ai] : "-1";
                    ai++;
                    t = (*arg ? pf_int(arg, err) : -1);
                    if (t == -1 || t == -2) t = os_time();
                    if (l > 98) l = 98;
                    memcpy(tf, f + 1, l);
                    tf[l] = 0;
                    sh_localtime((long)t, &tm);
                    sh_strftime(tb, sizeof(tb), *tf ? tf : "%X", &tm);
                    fmt_str(out, flags, width, prec, tb);
                    f = e + 2;
                    continue;
                }
            }
            conv = *f;
            if (!conv) {
                sh_cmd_error("printf", "%s: missing format character", fmt);
                *err = 1;
                break;
            }
            f++;
            arg = ai < nargs ? args[ai] : NULL;
            if (strchr("sbqcdiouxXfFeEgGaA", conv)) ai++;
            switch (conv) {
            case 's':
                fmt_str(out, flags, width, prec, arg ? arg : "");
                break;
            case 'b': {
                int st = 0;
                char *d = ansi_decode(arg ? arg : "", arg ? (int)strlen(arg) : 0, 1, &st);
                fmt_strn(out, flags, width, prec, d, ansi_decode_len);
                xfree(d);
                if (st) *stop = 1;
                break;
            }
            case 'q': {
                char *q = printf_quote(arg ? arg : "");
                fmt_str(out, flags, width, prec, q);
                xfree(q);
                break;
            }
            case 'c': {
                char cs[2];
                cs[0] = arg ? arg[0] : 0;
                cs[1] = 0;
                fmt_strn(out, flags, width, -1, cs, 1);   /* "" gives a NUL */
                break;
            }
            case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
                fmt_int(out, flags, width, prec, conv, arg ? pf_int(arg, err) : 0);
                break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
                char spec[32], buf[128];
                double dv = arg ? atof(arg) : 0.0;
                if (prec >= 0) sprintf(spec, "%%%s%d.%d%c", flags, width, prec, conv);
                else sprintf(spec, "%%%s%d%c", flags, width, conv);
                snprintf(buf, sizeof(buf), spec, dv);
                sb_adds(out, buf);
                break;
            }
            default:
                sh_cmd_error("printf", "`%c': invalid format character", conv);
                *err = 1;
                return ai;
            }
        }
    }
    return ai;
}

static int bi_printf(int argc, char **argv)
{
    SBuf out;
    int i = 1, err = 0, stop = 0;
    const char *var = NULL;
    if (i < argc && strcmp(argv[i], "-v") == 0) {
        if (i + 1 >= argc) {
            sh_cmd_error("printf", "-v: option requires an argument");
            return 2;
        }
        var = argv[i + 1];
        i += 2;
    }
    if (i < argc && strcmp(argv[i], "--") == 0) i++;
    if (i >= argc) {
        sh_puts(2, "printf: usage: printf [-v var] format [arguments]\n");
        return 2;
    }
    sb_init(&out);
    sb_addn(&out, "", 0);
    {
        const char *fmt = argv[i];
        char **args = argv + i + 1;
        int nargs = argc - i - 1;
        do {
            int used = do_printf(&out, fmt, args, nargs, &err, &stop);
            if (used == 0 || stop) break;
            args += used;
            nargs -= used;
        } while (nargs > 0);
    }
    if (var) {
        char *br = strchr(var, '[');
        if (br) {
            char *n = xstrndup(var, br - var);
            char *k = xstrndup(br + 1, strlen(br + 1) - 1);
            var_arr_set(n, k, sb_str(&out));
            xfree(n);
            xfree(k);
        } else {
            var_set(var, sb_str(&out), 0);
        }
    } else {
        sh_write(1, out.s, out.len);
    }
    sb_free(&out);
    return err;
}

/* ------------------------------------------------------------------ */
/* read / mapfile                                                       */

static int read_char(ShFile *f, long timeout_ms)
{
    if (timeout_ms >= 0 && f && f->kind == SHF_OS && f->rpos >= f->rlen) {
        if (!os_waitchar(f->h, timeout_ms * 1000L)) return -2;
    }
    return shf_getc(f);
}

static int bi_read(int argc, char **argv)
{
    GetOpt g;
    int c, raw = 0, silent = 0, nchars = -1, exactn = 0, fd = 0, edit = 0;
    const char *prompt = NULL, *arrname = NULL;
    int delim = '\n';
    long timeout = -1;
    SBuf line;
    ShFile *f;
    int status = 0, got = 0;
    sh_getopt_init(&g, "read");
    while ((c = sh_getopt(&g, argc, argv, "ersa:d:n:N:p:t:u:i:")) != -1) {
        switch (c) {
        case 'r': raw = 1; break;
        case 's': silent = 1; break;
        case 'e': edit = 1; break;
        case 'a': arrname = g.arg; break;
        case 'd': delim = g.arg[0] ? (unsigned char)g.arg[0] : 0; break;
        case 'n': nchars = atoi(g.arg); break;
        case 'N': nchars = atoi(g.arg); exactn = 1; break;
        case 'p': prompt = g.arg; break;
        case 't': {
            double t = atof(g.arg);
            timeout = (long)(t * 1000);
            break;
        }
        case 'u': fd = atoi(g.arg); break;
        case 'i': break;
        default:
            sh_puts(2, "read: usage: read [-ers] [-a array] [-d delim] [-i text] [-n nchars] [-N nchars] [-p prompt] [-t timeout] [-u fd] [name ...]\n");
            return 2;
        }
    }
    f = sh_fd(fd);
    if (!f) {
        sh_cmd_error("read", "%d: invalid file descriptor: Bad file descriptor", fd);
        return 1;
    }
    sb_init(&line);
    sb_addn(&line, "", 0);
    if (edit && f->kind == SHF_OS && f->tty && nchars < 0) {
        char *l = le_readline(prompt ? prompt : "");
        if (!l) {
            sb_free(&line);
            return 1;
        }
        sb_adds(&line, l);
        xfree(l);
        got = 1;
    } else {
        int rawmode = 0;
        if (prompt && f->kind == SHF_OS && f->tty) {
            sh_puts(2, prompt);
            shf_flush(sh_fd(2));
        }
        if ((silent || nchars >= 0) && f->kind == SHF_OS && f->tty) {
            sh_flush_all();
            os_rawmode(f->h, 1);
            rawmode = 1;
        }
        for (;;) {
            int ch;
            if (nchars >= 0 && line.len >= nchars) break;
            ch = read_char(f, timeout);
            if (ch == -2) {
                status = 142;
                break;
            }
            if (ch == -1) {
                status = 1;
                break;
            }
            got = 1;
            if (rawmode && ch == '\r') ch = '\n';
            if (rawmode && ch == 3) {
                status = 130;
                break;
            }
            if (!exactn && ch == delim) break;
            if (!raw && ch == '\\') {
                int n2 = read_char(f, timeout);
                if (n2 < 0) break;
                if (n2 == '\n') continue;      /* line continuation */
                sb_addc(&line, 1);             /* marker: escaped char */
                sb_addc(&line, n2);
                continue;
            }
            if (rawmode && !silent) {
                char cc = (char)ch;
                os_write(f->h, &cc, 1);
            }
            sb_addc(&line, ch);
        }
        if (rawmode) {
            os_rawmode(f->h, 0);
            if (silent || nchars >= 0) {
                /* echo newline only for -s with full line */
            }
        }
        if (status == 1 && line.len > 0) status = 1;
        else if (status == 1) {
            got = 0;
        }
    }
    /* split into variables */
    {
        const char *ifs = var_get("IFS");
        char **names = argv + g.ind;
        int nnames = argc - g.ind;
        const char *s = line.s ? line.s : "";
        int ifs_ws;
        StrList fields;
        if (!ifs) ifs = " \t\n";
        sl_init(&fields);
        if (arrname || nnames > 0) {
            /* tokenizer honoring escaped chars (marker \001) */
            int nf = arrname ? 1 << 30 : nnames;
            SBuf cur;
            int i = 0, n = line.len;
            sb_init(&cur);
            sb_addn(&cur, "", 0);
            /* skip leading IFS whitespace */
            while (i < n && s[i] != 1 && strchr(ifs, s[i]) && strchr(" \t\n", s[i])) i++;
            while (i < n) {
                if (fields.n == nf - 1) {
                    /* last variable gets the rest (trailing IFS ws removed) */
                    int e = n;
                    while (e > i && s[e - 1] != 1 && strchr(ifs, s[e - 1]) && strchr(" \t\n", s[e - 1]) &&
                           !(e - 2 >= i && s[e - 2] == 1))
                        e--;
                    {
                        int k;
                        for (k = i; k < e; k++) {
                            if (s[k] == 1 && k + 1 < e) {
                                k++;
                            }
                            sb_addc(&cur, s[k]);
                        }
                    }
                    sl_add(&fields, sb_take(&cur));
                    i = n;
                    break;
                }
                if (s[i] == 1 && i + 1 < n) {
                    sb_addc(&cur, s[i + 1]);
                    i += 2;
                    continue;
                }
                if (strchr(ifs, s[i])) {
                    ifs_ws = strchr(" \t\n", s[i]) != NULL;
                    sl_add(&fields, sb_take(&cur));
                    sb_addn(&cur, "", 0);
                    i++;
                    while (i < n && s[i] != 1 && strchr(ifs, s[i]) && strchr(" \t\n", s[i])) i++;
                    if (ifs_ws && i < n && s[i] != 1 && strchr(ifs, s[i])) {
                        i++;
                        while (i < n && s[i] != 1 && strchr(ifs, s[i]) && strchr(" \t\n", s[i])) i++;
                    }
                    continue;
                }
                sb_addc(&cur, s[i]);
                i++;
            }
            if (cur.len || (i == n && fields.n < nf && cur.s && 0)) sl_add(&fields, sb_take(&cur));
            sb_free(&cur);
        }
        if (arrname) {
            Var *v = var_make_array(arrname, 0);
            int k;
            var_arr_clear(v);
            for (k = 0; k < fields.n; k++) {
                char *key = str_itoa(k);
                var_arr_set(arrname, key, fields.v[k]);
                xfree(key);
            }
        } else if (nnames == 0) {
            /* REPLY gets the line, without escape markers */
            SBuf r;
            int k;
            sb_init(&r);
            sb_addn(&r, "", 0);
            for (k = 0; k < line.len; k++) {
                if (s[k] == 1 && k + 1 < line.len) k++;
                sb_addc(&r, s[k]);
            }
            var_set("REPLY", r.s, 0);
            sb_free(&r);
        } else {
            int k;
            for (k = 0; k < nnames; k++) {
                if (!str_isname(names[k])) {
                    sh_cmd_error("read", "`%s': not a valid identifier", names[k]);
                    status = 1;
                    continue;
                }
                if (var_set(names[k], k < fields.n ? fields.v[k] : "", 0)) status = 1;
            }
        }
        sl_free(&fields);
    }
    sb_free(&line);
    if (!got && status == 0) status = 1;
    return status;
}

static int bi_mapfile(int argc, char **argv)
{
    GetOpt g;
    int c, strip = 0, count = -1, skip = 0, fd = 0, origin = 0, n = 0, have_origin = 0;
    int delim = '\n';
    const char *name = "MAPFILE";
    ShFile *f;
    SBuf line;
    Var *v;
    sh_getopt_init(&g, argv[0]);
    while ((c = sh_getopt(&g, argc, argv, "tn:s:u:O:d:C:c:")) != -1) {
        switch (c) {
        case 't': strip = 1; break;
        case 'n': count = atoi(g.arg); break;
        case 's': skip = atoi(g.arg); break;
        case 'u': fd = atoi(g.arg); break;
        case 'O': origin = atoi(g.arg); have_origin = 1; break;
        case 'd': delim = (unsigned char)g.arg[0]; break;
        case 'C': case 'c': break;
        default: return 2;
        }
    }
    if (g.ind < argc) name = argv[g.ind];
    f = sh_fd(fd);
    if (!f) return 1;
    v = var_make_array(name, 0);
    if (!have_origin) var_arr_clear(v);
    sb_init(&line);
    for (;;) {
        int ch;
        if (count >= 0 && n >= count) break;
        sb_clear(&line);
        sb_addn(&line, "", 0);
        ch = -1;
        while ((ch = shf_getc(f)) != -1) {
            if (ch == delim) break;
            sb_addc(&line, ch);
        }
        if (ch == -1 && line.len == 0) break;
        if (ch == delim && !strip) sb_addc(&line, delim);
        if (skip > 0) {
            skip--;
            continue;
        }
        {
            char *k = str_itoa(origin + n);
            var_arr_set(name, k, line.s);
            xfree(k);
        }
        n++;
        if (ch == -1) break;
    }
    sb_free(&line);
    return 0;
}

/* ------------------------------------------------------------------ */
/* variables                                                            */

static int valid_ident_err(const char *cmd, const char *s)
{
    sh_cmd_error(cmd, "`%s': not a valid identifier", s);
    return 1;
}

/* name part of an assignment argument (up to = or [ or +=) */
static int arg_name_len(const char *a)
{
    int i = 0;
    while (isalnum((unsigned char)a[i]) || a[i] == '_') i++;
    return i;
}

extern int do_assignment(const char *w, int flags);
extern int assign_array(const char *name, const char *inner, int append, int assoc);

static void print_function(int fd, Func *f)
{
    SBuf b;
    sb_init(&b);
    sb_printf(&b, "%s () \n", f->name);
    node_to_text(f->body, &b, 0);
    sb_addc(&b, '\n');
    sh_write(fd, b.s, b.len);
    sb_free(&b);
}

/* declare/typeset/local/export/readonly */
static int declare_common(int argc, char **argv, int mode)
{
    /* mode: 0 declare, 1 local, 2 export, 3 readonly */
    int i, status = 0;
    int set = 0, clear = 0, print = 0, funcs = 0, fnames = 0, global = 0;
    const char *cmd = argv[0];
    for (i = 1; i < argc && (argv[i][0] == '-' || argv[i][0] == '+') && argv[i][1]; i++) {
        const char *p;
        int on = argv[i][0] == '-';
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            int fl = 0;
            switch (*p) {
            case 'a': fl = V_ARRAY; break;
            case 'A': fl = V_ASSOC; break;
            case 'i': fl = V_INTEGER; break;
            case 'l': fl = V_LOWER; break;
            case 'u': fl = V_UPPER; break;
            case 'r': fl = V_READONLY; break;
            case 'x': fl = V_EXPORT; break;
            case 'n': fl = V_NAMEREF; break;
            case 'p': print = 1; break;
            case 'f': funcs = 1; break;
            case 'F': fnames = 1; break;
            case 'g': global = 1; break;
            case 't': break;
            default:
                sh_cmd_error(cmd, "-%c: invalid option", *p);
                if (mode == 0)
                    sh_puts(2, "declare: usage: declare [-aAfFgiIlnrtux] [name[=value] ...] or declare -p [-aAfFilnrtux] [name ...]\n");
                return 2;
            }
            if (fl) {
                if (on) set |= fl;
                else clear |= fl;
            }
        }
    }
    if (mode == 2) {
        if (clear & V_EXPORT) {
            clear = V_EXPORT;
            set &= ~V_EXPORT;
        } else {
            set |= V_EXPORT;
        }
        if (funcs) return 0;
    }
    if (mode == 3) set |= V_READONLY;
    if (funcs || fnames) {
        if (i >= argc) {
            StrList names;
            int k;
            sl_init(&names);
            func_names(&names, NULL);
            for (k = 0; k < names.n; k++) {
                if (fnames) sh_printf(1, "declare -f %s\n", names.v[k]);
                else print_function(1, func_lookup(names.v[k]));
            }
            sl_free(&names);
            return 0;
        }
        for (; i < argc; i++) {
            Func *f = func_lookup(argv[i]);
            if (!f) {
                status = 1;
                continue;
            }
            if (fnames) sh_printf(1, "%s\n", argv[i]);
            else print_function(1, f);
        }
        return status;
    }
    if (i >= argc) {
        if (mode == 2) {
            var_list(1, V_EXPORT, 1);
        } else if (mode == 3) {
            var_list(1, V_READONLY, 1);
        } else if (print || set) {
            var_list(1, set & ~V_LOCAL, 1);
        } else if (mode == 1) {
            /* list locals */
            var_list(1, V_LOCAL, 0);
        } else {
            var_list(1, 0, 0);
        }
        return 0;
    }
    for (; i < argc; i++) {
        const char *a = argv[i];
        int nl = arg_name_len(a);
        char *name;
        Var *v;
        if (mode == 1 && strcmp(a, "-") == 0) {
            /* local -: options are restored when the function returns */
            if (func_local_options()) {
                sh_cmd_error(cmd, "can only be used in a function");
                status = 1;
            }
            continue;
        }
        if (nl == 0 || !(isalpha((unsigned char)a[0]) || a[0] == '_') ||
            (a[nl] && a[nl] != '=' && a[nl] != '[' && !(a[nl] == '+' && a[nl + 1] == '='))) {
            status = valid_ident_err(cmd, a);
            continue;
        }
        name = xstrndup(a, nl);
        if (print) {
            v = var_lookup_raw(name);
            if (!v) {
                sh_cmd_error(cmd, "%s: not found", name);
                status = 1;
            } else {
                var_print_decl(1, v);
            }
            xfree(name);
            continue;
        }
        if ((mode == 1 || (mode == 0 && sh.funcdepth > 0 && !global)) && var_scope_level() > 0) {
            Var *cur = var_lookup_raw(name);
            if (!(cur && cur->scope == var_scope_level() && (cur->flags & V_LOCAL))) {
                if (var_make_local(name)) {
                    xfree(name);
                    status = 1;
                    continue;
                }
            }
        } else if (mode == 1) {
            sh_cmd_error(cmd, "can only be used in a function");
            xfree(name);
            return 1;
        }
        v = (set & V_NAMEREF) ? var_lookup_raw(name) : var_lookup(name);
        if (v && (v->flags & V_READONLY) && (a[nl] || (clear & V_READONLY))) {
            sh_error("%s: readonly variable", name);
            xfree(name);
            status = 1;
            continue;
        }
        if (set & (V_ARRAY | V_ASSOC)) {
            Var *av = v;
            int had_value = v && !(v->flags & V_UNSET);
            if (!av || !(av->flags & (V_ARRAY | V_ASSOC))) av = var_make_array(name, (set & V_ASSOC) != 0);
            av->flags &= ~V_UNSET;
            /* "declare -a x" without a value leaves x declared but unset */
            if (!a[nl] && !had_value && av->alen == 0) av->flags |= V_UNSET;
            if ((set & V_ASSOC) && !(av->flags & V_ASSOC) && av->alen == 0) {
                av->flags &= ~V_ARRAY;
                av->flags |= V_ASSOC;
            }
            v = av;
        }
        if (!v) {
            /* declare x : declared but unset (attributes are applied
             * before any assignment, e.g. declare -i x=1+2) */
            var_set(name, "", 0);
            v = var_lookup(name);
            if (v) {
                v->flags |= V_UNSET;
                xfree(v->val);
                v->val = NULL;
            }
        }
        if (v) {
            v->flags |= set & ~(V_READONLY | V_ARRAY | V_ASSOC);
            v->flags &= ~clear;
            if ((set & V_UPPER)) v->flags &= ~V_LOWER;
            if ((set & V_LOWER)) v->flags &= ~V_UPPER;
            if (clear & V_EXPORT) os_unsetenv(name);
        }
        if (a[nl] && v && (v->flags & V_NAMEREF) && a[nl] == '=') {
            if (var_set_nameref(name, a + nl + 1)) status = 1;
        } else if (a[nl]) {
            /* has assignment */
            if (a[nl] == '=' && a[nl + 1] == '(' && a[strlen(a) - 1] == ')') {
                char *inner = xstrndup(a + nl + 2, strlen(a) - nl - 3);
                if (assign_array(name, inner, 0, (set & V_ASSOC) != 0 || (v && (v->flags & V_ASSOC)))) status = 1;
                xfree(inner);
            } else if (a[nl] == '+' && a[nl + 2] == '(' && a[strlen(a) - 1] == ')') {
                char *inner = xstrndup(a + nl + 3, strlen(a) - nl - 4);
                if (assign_array(name, inner, 1, 0)) status = 1;
                xfree(inner);
            } else {
                /* value already expanded: assign literally */
                const char *eq = strchr(a, '=');
                const char *br = a[nl] == '[' ? a + nl : NULL;
                if (br && eq) {
                    char *sub = xstrndup(br + 1, (strchr(br, ']') ? strchr(br, ']') : eq) - br - 1);
                    Var *av = var_lookup(name);
                    char *key;
                    if (av && (av->flags & V_ASSOC)) key = xstrdup(sub);
                    else {
                        ShInt idx = 0;
                        arith_expand_eval(sub, &idx);
                        key = str_itoa(idx);
                    }
                    if (var_arr_set(name, key, eq + 1)) status = 1;
                    xfree(key);
                    xfree(sub);
                } else if (a[nl] == '+') {
                    if (var_append(name, a + nl + 2)) status = 1;
                } else if (var_set(name, a + nl + 1, 0)) {
                    status = 1;
                }
            }
            v = var_lookup(name);
        }
        if (v) {
            if (set & V_READONLY) v->flags |= V_READONLY;
            if (v->flags & V_EXPORT) {
                const char *val = var_get(name);
                if (val) os_setenv(name, val);
            }
        } else if (mode == 2 || mode == 3) {
            /* export/readonly of unset variable: create it unset */
            var_set(name, "", set);
            v = var_lookup(name);
            if (v) {
                v->flags |= V_UNSET | set;
                xfree(v->val);
                v->val = NULL;
            }
        }
        xfree(name);
    }
    return status;
}

static int bi_declare(int argc, char **argv) { return declare_common(argc, argv, 0); }
static int bi_local(int argc, char **argv) { return declare_common(argc, argv, 1); }
static int bi_export(int argc, char **argv) { return declare_common(argc, argv, 2); }
static int bi_readonly(int argc, char **argv) { return declare_common(argc, argv, 3); }

static int bi_unset(int argc, char **argv)
{
    int i = 1, funcs = 0, vars = 0, status = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "-f") == 0) funcs = 1;
        else if (strcmp(argv[i], "-v") == 0) vars = 1;
        else if (strcmp(argv[i], "-n") == 0) vars = 2;
        else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            sh_cmd_error("unset", "%s: invalid option", argv[i]);
            sh_puts(2, "unset: usage: unset [-f] [-v] [-n] [name ...]\n");
            return 2;
        }
    }
    for (; i < argc; i++) {
        const char *a = argv[i];
        const char *br = strchr(a, '[');
        if (funcs) {
            func_unset(a);
            continue;
        }
        if (br && a[strlen(a) - 1] == ']') {
            char *name = xstrndup(a, br - a);
            char *sub = xstrndup(br + 1, strlen(br + 1) - 1);
            Var *v = var_lookup(name);
            if (strcmp(sub, "@") == 0 || strcmp(sub, "*") == 0) {
                if (var_unset(name)) status = 1;
            } else if (v && (v->flags & V_ASSOC)) {
                char *k = expand_str(sub, 0);
                var_arr_unset(name, k ? k : sub);
                xfree(k);
            } else {
                ShInt idx;
                if (!arith_expand_eval(sub, &idx)) {
                    char *k;
                    if (idx < 0 && v && v->alen) idx += v->arr[v->alen - 1].idx + 1;
                    k = str_itoa(idx);
                    var_arr_unset(name, k);
                    xfree(k);
                }
            }
            xfree(name);
            xfree(sub);
            continue;
        }
        if (!str_isname(a)) {
            status = valid_ident_err("unset", a);
            continue;
        }
        if (!funcs) {
            Var *rv = vars == 2 ? var_lookup_raw(a) : var_lookup(a);
            if (rv && (rv->flags & V_READONLY)) {
                sh_cmd_error("unset", "%s: cannot unset: readonly variable", a);
                status = 1;
                continue;
            }
        }
        if (vars == 2) {
            Var *rv = var_lookup_raw(a);
            if (rv && (rv->flags & V_NAMEREF) && !(rv->flags & V_READONLY)) {
                rv->flags &= ~V_NAMEREF;
                xfree(rv->val);
                rv->val = xstrdup("");
            }
            if (var_unset(a)) status = 1;
        } else if (var_lookup(a) || vars) {
            if (var_unset(a)) status = 1;
        } else {
            func_unset(a);
        }
    }
    return status;
}

static int bi_shift(int argc, char **argv)
{
    ShInt n = 1;
    int i;
    if (argc > 2) {
        sh_cmd_error("shift", "too many arguments");
        return 1;
    }
    if (argc > 1 && !str_toint(argv[1], &n)) {
        sh_cmd_error("shift", "%s: numeric argument required", argv[1]);
        return 1;
    }
    if (n < 0 || n > sh_params.n) {
        if (n > sh_params.n) return 1;
        sh_cmd_error("shift", "%s: shift count out of range", argv[1]);
        return 1;
    }
    for (i = 0; i < n; i++) xfree(sh_params.v[i]);
    memmove(sh_params.v, sh_params.v + n, (sh_params.n - n + 1) * sizeof(char *));
    sh_params.n -= (int)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* set / shopt                                                          */

typedef struct OptDef { const char *name; char letter; int *p; } OptDef;

static OptDef setopts[] = {
    {"allexport", 'a', &sh.opt_allexport},
    {"braceexpand", 'B', &sh.opt_braceexpand},
    {"emacs", 0, &sh.opt_emacs},
    {"errexit", 'e', &sh.opt_errexit},
    {"errtrace", 'E', &sh.opt_errtrace},
    {"functrace", 'T', &sh.opt_functrace},
    {"hashall", 'h', &sh.opt_hashall},
    {"histexpand", 'H', &sh.opt_histexpand},
    {"history", 0, &sh.opt_history},
    {"ignoreeof", 0, &sh.opt_ignoreeof},
    {"interactive-comments", 0, NULL},
    {"keyword", 'k', NULL},
    {"monitor", 'm', &sh.opt_monitor},
    {"noclobber", 'C', &sh.opt_noclobber},
    {"noexec", 'n', &sh.opt_noexec},
    {"noglob", 'f', &sh.opt_noglob},
    {"nolog", 0, NULL},
    {"notify", 'b', &sh.opt_notify},
    {"nounset", 'u', &sh.opt_nounset},
    {"onecmd", 't', &sh.opt_onecmd},
    {"physical", 'P', NULL},
    {"pipefail", 0, &sh.opt_pipefail},
    {"posix", 0, &sh.opt_posix},
    {"privileged", 'p', NULL},
    {"verbose", 'v', &sh.opt_verbose},
    {"vi", 0, &sh.opt_vi},
    {"xtrace", 'x', &sh.opt_xtrace},
    {NULL, 0, NULL}
};

static int set_option_name(const char *name, int on)
{
    int i;
    for (i = 0; setopts[i].name; i++) {
        if (strcmp(setopts[i].name, name) == 0) {
            if (setopts[i].p) *setopts[i].p = on;
            if (strcmp(name, "vi") == 0 && on) sh.opt_emacs = 0;
            if (strcmp(name, "emacs") == 0 && on) sh.opt_vi = 0;
            return 0;
        }
    }
    sh_cmd_error("set", "%s: invalid option name", name);
    return 1;
}

static int list_options(int plus, const char *only)
{
    int i, found = 0;
    for (i = 0; setopts[i].name; i++) {
        int on;
        if (only && strcmp(only, setopts[i].name) != 0) continue;
        found = 1;
        on = setopts[i].p ? *setopts[i].p : (strcmp(setopts[i].name, "interactive-comments") == 0);
        if (plus) sh_printf(1, "set %co %s\n", on ? '-' : '+', setopts[i].name);
        else sh_printf(1, "%-15s\t%s\n", setopts[i].name, on ? "on" : "off");
    }
    return found;
}

static int bi_set(int argc, char **argv)
{
    int i;
    if (argc == 1) {
        var_list(1, 0, 0);
        return 0;
    }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int on;
        const char *p;
        if (strcmp(a, "--") == 0) {
            i++;
            params_set(argv + i, argc - i);
            return 0;
        }
        if (strcmp(a, "-") == 0) {
            sh.opt_xtrace = sh.opt_verbose = 0;
            i++;
            if (i < argc) params_set(argv + i, argc - i);   /* "set -" keeps $@ */
            return 0;
        }
        if (a[0] != '-' && a[0] != '+') {
            params_set(argv + i, argc - i);
            return 0;
        }
        on = a[0] == '-';
        for (p = a + 1; *p; p++) {
            int k;
            if (*p == 'o') {
                if (i + 1 >= argc) {
                    list_options(!on, NULL);
                    continue;
                }
                if (set_option_name(argv[++i], on)) return 1;
                continue;
            }
            for (k = 0; setopts[k].name; k++)
                if (setopts[k].letter == *p) break;
            if (!setopts[k].name) {
                if (*p == 'i' || *p == 's' || *p == 'c') continue;
                sh_cmd_error("set", "%c%c: invalid option", a[0], *p);
                sh_puts(2, "set: usage: set [-abefhkmnptuvxBCEHPT] [-o option-name] [--] [-] [arg ...]\n");
                return 2;
            }
            if (setopts[k].p) *setopts[k].p = on;
        }
    }
    return 0;
}

typedef struct ShoptDef { const char *name; int *p; } ShoptDef;

static ShoptDef shopts[] = {
    {"amigapaths", &sh.sh_pathconv},
    {"autocd", &sh.sh_autocd},
    {"cdspell", &sh.sh_cdspell},
    {"checkwinsize", &sh.sh_checkwinsize},
    {"cmdhist", &sh.sh_cmdhist},
    {"dotglob", &sh.sh_dotglob},
    {"expand_aliases", &sh.sh_expand_aliases},
    {"extglob", &sh.sh_extglob},
    {"failglob", &sh.sh_failglob},
    {"globstar", &sh.sh_globstar},
    {"histappend", &sh.sh_histappend},
    {"lastpipe", &sh.sh_lastpipe},
    {"nocaseglob", &sh.sh_nocaseglob},
    {"nocasematch", &sh.sh_nocasematch},
    {"nullglob", &sh.sh_nullglob},
    {"xpg_echo", &sh.sh_xpg_echo},
    {"extquote", NULL},
    {"interactive_comments", NULL},
    {"progcomp", NULL},
    {"promptvars", NULL},
    {"sourcepath", NULL},
    {NULL, NULL}
};

/* colon-separated list of enabled options ($SHELLOPTS / $BASHOPTS) */
char *shell_options_string(int set_opts)
{
    StrList l;
    SBuf b;
    int k;
    sl_init(&l);
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (set_opts) {
        for (k = 0; setopts[k].name; k++)
            if (setopts[k].p ? *setopts[k].p : strcmp(setopts[k].name, "interactive-comments") == 0)
                sl_adddup(&l, setopts[k].name);
    } else {
        for (k = 0; shopts[k].name; k++)
            if (!shopts[k].p || *shopts[k].p) sl_adddup(&l, shopts[k].name);
    }
    sl_sort(&l, 0);
    for (k = 0; k < l.n; k++) {
        if (k) sb_addc(&b, ':');
        sb_adds(&b, l.v[k]);
    }
    sl_free(&l);
    return sb_take(&b);
}

static int bi_shopt(int argc, char **argv)
{
    int i = 1, mode = 0, quiet = 0, print = 0, oflag = 0, status = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *p;
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 's') mode = 1;
            else if (*p == 'u') mode = -1;
            else if (*p == 'q') quiet = 1;
            else if (*p == 'p') print = 1;
            else if (*p == 'o') oflag = 1;
            else {
                sh_cmd_error("shopt", "-%c: invalid option", *p);
                return 2;
            }
        }
    }
    if (oflag) {
        int first = i;
        for (; i < argc; i++) {
            if (mode) {
                if (set_option_name(argv[i], mode > 0)) status = 1;
            } else if (quiet) {
                int k;
                for (k = 0; setopts[k].name; k++)
                    if (strcmp(setopts[k].name, argv[i]) == 0) break;
                if (!setopts[k].name || !setopts[k].p || !*setopts[k].p) status = 1;
            } else if (!list_options(print, argv[i])) {
                sh_cmd_error("shopt", "%s: invalid option name", argv[i]);
                status = 1;
            }
        }
        if (!mode && first >= argc) list_options(print, NULL);
        return status;
    }
    if (i >= argc) {
        int k;
        for (k = 0; shopts[k].name; k++) {
            int on = shopts[k].p ? *shopts[k].p : 1;
            if (mode && (mode > 0) != on) continue;
            if (print) sh_printf(1, "shopt %s %s\n", on ? "-s" : "-u", shopts[k].name);
            else sh_printf(1, "%-15s\t%s\n", shopts[k].name, on ? "on" : "off");
        }
        return 0;
    }
    for (; i < argc; i++) {
        int k;
        for (k = 0; shopts[k].name; k++)
            if (strcmp(shopts[k].name, argv[i]) == 0) break;
        if (!shopts[k].name) {
            sh_cmd_error("shopt", "%s: invalid shell option name", argv[i]);
            status = 1;
            continue;
        }
        if (mode) {
            if (shopts[k].p) *shopts[k].p = mode > 0;
        } else {
            int on = shopts[k].p ? *shopts[k].p : 1;
            if (!quiet) {
                if (print) sh_printf(1, "shopt %s %s\n", on ? "-s" : "-u", shopts[k].name);
                else sh_printf(1, "%-15s\t%s\n", shopts[k].name, on ? "on" : "off");
            }
            if (!on) status = 1;
        }
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* control flow                                                         */

static int bi_exit(int argc, char **argv)
{
    ShInt n = sh.laststatus;
    if (argc > 1 && !str_toint(argv[1], &n)) {
        sh_cmd_error("exit", "%s: numeric argument required", argv[1]);
        n = 2;
    }
    if (sh.interactive && sh.subshell == 0 && sh.sourcedepth == 0) sh_puts(2, "exit\n");
    sh.flow = FLOW_EXIT;
    sh.exitstatus = (int)(n & 255);
    return sh.exitstatus;
}

static int bi_return(int argc, char **argv)
{
    ShInt n = sh.laststatus;
    /* the script being run is itself at sourcedepth 1 */
    if (sh.funcdepth == 0 && sh.sourcedepth <= (sh.interactive ? 0 : 1)) {
        sh_cmd_error("return", "can only `return' from a function or sourced script");
        return 2;
    }
    if (argc > 1 && !str_toint(argv[1], &n)) {
        sh_cmd_error("return", "%s: numeric argument required", argv[1]);
        n = 2;
    }
    sh.flow = FLOW_RETURN;
    sh.exitstatus = (int)(n & 255);
    return sh.exitstatus;
}

static int loopctl(int argc, char **argv, int flow)
{
    ShInt n = 1;
    if (argc > 1 && (!str_toint(argv[1], &n) || n < 1)) {
        sh_cmd_error(argv[0], "%s: loop count out of range", argv[1]);
        return 1;
    }
    if (sh.loopdepth == 0) {
        sh_cmd_error(argv[0], "only meaningful in a `for', `while', or `until' loop");
        return 0;
    }
    if (n > sh.loopdepth) n = sh.loopdepth;
    sh.flow = flow;
    sh.flowcount = (int)n;
    return 0;
}

static int bi_break(int argc, char **argv) { return loopctl(argc, argv, FLOW_BREAK); }
static int bi_continue(int argc, char **argv) { return loopctl(argc, argv, FLOW_CONTINUE); }

static int bi_true(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int bi_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

static int bi_test(int argc, char **argv)
{
    if (strcmp(argv[0], "[") == 0) {
        if (strcmp(argv[argc - 1], "]") != 0) {
            sh_cmd_error("[", "missing `]'");
            return 2;
        }
        argc--;
    }
    test_cmdname = argv[0];
    return test_eval(argc - 1, argv + 1);
}

static char *join_args(int argc, char **argv)
{
    SBuf b;
    int i;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (i = 0; i < argc; i++) {
        if (i) sb_addc(&b, ' ');
        sb_adds(&b, argv[i]);
    }
    return sb_take(&b);
}

static int bi_eval(int argc, char **argv)
{
    char *s;
    int r;
    if (argc > 1 && strcmp(argv[1], "--") == 0) argc--, argv++;
    s = join_args(argc - 1, argv + 1);
    r = exec_string(s, "eval");
    xfree(s);
    return r;
}

static int bi_source(int argc, char **argv)
{
    char *path = NULL;
    int r;
    if (argc < 2) {
        sh_cmd_error(argv[0], "filename argument required");
        return 2;
    }
    if (!path_has_dir(argv[1])) {
        /* bash searches PATH for source files, then cwd */
        const char *pv = var_get("PATH");
        (void)pv;
        if (sh_exists(argv[1])) path = xstrdup(argv[1]);
        else {
            char *found = find_command(argv[1]);
            if (found) path = found;
        }
    } else {
        path = xstrdup(argv[1]);
    }
    if (!path || !sh_exists(path)) {
        sh_cmd_error(argv[0], "%s: No such file or directory", argv[1]);
        xfree(path);
        return 1;
    }
    r = exec_file(path, argv + 2, argc - 2, argc <= 2);
    xfree(path);
    return r;
}

static int bi_exec(int argc, char **argv)
{
    int i = 1, r;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        i++;               /* -c -l -a ignored */
    }
    if (i >= argc) {
        keep_redirs = 1;
        return 0;
    }
    r = exec_argv(argc - i, argv + i);
    if (r == 127 && !sh.interactive) {
        sh.flow = FLOW_EXIT;
        sh.exitstatus = 127;
        return 127;
    }
    /* the command "replaced" the shell: no EXIT trap any more */
    sh.traps[TRAP_EXIT].action = NULL;
    sh.flow = FLOW_EXIT;
    sh.exitstatus = r;
    return r;
}

/* ------------------------------------------------------------------ */
/* alias / type / command / hash                                        */

static void print_alias(const char *name)
{
    char *q = shell_quote(alias_get(name));
    sh_printf(1, "alias %s=%s\n", name, q);
    xfree(q);
}

static int bi_alias(int argc, char **argv)
{
    int i = 1, status = 0;
    if (i < argc && strcmp(argv[i], "-p") == 0) i++;
    else if (i < argc && strcmp(argv[i], "--") == 0) i++;
    else if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        sh_cmd_error("alias", "%s: invalid option", argv[i]);
        sh_puts(2, "alias: usage: alias [-p] [name[=value] ... ]\n");
        return 2;
    }
    if (i >= argc) {
        StrList names;
        int k;
        sl_init(&names);
        alias_names(&names, NULL);
        for (k = 0; k < names.n; k++) print_alias(names.v[k]);
        sl_free(&names);
        return 0;
    }
    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = xstrndup(argv[i], eq - argv[i]);
            alias_set(name, eq + 1);
            xfree(name);
        } else if (alias_get(argv[i])) {
            print_alias(argv[i]);
        } else {
            sh_cmd_error("alias", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int bi_unalias(int argc, char **argv)
{
    int i, status = 0;
    if (argc < 2) {
        sh_puts(2, "unalias: usage: unalias [-a] name [name ...]\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (alias_unset(argv[i])) {
            sh_cmd_error("unalias", "%s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int is_keyword(const char *w)
{
    static const char *const kw[] = {
        "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
        "while", "until", "do", "done", "in", "function", "time", "{", "}",
        "!", "[[", "]]", "coproc", NULL
    };
    int i;
    for (i = 0; kw[i]; i++)
        if (strcmp(kw[i], w) == 0) return 1;
    return 0;
}

/* describe a command; mode: 0 type, 1 type -t, 2 command -v, 3 command -V, 4 type -p/-P */
static int describe(const char *name, int mode, int all)
{
    int found = 0;
    const char *al;
    Func *f;
    if (mode != 4) {
        if (sh.sh_expand_aliases && (al = alias_get(name)) != NULL) {
            found = 1;
            if (mode == 1) sh_puts(1, "alias\n");
            else if (mode == 2) {
                char *q = shell_quote(al);
                sh_printf(1, "alias %s=%s\n", name, q);
                xfree(q);
            } else sh_printf(1, "%s is aliased to `%s'\n", name, al);
            if (!all) return 0;
        }
        if (is_keyword(name)) {
            found = 1;
            if (mode == 1) sh_puts(1, "keyword\n");
            else if (mode == 2) sh_printf(1, "%s\n", name);
            else sh_printf(1, "%s is a shell keyword\n", name);
            if (!all) return 0;
        }
        if ((f = func_lookup(name)) != NULL) {
            found = 1;
            if (mode == 1) sh_puts(1, "function\n");
            else if (mode == 2) sh_printf(1, "%s\n", name);
            else {
                sh_printf(1, "%s is a function\n", name);
                print_function(1, f);
            }
            if (!all) return 0;
        }
        if (builtin_lookup(name)) {
            found = 1;
            if (mode == 1) sh_puts(1, "builtin\n");
            else if (mode == 2) sh_printf(1, "%s\n", name);
            else sh_printf(1, "%s is a shell builtin\n", name);
            if (!all) return 0;
        }
    }
    {
        StrList paths;
        int k;
        sl_init(&paths);
        find_command_all(name, &paths);
        for (k = 0; k < paths.n && (k == 0 || all); k++) {
            const char *p = paths.v[k];
            found = 1;
            if (mode == 1) sh_puts(1, "file\n");
            else if (mode == 2 || mode == 4) sh_printf(1, "%s\n", p);
            else sh_printf(1, "%s is %s\n", name, p);
        }
        sl_free(&paths);
    }
    if (!found && mode != 2 && mode != 1 && mode != 4) sh_cmd_error(mode == 3 ? "command" : "type", "%s: not found", name);
    return found ? 0 : 1;
}

static int bi_type(int argc, char **argv)
{
    int i = 1, mode = 0, all = 0, status = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *p;
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 't') mode = 1;
            else if (*p == 'a') all = 1;
            else if (*p == 'p' || *p == 'P') mode = 4;
            else if (*p == 'f') ;
            else {
                sh_cmd_error("type", "-%c: invalid option", *p);
                return 2;
            }
        }
    }
    for (; i < argc; i++)
        if (describe(argv[i], mode, all)) status = 1;
    return status;
}

static int bi_command(int argc, char **argv)
{
    int i = 1, mode = -1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "-v") == 0) mode = 2;
        else if (strcmp(argv[i], "-V") == 0) mode = 3;
        else if (strcmp(argv[i], "-p") == 0) ;
        else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            sh_cmd_error("command", "%s: invalid option", argv[i]);
            return 2;
        }
    }
    if (i >= argc) return 0;
    if (mode >= 0) {
        int status = 0;
        for (; i < argc; i++)
            if (describe(argv[i], mode, 0)) status = 1;
        return status;
    }
    /* run skipping functions */
    {
        const Builtin *b = builtin_lookup(argv[i]);
        char *path;
        int r;
        if (b) return b->fn(argc - i, argv + i);
        path = find_command(argv[i]);
        if (!path) {
            sh_error("%s: command not found", argv[i]);
            return 127;
        }
        r = exec_external(argc - i, argv + i, path);
        xfree(path);
        return r;
    }
}

static int bi_builtin(int argc, char **argv)
{
    const Builtin *b;
    if (argc < 2) return 0;
    b = builtin_lookup(argv[1]);
    if (!b) {
        sh_cmd_error("builtin", "%s: not a shell builtin", argv[1]);
        return 1;
    }
    return b->fn(argc - 1, argv + 1);
}

static int bi_hash(int argc, char **argv)
{
    int i = 1, status = 0;
    if (argc == 1) {
        hash_list(1);
        return 0;
    }
    if (strcmp(argv[1], "-r") == 0) {
        hash_clear();
        return 0;
    }
    for (; i < argc; i++) {
        char *p = find_command(argv[i]);
        if (!p) {
            sh_cmd_error("hash", "%s: not found", argv[i]);
            status = 1;
        }
        xfree(p);
    }
    return status;
}

static int bi_which(int argc, char **argv)
{
    int i, status = 0, all = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            all = 1;
            continue;
        }
        if (func_lookup(argv[i]) || builtin_lookup(argv[i])) {
            if (builtin_lookup(argv[i]) && !func_lookup(argv[i])) sh_printf(1, "%s: shell built-in command\n", argv[i]);
            else sh_printf(1, "%s: shell function\n", argv[i]);
            if (!all) continue;
        }
        {
            char *p = find_command(argv[i]);
            if (p) {
                sh_printf(1, "%s\n", p);
                xfree(p);
            } else if (!func_lookup(argv[i]) && !builtin_lookup(argv[i])) {
                status = 1;
            }
        }
    }
    return status;
}

static int bi_enable(int argc, char **argv)
{
    int i = 1, on = 1, all = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strchr(argv[i], 'n')) on = 0;
        if (strchr(argv[i], 'a')) all = 1;
    }
    if (i >= argc) {
        const Builtin *t;
        for (t = shell_builtins; t->name; t++)
            if (all || (builtin_lookup(t->name) != NULL) == on) sh_printf(1, "enable %s%s\n", builtin_lookup(t->name) ? "" : "-n ", t->name);
        for (t = util_builtins; t->name; t++)
            if (all || (builtin_lookup(t->name) != NULL) == on) sh_printf(1, "enable %s%s\n", builtin_lookup(t->name) ? "" : "-n ", t->name);
        return 0;
    }
    for (; i < argc; i++) builtin_set_enabled(argv[i], on);
    return 0;
}

/* ------------------------------------------------------------------ */
/* let / getopts / trap / misc                                          */

static int bi_let(int argc, char **argv)
{
    ShInt v = 0;
    int i;
    if (argc < 2) {
        sh_cmd_error("let", "expression expected");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        int r;
        arith_errprefix = "let";
        r = arith_eval(argv[i], &v);
        arith_errprefix = NULL;
        if (r) return 1;
    }
    return v == 0;
}

static int getopts_sp = 1;

static int bi_getopts(int argc, char **argv)
{
    const char *opts, *name;
    char **args;
    int nargs, optind, silent, c;
    const char *oi;
    ShInt v;
    char cs[2];
    if (argc < 3) {
        sh_puts(2, "getopts: usage: getopts optstring name [arg ...]\n");
        return 2;
    }
    opts = argv[1];
    name = argv[2];
    if (argc > 3) {
        args = argv + 3;
        nargs = argc - 3;
    } else {
        args = sh_params.v;
        nargs = sh_params.n;
    }
    oi = var_get("OPTIND");
    if (!oi || !str_toint(oi, &v) || v < 1) v = 1;
    optind = (int)v;
    {
        static int last_optind = 1;
        if (optind == 1 && last_optind != 1) getopts_sp = 1;
        if (optind != last_optind) getopts_sp = 1;
        last_optind = optind;
    }
    silent = opts[0] == ':';
    if (silent) opts++;
    if (optind > nargs || args[optind - 1][0] != '-' || args[optind - 1][1] == 0) {
        var_set(name, "?", 0);
        var_unset("OPTARG");
        var_set_int("OPTIND", optind);
        return 1;
    }
    if (strcmp(args[optind - 1], "--") == 0) {
        var_set_int("OPTIND", optind + 1);
        var_set(name, "?", 0);
        var_unset("OPTARG");
        return 1;
    }
    c = (unsigned char)args[optind - 1][getopts_sp];
    cs[0] = (char)c;
    cs[1] = 0;
    {
        const char *cp = strchr(opts, c);
        int advance = 0;
        if (args[optind - 1][getopts_sp + 1] == 0) advance = 1;
        if (c == ':' || !cp) {
            if (silent) {
                var_set("OPTARG", cs, 0);
            } else {
                sh_printf(2, "%s: illegal option -- %c\n", sh.name ? sh.name : "bash", c);
                var_unset("OPTARG");
            }
            var_set(name, "?", 0);
            if (advance) {
                getopts_sp = 1;
                optind++;
            } else {
                getopts_sp++;
            }
        } else if (cp[1] == ':') {
            if (args[optind - 1][getopts_sp + 1]) {
                var_set("OPTARG", args[optind - 1] + getopts_sp + 1, 0);
                optind++;
            } else if (optind < nargs) {
                var_set("OPTARG", args[optind], 0);
                optind += 2;
            } else {
                if (silent) {
                    var_set(name, ":", 0);
                    var_set("OPTARG", cs, 0);
                } else {
                    sh_printf(2, "%s: option requires an argument -- %c\n", sh.name ? sh.name : "bash", c);
                    var_set(name, "?", 0);
                    var_unset("OPTARG");
                }
                optind++;
                getopts_sp = 1;
                var_set_int("OPTIND", optind);
                return 0;
            }
            getopts_sp = 1;
            var_set(name, cs, 0);
        } else {
            var_set(name, cs, 0);
            var_unset("OPTARG");
            if (advance) {
                getopts_sp = 1;
                optind++;
            } else {
                getopts_sp++;
            }
        }
    }
    var_set_int("OPTIND", optind);
    {
        static int *lp = NULL;
        (void)lp;
    }
    return 0;
}

static const char *const signames[NSIGS] = {
    "EXIT", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL",
    "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT",
    "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF",
    "WINCH", "RETURN", "ERR", "DEBUG"
};

static int sig_number(const char *s)
{
    int i;
    if (str_isnum(s)) {
        i = atoi(s);
        return i >= 0 && i < 29 ? i : -1;
    }
    if (str_ncasecmp(s, "SIG", 3) == 0) s += 3;
    for (i = 0; i < NSIGS; i++)
        if (str_casecmp(s, signames[i]) == 0) return i;
    return -1;
}

static int bi_trap(int argc, char **argv)
{
    int i = 1, k;
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        for (k = 1; k < 29; k++) sh_printf(1, "%2d) SIG%s%c", k, signames[k], k % 5 == 0 ? '\n' : '\t');
        sh_putc(1, '\n');
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "-p") == 0) i++;
    if (argc - i == 0 || i == 2) {
        int only = i == 2 && argc > 2;
        for (k = 0; k < NSIGS; k++) {
            if (only) {
                int j, want = 0;
                for (j = 2; j < argc; j++)
                    if (sig_number(argv[j]) == k) want = 1;
                if (!want) continue;
            }
            if (sh.traps[k].action || sh.ptraps[k].action) {
                char *q = shell_quote(sh.traps[k].action ? sh.traps[k].action : sh.ptraps[k].action);
                int plain = k == TRAP_EXIT || k == TRAP_ERR || k == TRAP_DEBUG || k == TRAP_RETURN;
                sh_printf(1, "trap -- %s %s%s\n", q, plain ? "" : "SIG", signames[k]);
                xfree(q);
            }
        }
        return 0;
    }
    if (strcmp(argv[i], "--") == 0) i++;
    {
        const char *action = argv[i];
        int reset = 0, status = 0;
        if (argc - i == 1 || strcmp(action, "-") == 0) reset = 1;
        else if (sig_number(action) >= 0 && argc - i == 1) reset = 1;
        if (argc - i == 1 && sig_number(action) >= 0) {
            /* trap SIG : reset */
            k = sig_number(action);
            if (sh.traps[k].action && sh.subshell == 0) xfree(sh.traps[k].action);
            sh.traps[k].action = NULL;
            sh.ptraps[k].action = NULL;
            return 0;
        }
        for (i++; i < argc; i++) {
            k = sig_number(argv[i]);
            if (k < 0) {
                sh_cmd_error("trap", "%s: invalid signal specification", argv[i]);
                status = 1;
                continue;
            }
            if (sh.traps[k].action && sh.subshell == 0) xfree(sh.traps[k].action);
            sh.traps[k].action = reset ? NULL : xstrdup(action);
            sh.ptraps[k].action = NULL;
        }
        return status;
    }
}

/* background jobs other than external commands ran synchronously, so
 * wait only reports their remembered status */
static int bi_wait(int argc, char **argv)
{
    int i, st = 0;
    for (i = 1; i < argc; i++) {
        ShInt id;
        const char *a = argv[i];
        if (strcmp(a, "-n") == 0 || strcmp(a, "-f") == 0) continue;
        if (a[0] == '%') {
            st = 0;
            continue;
        }
        if (!str_toint(a, &id)) {
            sh_cmd_error("wait", "`%s': not a pid or valid job spec", a);
            st = 2;
            continue;
        }
        st = bg_status((long)id);
        if (st < 0) {
            if (id == sh.lastbg) st = 0;      /* real background process */
            else {
                sh_cmd_error("wait", "pid %s is not a child of this shell", a);
                st = 127;
            }
        }
    }
    return st;
}

static int bi_jobs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

static int bi_fg(int argc, char **argv)
{
    (void)argc;
    sh_cmd_error(argv[0], "no job control in this shell");
    return 1;
}

static int bi_kill(int argc, char **argv)
{
    if (argc > 1 && (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "-L") == 0)) {
        int k, i;
        if (argc == 2) {
            for (k = 1; k < 29; k++) sh_printf(1, "%s%c", signames[k], k % 10 == 0 ? '\n' : ' ');
            sh_putc(1, '\n');
            return 0;
        }
        for (i = 2; i < argc; i++) {
            ShInt n;
            if (str_toint(argv[i], &n)) {
                if (n > 128) n -= 128;
                if (n < 1 || n >= 29) {
                    sh_cmd_error("kill", "%s: invalid signal specification", argv[i]);
                    return 1;
                }
                sh_printf(1, "%s\n", signames[n]);
            } else {
                k = sig_number(argv[i]);
                if (k < 1 || k >= 29) {
                    sh_cmd_error("kill", "%s: invalid signal specification", argv[i]);
                    return 1;
                }
                sh_printf(1, "%d\n", k);
            }
        }
        return 0;
    }
    if (argc > 2 && (strcmp(argv[1], "-0") == 0 || (strcmp(argv[1], "-s") == 0 && strcmp(argv[2], "0") == 0))) {
        /* kill -0 pid: does the process exist?  Only our own (and our
         * finished emulated jobs) are known */
        int i, status = 0;
        for (i = argv[1][1] == 's' ? 3 : 2; i < argc; i++) {
            ShInt n;
            if (!str_toint(argv[i], &n) || (n != sh.pid && bg_status((long)n) < 0 && n != sh.lastbg)) {
                sh_cmd_error("kill", "(%s) - No such process", argv[i]);
                status = 1;
            }
        }
        return status;
    }
#ifdef AMIGA
    {
        /* kill N : send Ctrl-C to CLI process N (like the Amiga "Break" command) */
        extern int os_break_cli(long n);
        int i, status = 0;
        for (i = 1; i < argc; i++) {
            ShInt n;
            if (argv[i][0] == '-') continue;
            if (!str_toint(argv[i][0] == '%' ? argv[i] + 1 : argv[i], &n) || os_break_cli((long)n)) {
                sh_cmd_error("kill", "(%s) - No such process", argv[i]);
                status = 1;
            }
        }
        return status;
    }
#else
    sh_cmd_error("kill", "not supported");
    return 1;
#endif
}

/* the file creation mask is only remembered (AmigaOS has none); it is
 * kept in sh so that subshells restore it */
#define umask_val sh.umask

static int bi_umask(int argc, char **argv)
{
    int i = 1, sym = 0, pr = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "-S") == 0) sym = 1;
        else if (strcmp(argv[i], "-p") == 0) pr = 1;
        else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            sh_cmd_error("umask", "%s: invalid option", argv[i]);
            return 2;
        }
    }
    if (i >= argc) {
        if (sym) {
            static const char *const who[3] = {"u", "g", "o"};
            int k;
            SBuf b;
            sb_init(&b);
            if (pr) sb_adds(&b, "umask -S ");
            for (k = 0; k < 3; k++) {
                int bits = (~umask_val >> (6 - 3 * k)) & 7;
                if (k) sb_addc(&b, ',');
                sb_printf(&b, "%s=", who[k]);
                if (bits & 4) sb_addc(&b, 'r');
                if (bits & 2) sb_addc(&b, 'w');
                if (bits & 1) sb_addc(&b, 'x');
            }
            sb_addc(&b, '\n');
            sh_write(1, b.s, b.len);
            sb_free(&b);
        } else {
            sh_printf(1, "%s%04o\n", pr ? "umask " : "", umask_val);
        }
        return 0;
    }
    {
        const char *a = argv[i];
        int v = 0;
        if (*a >= '0' && *a <= '7') {
            for (; *a; a++) {
                if (*a < '0' || *a > '7') {
                    sh_cmd_error("umask", "%s: octal number out of range", argv[i]);
                    return 1;
                }
                v = v * 8 + (*a - '0');
            }
            umask_val = v & 0777;
            return 0;
        }
        /* symbolic: u=rwx,g=rx,o= (only "=" assignments and +/-) */
        {
            int perm = ~umask_val & 0777;
            while (*a) {
                int whom = 0, op, bits = 0, k;
                while (*a && strchr("ugoa", *a)) {
                    if (*a == 'u') whom |= 0700;
                    else if (*a == 'g') whom |= 070;
                    else if (*a == 'o') whom |= 07;
                    else whom |= 0777;
                    a++;
                }
                if (!whom) whom = 0777;
                op = *a;
                if (op != '=' && op != '+' && op != '-') {
                    sh_cmd_error("umask", "`%c': invalid symbolic mode operator", op ? op : ' ');
                    return 1;
                }
                a++;
                while (*a && strchr("rwx", *a)) {
                    k = *a == 'r' ? 4 : *a == 'w' ? 2 : 1;
                    bits |= k | (k << 3) | (k << 6);
                    a++;
                }
                bits &= whom;
                if (op == '=') perm = (perm & ~whom) | bits;
                else if (op == '+') perm |= bits;
                else perm &= ~bits;
                if (*a == ',') a++;
            }
            umask_val = ~perm & 0777;
        }
    }
    return 0;
}

static int bi_ulimit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_puts(1, "unlimited\n");
    return 0;
}

static int bi_times(int argc, char **argv)
{
    long s = os_time() - sh.starttime;
    (void)argc;
    (void)argv;
    sh_printf(1, "0m%lds 0m0.000s\n0m0.000s 0m0.000s\n", s);
    return 0;
}

static int bi_history(int argc, char **argv)
{
    int i, n = hist_count(), start = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "-c") == 0) {
            hist_clear();
            return 0;
        }
        if (strcmp(argv[1], "-d") == 0 && argc > 2) {
            int k = atoi(argv[2]) - hist_base();
            if (k < 0 || k >= n) {
                sh_cmd_error("history", "%s: history position out of range", argv[2]);
                return 1;
            }
            hist_delete(k);
            return 0;
        }
        if (strcmp(argv[1], "-w") == 0 || strcmp(argv[1], "-a") == 0) {
            const char *f = argc > 2 ? argv[2] : var_get("HISTFILE");
            if (f) hist_save(f);
            return 0;
        }
        if (strcmp(argv[1], "-r") == 0) {
            const char *f = argc > 2 ? argv[2] : var_get("HISTFILE");
            if (f) hist_load(f);
            return 0;
        }
        if (str_isnum(argv[1])) {
            start = n - atoi(argv[1]);
            if (start < 0) start = 0;
        }
    }
    for (i = start; i < n; i++) sh_printf(1, "%5d  %s\n", hist_base() + i, hist_get(i));
    return 0;
}

static int bi_help(int argc, char **argv)
{
    const Builtin *t;
    int col = 0;
    if (argc > 1) {
        int found = 0;
        for (t = shell_builtins; t->name; t++)
            if (pat_match(argv[1], t->name, 0)) {
                sh_printf(1, "%s: %s\n", t->name, t->usage ? t->usage : t->name);
                found = 1;
            }
        for (t = util_builtins; t->name; t++)
            if (pat_match(argv[1], t->name, 0)) {
                sh_printf(1, "%s: %s\n", t->name, t->usage ? t->usage : t->name);
                found = 1;
            }
        if (!found) {
            sh_cmd_error("help", "no help topics match `%s'.", argv[1]);
            return 1;
        }
        return 0;
    }
    sh_printf(1, "AmiBash, version %s (bash-compatible shell for AmigaOS)\n", AMIBASH_VERSION);
    sh_puts(1, "These shell commands are defined internally.  Type `help name' to find out\n"
               "more about the function `name'.  Amiga commands in C: and the PATH are run\n"
               "normally; paths may be written DH0:dir/file or /dh0/dir/file.\n\n");
    sh_puts(1, "Shell builtins:\n");
    for (t = shell_builtins; t->name; t++) {
        sh_printf(1, "%-12s", t->name);
        if (++col % 6 == 0) sh_putc(1, '\n');
    }
    if (col % 6) sh_putc(1, '\n');
    col = 0;
    sh_puts(1, "\nBuilt-in utilities (disable with `enable -n NAME' to use an external one):\n");
    for (t = util_builtins; t->name; t++) {
        sh_printf(1, "%-12s", t->name);
        if (++col % 6 == 0) sh_putc(1, '\n');
    }
    if (col % 6) sh_putc(1, '\n');
    return 0;
}

static int bi_caller(int argc, char **argv)
{
    ShInt n = 0;
    int line;
    const char *file;
    if (argc > 1 && !str_toint(argv[1], &n)) {
        sh_cmd_error("caller", "%s: invalid number", argv[1]);
        return 2;
    }
    if (sh.funcdepth == 0) {
        if (argc > 1 || !sh.script) return 1;
        sh_puts(1, "0 NULL\n");
        return 0;
    }
    if (!caller_frame((int)n, &line, &file)) return 1;
    if (argc > 1) {
        Var *fn = var_lookup("FUNCNAME");
        char *k = str_itoa(n + 1);
        const char *name = fn ? var_arr_get(fn, k) : NULL;
        xfree(k);
        sh_printf(1, "%d %s %s\n", line, name ? name : "main", file);
    } else {
        sh_printf(1, "%d %s\n", line, file);
    }
    return 0;
}

static int bi_complete(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

static int bi_compgen(int argc, char **argv)
{
    int i = 1;
    const char *word = "";
    int mode = 0;   /* 1 cmds 2 files 3 vars 4 funcs 5 aliases 6 builtins 7 dirs, 8 wordlist */
    const char *wl = NULL;
    StrList out;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "-c") == 0) mode = 1;
        else if (strcmp(argv[i], "-k") == 0) mode = 9;
        else if (strcmp(argv[i], "-f") == 0) mode = 2;
        else if (strcmp(argv[i], "-v") == 0) mode = 3;
        else if (strcmp(argv[i], "-a") == 0) mode = 5;
        else if (strcmp(argv[i], "-b") == 0) mode = 6;
        else if (strcmp(argv[i], "-d") == 0) mode = 7;
        else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) mode = 8, wl = argv[++i];
        else if (strcmp(argv[i], "-A") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "function") == 0) mode = 4;
            else if (strcmp(argv[i], "variable") == 0) mode = 3;
            else if (strcmp(argv[i], "command") == 0) mode = 1;
            else if (strcmp(argv[i], "alias") == 0) mode = 5;
            else if (strcmp(argv[i], "builtin") == 0) mode = 6;
            else if (strcmp(argv[i], "directory") == 0) mode = 7;
            else if (strcmp(argv[i], "file") == 0) mode = 2;
            else if (strcmp(argv[i], "keyword") == 0) mode = 9;
        }
    }
    if (i < argc) word = argv[i];
    sl_init(&out);
    switch (mode) {
    case 3: var_names(&out, word); break;
    case 4: func_names(&out, word); break;
    case 5: alias_names(&out, word); break;
    case 6: builtin_names(&out, word); break;
    case 8: {
        /* the word list is expanded, then split at $IFS */
        char *e = expand_str(wl, 0);
        const char *ifs = var_get("IFS");
        char *p, *q;
        if (!ifs) ifs = " \t\n";
        for (p = e ? e : ""; *p;) {
            while (*p && strchr(ifs, *p)) p++;
            if (!*p) break;
            for (q = p; *q && !strchr(ifs, *q); q++)
                ;
            {
                char *w = xstrndup(p, q - p);
                if (str_startswith(w, word)) sl_add(&out, w);
                else xfree(w);
            }
            p = q;
        }
        xfree(e);
        break;
    }
    case 9: {
        static const char *const kw[] = {
            "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
            "while", "until", "do", "done", "in", "function", "time", "{", "}",
            "!", "[[", "]]", "coproc", NULL
        };
        int k;
        for (k = 0; kw[k]; k++)
            if (str_startswith(kw[k], word)) sl_adddup(&out, kw[k]);
        break;
    }
    default: {
        int isfile = 0;
        char *line = xstrdup(word);
        int k;
        if (mode == 1) {
            complete_word(line, 0, (int)strlen(line), &out, &isfile);
        } else {
            char *l2 = str_cat("x ", word);
            complete_word(l2, 2, (int)strlen(l2), &out, &isfile);
            xfree(l2);
            if (mode == 7) {
                for (k = 0; k < out.n; k++) {
                    size_t L = strlen(out.v[k]);
                    if (L && out.v[k][L - 1] == '/') out.v[k][L - 1] = 0;
                    else {
                        xfree(out.v[k]);
                        out.v[k] = xstrdup("");
                    }
                }
            }
        }
        xfree(line);
    }
    }
    {
        int k;
        for (k = 0; k < out.n; k++)
            if (*out.v[k]) sh_printf(1, "%s\n", out.v[k]);
    }
    {
        int n = out.n;
        sl_free(&out);
        return n ? 0 : 1;
    }
}

static int bi_ados(int argc, char **argv)
{
    /* run a command line through the native AmigaDOS shell */
    extern int os_run_native(const char *cmdline, OsHandle in, OsHandle out);
    char *line;
    int r;
    ShFile *f0 = sh_fd(0), *f1 = sh_fd(1);
    OsHandle in, out;
    if (argc < 2) {
        sh_puts(2, "usage: ados command-line   (run via the AmigaDOS shell)\n");
        return 2;
    }
    line = join_args(argc - 1, argv + 1);
    sh_flush_all();
    in = shf_os_handle(f0, 0);
    out = shf_os_handle(f1, 1);
    r = os_run_native(line, in, out);
    shf_os_done(f0, 0);
    shf_os_done(f1, 1);
    xfree(line);
    return r;
}

/* showkey: print the bytes each key sends (to diagnose terminals) */
static int bi_showkey(int argc, char **argv)
{
    OsHandle in = os_stdin();
    int idle = 0;
    (void)argc;
    (void)argv;
    if (!os_isatty(in)) {
        sh_cmd_error("showkey", "stdin is not a console");
        return 1;
    }
    sh_puts(1, "Press keys to see their codes; 'q' or 10 s idle quits.\n");
    sh_flush_all();
    if (os_rawmode(in, 1) != 0) {
        sh_cmd_error("showkey", "console does not support raw mode (no line editing possible)");
        return 1;
    }
    for (;;) {
        unsigned char buf[16];
        long n, i;
        if (!os_waitchar(in, 1000000L)) {
            if (++idle >= 10) break;
            continue;
        }
        idle = 0;
        n = os_read(in, buf, 1);
        /* collect the rest of an escape sequence */
        while (n > 0 && n < (long)sizeof(buf) && os_waitchar(in, 50000L)) {
            long r = os_read(in, buf + n, 1);
            if (r != 1) break;
            n++;
        }
        if (n <= 0) break;
        {
            SBuf b;
            sb_init(&b);
            for (i = 0; i < n; i++) sb_printf(&b, "%s%02X", i ? " " : "", buf[i]);
            sb_adds(&b, "   ");
            for (i = 0; i < n; i++) {
                if (buf[i] == 0x9B) sb_adds(&b, "CSI ");
                else if (buf[i] == 27) sb_adds(&b, "ESC ");
                else if (buf[i] < 32) sb_printf(&b, "^%c ", buf[i] + 64);
                else sb_printf(&b, "%c", buf[i]);
            }
            sb_adds(&b, "\r\n");
            os_write(os_stdout(), b.s, b.len);
            sb_free(&b);
        }
        if (n == 1 && buf[0] == 'q') break;
    }
    os_rawmode(in, 0);
    return 0;
}

/* setfont [NAME SIZE]: show or change the console window's font */
static int bi_setfont(int argc, char **argv)
{
    char name[64];
    int size, r;
    if (argc == 1) {
        if (os_console_getfont(name, sizeof(name), &size) != 0) {
            sh_cmd_error("setfont", "no console window");
            return 1;
        }
        sh_printf(1, "%s %d\n", name, size);
        return 0;
    }
    if (argc != 3 || !str_isnum(argv[2])) {
        sh_puts(2, "setfont: usage: setfont [fontname size]   e.g. setfont topaz 8\n");
        return 2;
    }
    r = os_console_setfont(argv[1], atoi(argv[2]));
    if (r == -2) {
        sh_cmd_error("setfont", "%s %s: not a fixed-width font", argv[1], argv[2]);
        return 1;
    }
    if (r != 0) {
        sh_cmd_error("setfont", "%s %s: %s", argv[1], argv[2], os_errstr());
        return 1;
    }
    return 0;
}

static int parse_rgb(const char *s, unsigned long *rgb)
{
    char *end;
    size_t n;
    if (*s == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    n = strlen(s);
    if (n != 3 && n != 6) return 0;
    *rgb = strtoul(s, &end, 16);
    if (*end) return 0;
    if (n == 3) /* RGB4 shorthand, Amiga style */
        *rgb = ((*rgb >> 8) & 15) * 0x110000UL + ((*rgb >> 4) & 15) * 0x1100UL + (*rgb & 15) * 0x11UL;
    return 1;
}

/* palette [PEN [#RRGGBB|#RGB]]: list or set the screen colours used by
 * ANSI colour codes (ESC[30m..37m use pens 0..7) */
static int bi_palette(int argc, char **argv)
{
    int n = os_console_pens(), i;
    unsigned long rgb;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-l") == 0)) {
        if (n < 0) {
            sh_cmd_error("palette", "no console screen");
            return 1;
        }
        for (i = 0; i < n && i < 32; i++) {
            if (os_console_getpen(i, &rgb) != 0) {
                sh_cmd_error("palette", "cannot read the palette here");
                return 1;
            }
            sh_printf(1, "pen %2d  #%06lX  \033[%dm sample \033[0m\n", i, rgb, i < 8 ? 30 + i : 39);
        }
        return 0;
    }
    if (argc != 3 || !str_isnum(argv[1]) || !parse_rgb(argv[2], &rgb)) {
        sh_puts(2, "palette: usage: palette [pen RRGGBB]   e.g. palette 3 ff8800  (quote a leading #)\n");
        return 2;
    }
    if (os_console_setpen(atoi(argv[1]), rgb) != 0) {
        sh_cmd_error("palette", "pen %s: cannot set", argv[1]);
        return 1;
    }
    return 0;
}

/* bashwin: open another Bash window; bashwin -n: go to the next one */
static int bi_bashwin(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        if (os_next_shell_window() != 0) {
            sh_cmd_error("bashwin", "no other Bash window");
            return 1;
        }
        return 0;
    }
    if (argc > 1) {
        sh_puts(2, "bashwin: usage: bashwin [-n]   (new window / next window)\n");
        return 2;
    }
    if (os_new_shell_window() != 0) {
        sh_cmd_error("bashwin", "%s", os_errstr());
        return 1;
    }
    return 0;
}

static int bi_logout(int argc, char **argv)
{
    return bi_exit(argc, argv);
}

/* ------------------------------------------------------------------ */

const Builtin shell_builtins[] = {
    {":", bi_true, BI_SPECIAL, ": [arguments]  - null command, returns success"},
    {".", bi_source, BI_SPECIAL, ". filename [arguments]  - execute commands from a file"},
    {"[", bi_test, 0, "[ arg... ]  - evaluate conditional expression"},
    {"ados", bi_ados, 0, "ados command-line  - run a command line through the AmigaDOS shell"},
    {"alias", bi_alias, 0, "alias [-p] [name[=value] ... ]"},
    {"bashwin", bi_bashwin, 0, "bashwin [-n]  - open a new Bash window (F1); -n: next window (F2)"},
    {"bg", bi_fg, 0, "bg [job_spec]  - not supported (no job control)"},
    {"bash", run_child_shell, 0, "bash [-ceux] [-c command | script] [args]  - run a child shell (in-process)"},
    {"break", bi_break, BI_SPECIAL, "break [n]"},
    {"builtin", bi_builtin, 0, "builtin [shell-builtin [arg ...]]"},
    {"caller", bi_caller, 0, "caller [expr]"},
    {"cd", bi_cd, 0, "cd [dir]  - change directory (DH0:dir, /dh0/dir, .., ~, -)"},
    {"command", bi_command, 0, "command [-pVv] command [arg ...]"},
    {"compgen", bi_compgen, 0, "compgen [-abcdfv] [-A action] [-W wordlist] [word]"},
    {"complete", bi_complete, 0, "complete ...  - accepted, ignored"},
    {"continue", bi_continue, BI_SPECIAL, "continue [n]"},
    {"declare", bi_declare, 0, "declare [-aAfFgilnrtux] [-p] [name[=value] ...]"},
    {"dirs", bi_dirs, 0, "dirs [-clpv]"},
    {"echo", bi_echo, 0, "echo [-neE] [arg ...]"},
    {"enable", bi_enable, 0, "enable [-a] [-n] [name ...]"},
    {"eval", bi_eval, BI_SPECIAL, "eval [arg ...]"},
    {"exec", bi_exec, BI_SPECIAL, "exec [command [arguments ...]] [redirection ...]"},
    {"exit", bi_exit, BI_SPECIAL, "exit [n]"},
    {"export", bi_export, BI_SPECIAL, "export [-fn] [name[=value] ...] or export -p"},
    {"false", bi_false, 0, "false  - return an unsuccessful result"},
    {"fg", bi_fg, 0, "fg [job_spec]  - not supported (no job control)"},
    {"getopts", bi_getopts, 0, "getopts optstring name [arg ...]"},
    {"hash", bi_hash, 0, "hash [-r] [name ...]"},
    {"help", bi_help, 0, "help [pattern ...]"},
    {"history", bi_history, 0, "history [-c] [-d offset] [n] or history -rw [filename]"},
    {"jobs", bi_jobs, 0, "jobs  - lists nothing (no job control)"},
    {"kill", bi_kill, 0, "kill N  - send Ctrl-C to Amiga CLI process N"},
    {"let", bi_let, 0, "let arg [arg ...]"},
    {"local", bi_local, 0, "local [option] name[=value] ..."},
    {"logout", bi_logout, 0, "logout [n]"},
    {"mapfile", bi_mapfile, 0, "mapfile [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd] [array]"},
    {"palette", bi_palette, 0, "palette [pen #RRGGBB]  - list/set the screen colours (Amiga)"},
    {"popd", bi_popd, 0, "popd"},
    {"printf", bi_printf, 0, "printf [-v var] format [arguments]"},
    {"pushd", bi_pushd, 0, "pushd [dir]"},
    {"pwd", bi_pwd, 0, "pwd [-LP]"},
    {"read", bi_read, 0, "read [-ers] [-a array] [-d delim] [-n nchars] [-N nchars] [-p prompt] [-t timeout] [-u fd] [name ...]"},
    {"readarray", bi_mapfile, 0, "readarray [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd] [array]"},
    {"readonly", bi_readonly, BI_SPECIAL, "readonly [-aAf] [name[=value] ...] or readonly -p"},
    {"return", bi_return, BI_SPECIAL, "return [n]"},
    {"set", bi_set, BI_SPECIAL, "set [-abefhkmnptuvxBCHP] [-o option-name] [--] [arg ...]"},
    {"setfont", bi_setfont, 0, "setfont [name size]  - show/change the console font (Amiga)"},
    {"sh", run_child_shell, 0, "sh [-ceux] [-c command | script] [args]  - same as bash"},
    {"showkey", bi_showkey, 0, "showkey  - show the codes keys send (diagnose cursor keys)"},
    {"shift", bi_shift, BI_SPECIAL, "shift [n]"},
    {"shopt", bi_shopt, 0, "shopt [-pqsu] [-o] [optname ...]"},
    {"source", bi_source, 0, "source filename [arguments]"},
    {"test", bi_test, 0, "test [expr]"},
    {"times", bi_times, BI_SPECIAL, "times"},
    {"trap", bi_trap, BI_SPECIAL, "trap [-lp] [[arg] signal_spec ...]"},
    {"true", bi_true, 0, "true  - return a successful result"},
    {"type", bi_type, 0, "type [-afptP] name [name ...]"},
    {"typeset", bi_declare, 0, "typeset [-aAfFgilnrtux] [-p] name[=value] ..."},
    {"ulimit", bi_ulimit, 0, "ulimit  - reports unlimited"},
    {"umask", bi_umask, 0, "umask  - reports 0022"},
    {"unalias", bi_unalias, 0, "unalias [-a] name [name ...]"},
    {"unset", bi_unset, BI_SPECIAL, "unset [-f] [-v] [-n] [name ...]"},
    {"wait", bi_wait, 0, "wait  - returns immediately"},
    {"which", bi_which, 0, "which [-a] name ...  - locate a command"},
    {NULL, NULL, 0, NULL}
};
