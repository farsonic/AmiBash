/*
 * utils_fs.c - file system utilities: ls cp mv rm mkdir rmdir touch chmod
 * find du df ln realpath stat.
 *
 * All paths are shell paths and go through the path.c wrappers, so both
 * "DH0:dir/file" and "/dh0/dir/file" work on the Amiga.
 */
#include <string.h>
#include <ctype.h>
#include "sh.h"
#include "utils.h"

#ifdef AMIGA
#define NAMECMP str_casecmp
#else
#define NAMECMP strcmp
#endif

/* "dir/" -> "dir", "DH0:" -> "DH0"; malloc'd final component */
static char *base_of(const char *p)
{
    char *s = xstrdup(p), *r;
    int n = (int)strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = 0;
    if (n > 1 && s[n - 1] == ':') s[--n] = 0;
    r = xstrdup(path_basename(s));
    xfree(s);
    return r;
}

static void prot_str(unsigned long prot, char *out)
{
    static const char letters[] = "hsparwed";
    static const unsigned long bits[] = { OS_P_HIDDEN, OS_P_SCRIPT, OS_P_PURE, OS_P_ARCH,
                                          OS_P_READ, OS_P_WRITE, OS_P_EXEC, OS_P_DELETE };
    int i;
    for (i = 0; i < 8; i++) out[i] = (prot & bits[i]) ? letters[i] : '-';
    out[8] = 0;
}

/* does this look like a program?  On the Amiga nearly every file has the
 * e bit, so only files without an extension (or with the s bit) count */
static int is_exec(const char *name, int type, unsigned long prot)
{
    if (type != OS_T_FILE) return 0;
#ifdef AMIGA
    if (prot & OS_P_SCRIPT) return 1;
    if (!(prot & OS_P_EXEC)) return 0;
    {
        const char *dot = strrchr(name, '.');
        return dot == NULL || dot == name;
    }
#else
    (void)name;
    return (prot & OS_P_EXEC) != 0;
#endif
}

/* same object?  (needs both to exist) */
static int same_file(const char *a, const char *b)
{
    char *na = path_native(a), *nb = path_native(b);
    char *fa = os_fullpath(na), *fb = os_fullpath(nb);
    int r = fa && fb && NAMECMP(fa, fb) == 0;
    xfree(na);
    xfree(nb);
    xfree(fa);
    xfree(fb);
    return r;
}

/* is b inside directory a? */
static int inside_dir(const char *a, const char *b)
{
    char *na = path_native(a), *nb = path_native(b);
    char *fa = os_fullpath(na), *fb = NULL;
    int r = 0;
    if (fa) {
        char *bd = path_dirname(nb);
        char *fbd = os_fullpath(bd);
        if (fbd) {
            size_t la = strlen(fa);
            fb = fbd;
            if (str_ncasecmp(fa, fb, la) == 0 &&
                (fb[la] == 0 || fb[la] == '/' || (la && (fa[la - 1] == ':' || fa[la - 1] == '/'))))
                r = 1;
        }
        xfree(bd);
    }
    xfree(na);
    xfree(nb);
    xfree(fa);
    xfree(fb);
    return r;
}

/* ================================================================== */
/* ls                                                                  */
/* ================================================================== */

typedef struct LsEnt {
    char *name;
    char *comment;
    double size;
    long mtime;
    unsigned long prot;
    int type;
} LsEnt;

typedef struct LsOpt {
    int all, almost, dirself, classify, human, rev, recurse, bysize, bytime;
    int one, longf, color, cols, nosort, total_hdr;
    long now;
    int width;
} LsOpt;

static LsEnt *ls_ent(const char *name, OsDirEnt *e)
{
    LsEnt *l = xmalloc(sizeof(LsEnt));
    l->name = xstrdup(name);
    l->comment = e->comment[0] ? xstrdup(e->comment) : NULL;
    l->size = e->size;
    l->mtime = e->mtime;
    l->prot = e->prot;
    l->type = e->type;
    return l;
}

static void ls_free_ents(LsEnt **v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        xfree(v[i]->name);
        xfree(v[i]->comment);
        xfree(v[i]);
    }
    xfree(v);
}

static int ls_cmp(const void *pa, const void *pb, void *ud)
{
    const LsEnt *a = pa, *b = pb;
    const LsOpt *o = ud;
    int r = 0;
    if (o->bysize) {
        if (a->size != b->size) r = a->size > b->size ? -1 : 1;
    } else if (o->bytime) {
        if (a->mtime != b->mtime) r = a->mtime > b->mtime ? -1 : 1;
    }
    if (!r) r = str_casecmp(a->name, b->name);
    if (!r) r = strcmp(a->name, b->name);
    return o->rev ? -r : r;
}

static void ls_sort(LsEnt **v, int n, LsOpt *o)
{
    if (!o->nosort) u_msort((void **)v, n, ls_cmp, o);
}

static const char *ls_color(const LsEnt *e)
{
    /* 33 is pen 3 (blue) in the console; real ANSI blue in the terminal */
    if (e->type == OS_T_DIR) return var_get("AMIBASH_TERM") ? "\033[1;34m" : "\033[1;33m";
    if (e->type == OS_T_VOL) return "\033[1;32m";
    if (e->type == OS_T_LINK) return "\033[36m";
    if (is_exec(e->name, e->type, e->prot)) return "\033[1;31m";
    return NULL;
}

static int ls_indicator(const LsEnt *e)
{
    if (e->type == OS_T_DIR) return '/';
    if (e->type == OS_T_VOL) return ':';
    if (e->type == OS_T_LINK) return '@';
    if (is_exec(e->name, e->type, e->prot)) return '*';
    return 0;
}

static void ls_name(SBuf *b, const LsEnt *e, LsOpt *o)
{
    const char *col = o->color ? ls_color(e) : NULL;
    if (col) sb_adds(b, col);
    sb_adds(b, e->name);
    if (col) sb_adds(b, "\033[0m");
    if (o->classify) {
        int c = ls_indicator(e);
        if (c) sb_addc(b, c);
    }
}

static int ls_namelen(const LsEnt *e, LsOpt *o)
{
    return (int)strlen(e->name) + (o->classify && ls_indicator(e) ? 1 : 0);
}

static void ls_print(LsEnt **v, int n, LsOpt *o)
{
    SBuf b;
    int i;
    sb_init(&b);
    if (n == 0) return;
    if (o->longf) {
        int sw = 1;
        char **sizes = xmalloc(n * sizeof(char *));
        for (i = 0; i < n; i++) {
            int l;
            sizes[i] = v[i]->type == OS_T_VOL ? xstrdup("-") : u_size_str(v[i]->size, o->human);
            l = (int)strlen(sizes[i]);
            if (l > sw) sw = l;
        }
        for (i = 0; i < n; i++) {
            char pr[9], date[32];
            ShTm tm;
            LsEnt *e = v[i];
            int tc = e->type == OS_T_DIR ? 'd' : e->type == OS_T_VOL ? 'v' : e->type == OS_T_LINK ? 'l' : '-';
            prot_str(e->prot, pr);
            sh_localtime(e->mtime, &tm);
            if (e->mtime < o->now - 15778476L || e->mtime > o->now + 3600L)
                sh_strftime(date, sizeof(date), "%b %e  %Y", &tm);
            else
                sh_strftime(date, sizeof(date), "%b %e %H:%M", &tm);
            sb_clear(&b);
            sb_printf(&b, "%c%s %*s %s ", tc, pr, sw, sizes[i], date);
            ls_name(&b, e, o);
            if (e->comment) {
                sb_adds(&b, " : ");
                sb_adds(&b, e->comment);
            }
            sb_addc(&b, '\n');
            sh_write(1, b.s, b.len);
            xfree(sizes[i]);
            if (u_break()) break;
        }
        for (; i < n; i++) xfree(sizes[i]);
        xfree(sizes);
    } else if (o->one || !o->cols) {
        for (i = 0; i < n; i++) {
            sb_clear(&b);
            ls_name(&b, v[i], o);
            sb_addc(&b, '\n');
            sh_write(1, b.s, b.len);
        }
    } else {
        /* vertical columns, as many as fit */
        int *lens = xmalloc(n * sizeof(int));
        int *cw = xmalloc(n * sizeof(int));
        int ncols, rows = n, r, c;
        for (i = 0; i < n; i++) lens[i] = ls_namelen(v[i], o);
        for (ncols = n; ncols >= 1; ncols--) {
            int tot = 0, ok = 1;
            rows = (n + ncols - 1) / ncols;
            if ((rows - 1) * ncols >= n && ncols > 1) continue;
            for (c = 0; c < ncols; c++) {
                int w = 0;
                for (r = 0; r < rows; r++) {
                    int k = c * rows + r;
                    if (k < n && lens[k] > w) w = lens[k];
                }
                cw[c] = w;
                tot += w + (c < ncols - 1 ? 2 : 0);
                if (tot > o->width) {
                    ok = 0;
                    break;
                }
            }
            if (ok) break;
        }
        if (ncols < 1) {
            ncols = 1;
            rows = n;
            cw[0] = 0;
        }
        for (r = 0; r < rows; r++) {
            sb_clear(&b);
            for (c = 0; c < ncols; c++) {
                int k = c * rows + r, pad;
                if (k >= n) break;
                ls_name(&b, v[k], o);
                if (c < ncols - 1 && (c + 1) * rows + r < n) {
                    for (pad = lens[k]; pad < cw[c] + 2; pad++) sb_addc(&b, ' ');
                }
            }
            sb_addc(&b, '\n');
            sh_write(1, b.s, b.len);
        }
        xfree(lens);
        xfree(cw);
    }
    sb_free(&b);
}

static int ls_hidden(const OsDirEnt *e, LsOpt *o)
{
    if (o->all || o->almost) return 0;
    if (e->name[0] == '.') return 1;
    if (e->prot & OS_P_HIDDEN) return 1;
    return 0;
}

/* list one directory; returns status */
static int ls_dir(const char *cmd, const char *path, int header, int *first, LsOpt *o)
{
    void *d;
    OsDirEnt *e;
    LsEnt **v = NULL;
    int n = 0, cap = 0, i, rc = 0;
    d = sh_opendir(path);
    if (!d) {
        sh_cmd_error(cmd, "cannot open directory '%s': %s", path, os_errstr());
        return 2;
    }
    e = xmalloc(sizeof(OsDirEnt));
    while (os_readdir(d, e)) {
        if (ls_hidden(e, o)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            v = xrealloc(v, cap * sizeof(LsEnt *));
        }
        v[n++] = ls_ent(e->name, e);
        if (u_break()) break;
    }
    os_closedir(d);
    xfree(e);
    ls_sort(v, n, o);
    if (header) {
        if (!*first) sh_putc(1, '\n');
        /* "RAM:" is printed as is, not "RAM::" */
        sh_printf(1, "%s%s\n", path, str_endswith(path, ":") ? "" : ":");
    }
    *first = 0;
    if (o->longf && o->total_hdr) {
        double tot = 0;
        char *s;
        for (i = 0; i < n; i++) tot += v[i]->size;
        s = u_size_str(tot, o->human);
        sh_printf(1, "total %s\n", s);
        xfree(s);
    }
    ls_print(v, n, o);
    if (o->recurse) {
        for (i = 0; i < n && !u_break(); i++) {
            if (v[i]->type == OS_T_DIR || v[i]->type == OS_T_VOL) {
                char *p = path_join(path, v[i]->name);
                rc |= ls_dir(cmd, p, 1, first, o);
                xfree(p);
            }
        }
    }
    ls_free_ents(v, n);
    return rc;
}

int util_ls(int argc, char **argv)
{
    static const char *const lo[] = { "all", "almost-all", "directory", "classify", "human-readable",
                                      "reverse", "recursive", "color?", "colour?", NULL };
    UGetopt g;
    LsOpt o;
    int c, i, rc = 0, nf = 0, nd = 0, first = 1, colorwhen = 0, istty;
    LsEnt **files, **dirs;
    memset(&o, 0, sizeof(o));
    u_break_reset();
    ug_init(&g, argc, argv, "1aAdFhlrRStuUCcgonxGsp", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case '1': o.one = 1; o.longf = 0; break;
        case 'a': case UG_LONG + 0: o.all = 1; break;
        case 'A': case UG_LONG + 1: o.almost = 1; break;
        case 'd': case UG_LONG + 2: o.dirself = 1; break;
        case 'F': case UG_LONG + 3: case 'p': o.classify = 1; break;
        case 'h': case UG_LONG + 4: o.human = 1; break;
        case 'r': case UG_LONG + 5: o.rev = 1; break;
        case 'R': case UG_LONG + 6: o.recurse = 1; break;
        case 'S': o.bysize = 1; o.bytime = 0; break;
        case 't': o.bytime = 1; o.bysize = 0; break;
        case 'U': o.nosort = 1; break;
        case 'l': case 'g': case 'o': case 'n': o.longf = 1; o.one = 0; break;
        case 'C': case 'x': o.cols = 2; o.one = 0; o.longf = 0; break;
        case 'u': case 'c': case 'G': case 's': break;
        case UG_LONG + 7: case UG_LONG + 8:
            if (!g.arg || strcmp(g.arg, "always") == 0 || strcmp(g.arg, "yes") == 0 || strcmp(g.arg, "force") == 0)
                colorwhen = 1;
            else if (strcmp(g.arg, "auto") == 0 || strcmp(g.arg, "tty") == 0 || strcmp(g.arg, "if-tty") == 0)
                colorwhen = 2;
            else if (strcmp(g.arg, "never") == 0 || strcmp(g.arg, "no") == 0 || strcmp(g.arg, "none") == 0)
                colorwhen = 0;
            else {
                sh_cmd_error(argv[0], "invalid argument '%s' for '--color'", g.arg);
                ug_free(&g);
                return 2;
            }
            break;
        case UG_LONG + 999:
            ug_free(&g);
            sh_printf(1, "Usage: %s\n", "ls [-1aAdFhlrRStuC] [--color[=WHEN]] [FILE]...");
            return 0;
        default:
            ug_free(&g);
            return u_usage(argv[0]);
        }
    }
    istty = u_out_is_tty(&o.width);
    if (o.cols == 0) o.cols = istty;
    o.color = colorwhen == 1 || (colorwhen == 2 && istty);
    o.now = os_time();
    o.total_hdr = 0;
    if (g.nops == 0) g.ops[g.nops++] = ".";
    files = xmalloc(g.nops * sizeof(LsEnt *));
    dirs = xmalloc(g.nops * sizeof(LsEnt *));
    for (i = 0; i < g.nops; i++) {
        OsDirEnt e;
        if (sh_stat(g.ops[i], &e) != 0) {
            sh_cmd_error(argv[0], "cannot access '%s': %s", g.ops[i], os_errstr());
            rc = 2;
            continue;
        }
        if ((e.type == OS_T_DIR || e.type == OS_T_VOL) && !o.dirself)
            dirs[nd++] = ls_ent(g.ops[i], &e);
        else
            files[nf++] = ls_ent(g.ops[i], &e);
    }
    ls_sort(files, nf, &o);
    ls_sort(dirs, nd, &o);
    ls_print(files, nf, &o);
    if (nf) first = 0;
    for (i = 0; i < nd && !u_break(); i++) {
        int r = ls_dir(argv[0], dirs[i]->name, nd + nf > 1 || o.recurse, &first, &o);
        if (r > rc) rc = r;
    }
    ls_free_ents(files, nf);
    ls_free_ents(dirs, nd);
    ug_free(&g);
    if (u_break()) rc = 130;
    return rc;
}

/* ================================================================== */
/* cp / mv                                                             */
/* ================================================================== */

typedef struct CpOpt { int rec, force, noclob, preserve, verbose, update, notarget; } CpOpt;

static int cp_one(const char *cmd, const char *src, const char *dst, CpOpt *o)
{
    OsDirEnt se, de;
    int dexists;
    if (sh_stat(src, &se) != 0) {
        sh_cmd_error(cmd, "cannot stat '%s': %s", src, os_errstr());
        return 1;
    }
    dexists = sh_stat(dst, &de) == 0;
    if (se.type == OS_T_DIR || se.type == OS_T_VOL) {
        if (!o->rec) {
            sh_cmd_error(cmd, "-r not specified; omitting directory '%s'", src);
            return 1;
        }
        if (dexists && de.type != OS_T_DIR && de.type != OS_T_VOL) {
            sh_cmd_error(cmd, "cannot overwrite non-directory '%s' with directory '%s'", dst, src);
            return 1;
        }
        if (inside_dir(src, dst)) {
            sh_cmd_error(cmd, "cannot copy a directory, '%s', into itself, '%s'", src, dst);
            return 1;
        }
        return u_copy_tree(cmd, src, dst, o->preserve, o->verbose);
    }
    if (dexists) {
        if (de.type == OS_T_DIR || de.type == OS_T_VOL) {
            sh_cmd_error(cmd, "cannot overwrite directory '%s' with non-directory", dst);
            return 1;
        }
        if (same_file(src, dst)) {
            sh_cmd_error(cmd, "'%s' and '%s' are the same file", src, dst);
            return 1;
        }
        if (o->noclob) return 0;
        if (o->update && de.mtime >= se.mtime) return 0;
        if (o->force && !(de.prot & OS_P_WRITE)) {
            char *n = path_native(dst);
            os_setprot(n, de.prot | OS_P_WRITE | OS_P_DELETE);
            xfree(n);
        }
    }
    if (u_copy_file(cmd, src, dst, o->preserve) != 0) return 1;
    if (o->verbose) sh_printf(1, "'%s' -> '%s'\n", src, dst);
    return 0;
}

static int mv_one(const char *cmd, const char *src, const char *dst, CpOpt *o)
{
    OsDirEnt se, de;
    int dexists;
    if (sh_stat(src, &se) != 0) {
        sh_cmd_error(cmd, "cannot stat '%s': %s", src, os_errstr());
        return 1;
    }
    {
        char *ns = path_native(src);
        if (path_is_vroot(ns)) {
            xfree(ns);
            sh_cmd_error(cmd, "cannot move '%s': Operation not permitted", src);
            return 1;
        }
        xfree(ns);
    }
    dexists = sh_stat(dst, &de) == 0;
    if (dexists && !same_file(src, dst)) {
        int sdir = se.type == OS_T_DIR || se.type == OS_T_VOL;
        int ddir = de.type == OS_T_DIR || de.type == OS_T_VOL;
        if (o->noclob) return 0;
        if (o->update && de.mtime >= se.mtime) return 0;
        if (sdir && !ddir) {
            sh_cmd_error(cmd, "cannot overwrite non-directory '%s' with directory '%s'", dst, src);
            return 1;
        }
        if (!sdir && ddir) {
            sh_cmd_error(cmd, "cannot overwrite directory '%s' with non-directory", dst);
            return 1;
        }
        if (sdir && inside_dir(src, dst)) {
            sh_cmd_error(cmd, "cannot move '%s' to a subdirectory of itself, '%s'", src, dst);
            return 1;
        }
        /* AmigaDOS Rename() refuses existing targets: remove it first */
        if (!sdir) {
            if (sh_remove(dst) != 0) {
                char *n = path_native(dst);
                os_setprot(n, de.prot | OS_P_DELETE | OS_P_WRITE);
                xfree(n);
                sh_remove(dst);
            }
        } else if (sh_remove(dst) != 0) {
            sh_cmd_error(cmd, "cannot move '%s' to '%s': Directory not empty", src, dst);
            return 1;
        }
    } else if (!dexists && (se.type == OS_T_DIR) && inside_dir(src, dst)) {
        sh_cmd_error(cmd, "cannot move '%s' to a subdirectory of itself, '%s'", src, dst);
        return 1;
    }
    if (sh_rename(src, dst) != 0) {
        /* across volumes: copy and delete */
        CpOpt co = *o;
        co.rec = 1;
        co.preserve = 1;
        co.verbose = 0;
        if (cp_one(cmd, src, dst, &co) != 0) return 1;
        if (u_remove_tree(cmd, src, 0) != 0) return 1;
    }
    if (o->verbose) sh_printf(1, "renamed '%s' -> '%s'\n", src, dst);
    return 0;
}

static int cp_mv(int argc, char **argv, int mv)
{
    static const char *const lo[] = { "recursive", "force", "no-clobber", "preserve?", "verbose", "update",
                                      "target-directory=", "no-target-directory", "archive", "interactive", NULL };
    UGetopt g;
    CpOpt o;
    int c, i, rc = 0;
    const char *target = NULL;
    memset(&o, 0, sizeof(o));
    u_break_reset();
    ug_init(&g, argc, argv, mv ? "finuvt:T" : "arRfinpPuvLdt:T", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'r': case 'R': case UG_LONG + 0: o.rec = 1; break;
        case 'a': case UG_LONG + 8: o.rec = 1; o.preserve = 1; break;
        case 'f': case UG_LONG + 1: o.force = 1; o.noclob = 0; break;
        case 'n': case UG_LONG + 2: o.noclob = 1; break;
        case 'p': case UG_LONG + 3: o.preserve = 1; break;
        case 'v': case UG_LONG + 4: o.verbose = 1; break;
        case 'u': case UG_LONG + 5: o.update = 1; break;
        case 't': case UG_LONG + 6: target = g.arg; break;
        case 'T': case UG_LONG + 7: o.notarget = 1; break;
        case 'i': case 'P': case 'L': case 'd': case UG_LONG + 9: break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: %s\n", mv ? "mv [-finuv] [-t DIR] SOURCE... DEST" : "cp [-afinprRuv] [-t DIR] SOURCE... DEST"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0 || (g.nops == 1 && !target)) {
        if (g.nops == 0) sh_cmd_error(argv[0], "missing file operand");
        else sh_cmd_error(argv[0], "missing destination file operand after '%s'", g.ops[0]);
        ug_free(&g);
        return 1;
    }
    if (!target) {
        const char *last = g.ops[g.nops - 1];
        if (!o.notarget && sh_is_dir(last)) {
            target = last;
            g.nops--;
        } else if (g.nops > 2) {
            sh_cmd_error(argv[0], "target '%s' is not a directory", last);
            ug_free(&g);
            return 1;
        }
    } else if (!sh_is_dir(target)) {
        sh_cmd_error(argv[0], "target directory '%s': Not a directory", target);
        ug_free(&g);
        return 1;
    }
    if (target) {
        for (i = 0; i < g.nops && !u_break(); i++) {
            char *b = base_of(g.ops[i]);
            char *d = path_join(target, b);
            rc |= mv ? mv_one(argv[0], g.ops[i], d, &o) : cp_one(argv[0], g.ops[i], d, &o);
            xfree(b);
            xfree(d);
        }
    } else {
        rc = mv ? mv_one(argv[0], g.ops[0], g.ops[1], &o) : cp_one(argv[0], g.ops[0], g.ops[1], &o);
    }
    ug_free(&g);
    if (u_break()) rc = 130;
    return rc;
}

int util_cp(int argc, char **argv)
{
    return cp_mv(argc, argv, 0);
}

int util_mv(int argc, char **argv)
{
    return cp_mv(argc, argv, 1);
}

/* ================================================================== */
/* rm / rmdir / mkdir                                                  */
/* ================================================================== */

static int is_dot_or_dotdot(const char *p)
{
    const char *b = path_basename(p);
    size_t n = strlen(b);
    while (n > 1 && b[n - 1] == '/') n--;
    return (n == 1 && b[0] == '.') || (n == 2 && b[0] == '.' && b[1] == '.');
}

static int is_root(const char *p)
{
    char *n = path_native(p);
    int r = path_is_vroot(n) || strcmp(n, "/") == 0;
    xfree(n);
    return r;
}

int util_rm(int argc, char **argv)
{
    static const char *const lo[] = { "recursive", "force", "verbose", "dir", "interactive?", NULL };
    UGetopt g;
    int c, i, rc = 0, rec = 0, force = 0, verbose = 0, dir = 0;
    u_break_reset();
    ug_init(&g, argc, argv, "rRfvdiI", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'r': case 'R': case UG_LONG + 0: rec = 1; break;
        case 'f': case UG_LONG + 1: force = 1; break;
        case 'v': case UG_LONG + 2: verbose = 1; break;
        case 'd': case UG_LONG + 3: dir = 1; break;
        case 'i': case 'I': case UG_LONG + 4: break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: rm [-dfirRv] FILE...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0 && !force) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops && !u_break(); i++) {
        const char *p = g.ops[i];
        OsDirEnt e;
        if (is_root(p)) {
            sh_cmd_error(argv[0], "it is dangerous to operate recursively on '%s'", p);
            rc = 1;
            continue;
        }
        if (is_dot_or_dotdot(p)) {
            sh_cmd_error(argv[0], "refusing to remove '.' or '..' directory: skipping '%s'", p);
            rc = 1;
            continue;
        }
        if (sh_stat(p, &e) != 0) {
            if (!force) {
                sh_cmd_error(argv[0], "cannot remove '%s': %s", p, os_errstr());
                rc = 1;
            }
            continue;
        }
        if (e.type == OS_T_VOL) {
            sh_cmd_error(argv[0], "cannot remove '%s': Is a volume", p);
            rc = 1;
            continue;
        }
        if (e.type == OS_T_DIR) {
            if (rec) {
                rc |= u_remove_tree(argv[0], p, verbose);
                continue;
            }
            if (!dir) {
                sh_cmd_error(argv[0], "cannot remove '%s': Is a directory", p);
                rc = 1;
                continue;
            }
        }
        if (sh_remove(p) != 0) {
            int ok = 0;
            if (force && !(e.prot & OS_P_DELETE)) {
                char *n = path_native(p);
                os_setprot(n, e.prot | OS_P_DELETE | OS_P_WRITE);
                xfree(n);
                ok = sh_remove(p) == 0;
            }
            if (!ok) {
                sh_cmd_error(argv[0], "cannot remove '%s': %s", p, os_errstr());
                rc = 1;
                continue;
            }
        }
        if (verbose) sh_printf(1, e.type == OS_T_DIR ? "removed directory '%s'\n" : "removed '%s'\n", p);
    }
    ug_free(&g);
    if (u_break()) rc = 130;
    return rc;
}

int util_rmdir(int argc, char **argv)
{
    static const char *const lo[] = { "parents", "verbose", "ignore-fail-on-non-empty", NULL };
    UGetopt g;
    int c, i, rc = 0, parents = 0, verbose = 0, ignore = 0;
    ug_init(&g, argc, argv, "pv", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'p': case UG_LONG + 0: parents = 1; break;
        case 'v': case UG_LONG + 1: verbose = 1; break;
        case UG_LONG + 2: ignore = 1; break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: rmdir [-pv] DIRECTORY...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops; i++) {
        char *p = xstrdup(g.ops[i]);
        for (;;) {
            OsDirEnt e;
            size_t n = strlen(p);
            char *d;
            while (n > 1 && p[n - 1] == '/') p[--n] = 0;
            if (sh_stat(p, &e) != 0) {
                sh_cmd_error(argv[0], "failed to remove '%s': %s", p, os_errstr());
                rc = 1;
                break;
            }
            if (e.type != OS_T_DIR) {
                sh_cmd_error(argv[0], "failed to remove '%s': Not a directory", p);
                rc = 1;
                break;
            }
            if (verbose) sh_printf(1, "rmdir: removing directory, '%s'\n", p);
            if (sh_remove(p) != 0) {
                if (!ignore) {
                    sh_cmd_error(argv[0], "failed to remove '%s': %s", p, os_errstr());
                    rc = 1;
                }
                break;
            }
            if (!parents) break;
            d = path_dirname(p);
            if (strcmp(d, ".") == 0 || strcmp(d, "/") == 0 || strcmp(d, p) == 0 ||
                (d[0] && d[strlen(d) - 1] == ':')) {
                xfree(d);
                break;
            }
            xfree(p);
            p = d;
        }
        xfree(p);
    }
    ug_free(&g);
    return rc;
}

static int mkdir_p(const char *cmd, const char *path, int verbose)
{
    char *p = xstrdup(path);
    size_t n = strlen(p), i;
    int rc = 0;
    for (i = 1; i <= n; i++) {
        char save;
        if (i < n && p[i] != '/') continue;
        if (p[i - 1] == '/' || p[i - 1] == ':') continue;
        save = p[i];
        p[i] = 0;
        if (!sh_is_dir(p)) {
            if (sh_mkdir(p) != 0 && !sh_is_dir(p)) {
                sh_cmd_error(cmd, "cannot create directory '%s': %s", p, os_errstr());
                rc = 1;
                p[i] = save;
                break;
            }
            if (verbose) sh_printf(1, "mkdir: created directory '%s'\n", p);
        }
        p[i] = save;
    }
    xfree(p);
    return rc;
}

int util_mkdir(int argc, char **argv)
{
    static const char *const lo[] = { "parents", "verbose", "mode=", NULL };
    UGetopt g;
    int c, i, rc = 0, parents = 0, verbose = 0;
    ug_init(&g, argc, argv, "pvm:", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'p': case UG_LONG + 0: parents = 1; break;
        case 'v': case UG_LONG + 1: verbose = 1; break;
        case 'm': case UG_LONG + 2: break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: mkdir [-pv] DIRECTORY...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops; i++) {
        if (parents) {
            rc |= mkdir_p(argv[0], g.ops[i], verbose);
        } else if (sh_exists(g.ops[i])) {
            sh_cmd_error(argv[0], "cannot create directory '%s': File exists", g.ops[i]);
            rc = 1;
        } else if (sh_mkdir(g.ops[i]) != 0) {
            sh_cmd_error(argv[0], "cannot create directory '%s': %s", g.ops[i], os_errstr());
            rc = 1;
        } else if (verbose) {
            sh_printf(1, "mkdir: created directory '%s'\n", g.ops[i]);
        }
    }
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* touch                                                               */
/* ================================================================== */

/* [[CC]YY]MMDDhhmm[.ss] */
static int parse_stamp(const char *s, long *out)
{
    char d[16];
    int n = 0, sec = 0, i;
    const char *dot = strchr(s, '.');
    ShTm tm;
    long now = os_time();
    size_t len = dot ? (size_t)(dot - s) : strlen(s);
    if (len != 8 && len != 10 && len != 12) return -1;
    for (i = 0; i < (int)len; i++) {
        if (!isdigit((unsigned char)s[i])) return -1;
        d[i] = (char)(s[i] - '0');
    }
    if (dot) {
        if (!isdigit((unsigned char)dot[1]) || !isdigit((unsigned char)dot[2]) || dot[3]) return -1;
        sec = (dot[1] - '0') * 10 + dot[2] - '0';
    }
    sh_localtime(now, &tm);
    if (len == 12) {
        tm.year = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];
        n = 4;
    } else if (len == 10) {
        int yy = d[0] * 10 + d[1];
        tm.year = yy < 69 ? 2000 + yy : 1900 + yy;
        n = 2;
    }
    tm.mon = d[n] * 10 + d[n + 1] - 1;
    tm.mday = d[n + 2] * 10 + d[n + 3];
    tm.hour = d[n + 4] * 10 + d[n + 5];
    tm.min = d[n + 6] * 10 + d[n + 7];
    tm.sec = sec;
    *out = sh_mktime(&tm);
    return 0;
}

int util_touch(int argc, char **argv)
{
    static const char *const lo[] = { "no-create", "date=", "reference=", NULL };
    UGetopt g;
    int c, i, rc = 0, nocreate = 0;
    long t;
    ug_init(&g, argc, argv, "acmd:t:r:f", lo);
    t = os_time();
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'c': case UG_LONG + 0: nocreate = 1; break;
        case 'a': case 'm': case 'f': break;
        case 'd': case UG_LONG + 1:
            if (u_parse_date(g.arg, 0, &t) != 0) {
                sh_cmd_error(argv[0], "invalid date format '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            break;
        case 't':
            if (parse_stamp(g.arg, &t) != 0) {
                sh_cmd_error(argv[0], "invalid date format '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            break;
        case 'r': case UG_LONG + 2: {
            OsDirEnt e;
            if (sh_stat(g.arg, &e) != 0) {
                sh_cmd_error(argv[0], "failed to get attributes of '%s': %s", g.arg, os_errstr());
                ug_free(&g);
                return 1;
            }
            t = e.mtime;
            break;
        }
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: touch [-c] [-d DATE] [-t STAMP] [-r FILE] FILE...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) {
        sh_cmd_error(argv[0], "missing file operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops; i++) {
        char *n;
        if (!sh_exists(g.ops[i])) {
            ShFile *f;
            if (nocreate) continue;
            f = shf_open(g.ops[i], OS_WR);
            if (!f) {
                sh_cmd_error(argv[0], "cannot touch '%s': %s", g.ops[i], os_errstr());
                rc = 1;
                continue;
            }
            shf_close(f);
        }
        n = path_native(g.ops[i]);
        if (os_setdate(n, t) != 0) {
            sh_cmd_error(argv[0], "setting times of '%s': %s", g.ops[i], os_errstr());
            rc = 1;
        }
        xfree(n);
    }
    ug_free(&g);
    return rc;
}

/* ================================================================== */
/* chmod                                                               */
/* ================================================================== */

/* apply mode string; returns 0 ok, -1 invalid */
static int chmod_apply(const char *mode, unsigned long old, unsigned long *out)
{
    unsigned long p = old;
    const char *s = mode;
    if (isdigit((unsigned char)*s)) {
        unsigned long v = 0, own;
        int nd = 0;
        for (; *s; s++, nd++) {
            if (*s < '0' || *s > '7') return -1;
            v = v * 8 + (unsigned long)(*s - '0');
        }
        if (nd > 4) return -1;
        own = (v >> 6) & 7;
        p &= ~(unsigned long)(OS_P_READ | OS_P_WRITE | OS_P_EXEC);
        if (own & 4) p |= OS_P_READ;
        if (own & 2) p |= OS_P_WRITE | OS_P_DELETE;
        if (own & 1) p |= OS_P_EXEC;
        p |= OS_P_DELETE;
        *out = p;
        return 0;
    }
    while (*s) {
        int who = 0, unixy = 0;      /* who: bit0 = owner affected */
        while (*s == 'u' || *s == 'g' || *s == 'o' || *s == 'a') {
            unixy = 1;
            if (*s == 'u' || *s == 'a') who = 1;
            s++;
        }
        if (!unixy) who = 1;
        if (*s != '+' && *s != '-' && *s != '=') return -1;
        while (*s == '+' || *s == '-' || *s == '=') {
            int op = *s++;
            unsigned long bits = 0;
            while (*s && *s != ',' && *s != '+' && *s != '-' && *s != '=') {
                switch (*s) {
                case 'r': bits |= OS_P_READ; break;
                case 'w': bits |= OS_P_WRITE; break;
                case 'x': case 'e': case 'X': bits |= OS_P_EXEC; break;
                case 'd': bits |= OS_P_DELETE; break;
                case 'h': bits |= OS_P_HIDDEN; break;
                case 's': bits |= OS_P_SCRIPT; break;
                case 'p': bits |= OS_P_PURE; break;
                case 'a': bits |= OS_P_ARCH; break;
                case 't': break;
                default: return -1;
                }
                s++;
            }
            if (!who) continue;
            if (op == '+') p |= bits;
            else if (op == '-') p &= ~bits;
            else {
                unsigned long mask = unixy ? (OS_P_READ | OS_P_WRITE | OS_P_EXEC) : 0xffUL;
                p = (p & ~mask) | (bits & (unixy ? mask : 0xffUL));
            }
        }
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    *out = p;
    return 0;
}

static int chmod_path(const char *cmd, const char *path, const char *mode, int rec, int verbose, int quiet)
{
    OsDirEnt e;
    unsigned long np;
    int rc = 0;
    char *n;
    if (u_break()) return 1;
    if (sh_stat(path, &e) != 0) {
        if (!quiet) sh_cmd_error(cmd, "cannot access '%s': %s", path, os_errstr());
        return 1;
    }
    if (chmod_apply(mode, e.prot, &np) != 0) {
        sh_cmd_error(cmd, "invalid mode: '%s'", mode);
        return 2;
    }
    if (e.type != OS_T_VOL) {
        n = path_native(path);
        if (os_setprot(n, np) != 0) {
            if (!quiet) sh_cmd_error(cmd, "changing permissions of '%s': %s", path, os_errstr());
            rc = 1;
        } else if (verbose) {
            char a[9], b[9];
            prot_str(e.prot, a);
            prot_str(np, b);
            sh_printf(1, "mode of '%s' changed from %s to %s\n", path, a, b);
        }
        xfree(n);
    }
    if (rec && e.type == OS_T_DIR) {
        void *d = sh_opendir(path);
        StrList l;
        int i;
        OsDirEnt *de;
        sl_init(&l);
        if (d) {
            de = xmalloc(sizeof(OsDirEnt));
            while (os_readdir(d, de)) sl_adddup(&l, de->name);
            os_closedir(d);
            xfree(de);
        }
        for (i = 0; i < l.n; i++) {
            char *p = path_join(path, l.v[i]);
            rc |= chmod_path(cmd, p, mode, rec, verbose, quiet);
            xfree(p);
        }
        sl_free(&l);
    }
    return rc;
}

int util_chmod(int argc, char **argv)
{
    int i, rec = 0, verbose = 0, quiet = 0, rc = 0, nfiles = 0, dd = 0;
    const char *mode = NULL;
    char **files;
    u_break_reset();
    /* the files, apart (argv belongs to the caller, which frees it) */
    files = xmalloc((argc + 1) * sizeof(char *));
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!dd && strcmp(a, "--") == 0) {
            dd = 1;
            continue;
        }
        if (!dd && a[0] == '-' && a[1] == '-') {
            if (strcmp(a, "--recursive") == 0) rec = 1;
            else if (strcmp(a, "--verbose") == 0 || strcmp(a, "--changes") == 0) verbose = 1;
            else if (strcmp(a, "--silent") == 0 || strcmp(a, "--quiet") == 0) quiet = 1;
            else if (strcmp(a, "--help") == 0) {
                sh_printf(1, "Usage: chmod [-Rv] (+-=)[hsparwed] | [ugoa][+-=][rwx] | OCTAL FILE...\n");
                xfree(files);
                return 0;
            } else {
                sh_cmd_error(argv[0], "unrecognized option '%s'", a);
                xfree(files);
                return 1;
            }
            continue;
        }
        if (!dd && a[0] == '-' && a[1] && strspn(a + 1, "Rvcf") == strlen(a + 1)) {
            if (strchr(a, 'R')) rec = 1;
            if (strchr(a, 'v') || strchr(a, 'c')) verbose = 1;
            if (strchr(a, 'f')) quiet = 1;
            continue;
        }
        if (!mode) mode = a;
        else files[nfiles++] = argv[i];
    }
    if (!mode || nfiles == 0) {
        if (!mode) sh_cmd_error(argv[0], "missing operand");
        else sh_cmd_error(argv[0], "missing operand after '%s'", mode);
        xfree(files);
        return 1;
    }
    {
        unsigned long dummy;
        if (chmod_apply(mode, 0, &dummy) != 0) {
            sh_cmd_error(argv[0], "invalid mode: '%s'", mode);
            xfree(files);
            return 1;
        }
    }
    for (i = 0; i < nfiles; i++) rc |= chmod_path(argv[0], files[i], mode, rec, verbose, quiet);
    xfree(files);
    return rc ? 1 : 0;
}

/* ================================================================== */
/* find                                                                */
/* ================================================================== */

enum {
    F_AND, F_OR, F_NOT, F_TRUE, F_FALSE, F_NAME, F_INAME, F_PATH, F_IPATH, F_TYPE,
    F_NEWER, F_SIZE, F_PRINT, F_PRINT0, F_EXEC, F_EXECPLUS, F_DELETE, F_EMPTY,
    F_MTIME, F_MMIN, F_PRUNE, F_QUIT, F_REGEX
};

typedef struct FNode {
    int type;
    struct FNode *l, *r;
    char *s;               /* pattern */
    int cmp;               /* -1 less, 0 equal, 1 greater */
    double num;
    long unit;
    char **argv; int argc; /* -exec */
    StrList pend;          /* -exec + accumulated paths */
    Regex *re;
} FNode;

typedef struct FCtx {
    const char *cmd;
    char **av; int ac, pos;
    int maxdepth, mindepth, depthfirst, err, quit;
    long now;
    FNode *root;
    OsDirEnt *ent;          /* scratch */
} FCtx;

typedef struct FInfo { int type; double size; long mtime; int prune; } FInfo;

static FNode *fnode(int type)
{
    FNode *n = xcalloc(sizeof(FNode));
    n->type = type;
    sl_init(&n->pend);
    return n;
}

static void fnode_free(FNode *n)
{
    if (!n) return;
    fnode_free(n->l);
    fnode_free(n->r);
    xfree(n->argv);
    sl_free(&n->pend);
    if (n->re) re_free(n->re);
    xfree(n);
}

static FNode *f_or(FCtx *c);
static int f_has_action;

static const char *f_arg(FCtx *c, const char *pred)
{
    if (c->pos >= c->ac) {
        sh_cmd_error(c->cmd, "missing argument to '%s'", pred);
        c->err = 1;
        return NULL;
    }
    return c->av[c->pos++];
}

static int f_numarg(const char *s, int *cmp, double *num, long *unit, int allow_unit)
{
    double v = 0;
    int any = 0;
    *cmp = 0;
    if (*s == '+') *cmp = 1, s++;
    else if (*s == '-') *cmp = -1, s++;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s++ - '0');
        any = 1;
    }
    if (!any) return -1;
    *unit = 512;
    if (allow_unit && *s) {
        switch (*s++) {
        case 'c': *unit = 1; break;
        case 'w': *unit = 2; break;
        case 'b': *unit = 512; break;
        case 'k': *unit = 1024; break;
        case 'M': *unit = 1048576L; break;
        case 'G': *unit = 1073741824L; break;
        default: return -1;
        }
    }
    if (*s) return -1;
    *num = v;
    return 0;
}

static FNode *f_primary(FCtx *c)
{
    const char *a, *v;
    FNode *n;
    if (c->pos >= c->ac) {
        sh_cmd_error(c->cmd, "expected an expression");
        c->err = 1;
        return NULL;
    }
    a = c->av[c->pos++];
    if (strcmp(a, "(") == 0) {
        n = f_or(c);
        if (c->err) return n;
        if (c->pos >= c->ac || strcmp(c->av[c->pos], ")") != 0) {
            sh_cmd_error(c->cmd, "invalid expression; I was expecting to find a ')' somewhere but did not see one.");
            c->err = 1;
            return n;
        }
        c->pos++;
        return n;
    }
    if (strcmp(a, "!") == 0 || strcmp(a, "-not") == 0) {
        n = fnode(F_NOT);
        n->l = f_primary(c);
        return n;
    }
    if (strcmp(a, "-name") == 0 || strcmp(a, "-iname") == 0 || strcmp(a, "-path") == 0 ||
        strcmp(a, "-ipath") == 0 || strcmp(a, "-wholename") == 0 || strcmp(a, "-iwholename") == 0) {
        if (!(v = f_arg(c, a))) return NULL;
        n = fnode(a[1] == 'n' ? F_NAME : a[1] == 'i' && a[2] == 'n' ? F_INAME :
                  (a[1] == 'p' || a[1] == 'w') ? F_PATH : F_IPATH);
        n->s = (char *)v;
        return n;
    }
    if (strcmp(a, "-regex") == 0 || strcmp(a, "-iregex") == 0) {
        char *err = NULL, *full;
        if (!(v = f_arg(c, a))) return NULL;
        n = fnode(F_REGEX);
        full = str_printf("^(%s)$", v);
        n->re = re_compile(full, a[1] == 'i' ? RE_ICASE : 0, &err);
        xfree(full);
        if (!n->re) {
            sh_cmd_error(c->cmd, "invalid regular expression '%s': %s", v, err ? err : "");
            xfree(err);
            c->err = 1;
        }
        return n;
    }
    if (strcmp(a, "-type") == 0) {
        if (!(v = f_arg(c, a))) return NULL;
        if (!strchr("fdl", v[0]) || !v[0] || v[1]) {
            sh_cmd_error(c->cmd, "Unknown argument to -type: %s", v);
            c->err = 1;
            return NULL;
        }
        n = fnode(F_TYPE);
        n->cmp = v[0];
        return n;
    }
    if (strcmp(a, "-maxdepth") == 0 || strcmp(a, "-mindepth") == 0) {
        long d;
        if (!(v = f_arg(c, a))) return NULL;
        if (!u_atoi(v, &d) || d < 0) {
            sh_cmd_error(c->cmd, "Expected a positive decimal integer argument to %s, but got '%s'", a, v);
            c->err = 1;
            return NULL;
        }
        if (a[2] == 'a') c->maxdepth = (int)d;
        else c->mindepth = (int)d;
        return fnode(F_TRUE);
    }
    if (strcmp(a, "-depth") == 0 || strcmp(a, "-d") == 0) {
        c->depthfirst = 1;
        return fnode(F_TRUE);
    }
    if (strcmp(a, "-xdev") == 0 || strcmp(a, "-mount") == 0 || strcmp(a, "-follow") == 0 ||
        strcmp(a, "-noleaf") == 0 || strcmp(a, "-true") == 0)
        return fnode(F_TRUE);
    if (strcmp(a, "-false") == 0) return fnode(F_FALSE);
    if (strcmp(a, "-newer") == 0) {
        OsDirEnt e;
        if (!(v = f_arg(c, a))) return NULL;
        if (sh_stat(v, &e) != 0) {
            sh_cmd_error(c->cmd, "'%s': %s", v, os_errstr());
            c->err = 1;
            return NULL;
        }
        n = fnode(F_NEWER);
        n->num = (double)e.mtime;
        return n;
    }
    if (strcmp(a, "-size") == 0 || strcmp(a, "-mtime") == 0 || strcmp(a, "-mmin") == 0) {
        if (!(v = f_arg(c, a))) return NULL;
        n = fnode(a[1] == 's' ? F_SIZE : a[2] == 't' ? F_MTIME : F_MMIN);
        if (f_numarg(v, &n->cmp, &n->num, &n->unit, a[1] == 's') != 0) {
            sh_cmd_error(c->cmd, "invalid argument '%s' to '%s'", v, a);
            c->err = 1;
        }
        return n;
    }
    if (strcmp(a, "-empty") == 0) return fnode(F_EMPTY);
    if (strcmp(a, "-print") == 0) {
        f_has_action = 1;
        return fnode(F_PRINT);
    }
    if (strcmp(a, "-print0") == 0) {
        f_has_action = 1;
        return fnode(F_PRINT0);
    }
    if (strcmp(a, "-delete") == 0) {
        f_has_action = 1;
        c->depthfirst = 1;
        return fnode(F_DELETE);
    }
    if (strcmp(a, "-prune") == 0) return fnode(F_PRUNE);
    if (strcmp(a, "-quit") == 0) return fnode(F_QUIT);
    if (strcmp(a, "-exec") == 0 || strcmp(a, "-ok") == 0) {
        int start = c->pos;
        f_has_action = 1;
        while (c->pos < c->ac && strcmp(c->av[c->pos], ";") != 0 &&
               !(strcmp(c->av[c->pos], "+") == 0 && c->pos > start && strcmp(c->av[c->pos - 1], "{}") == 0))
            c->pos++;
        if (c->pos >= c->ac || c->pos == start) {
            sh_cmd_error(c->cmd, "missing argument to '%s'", a);
            c->err = 1;
            return NULL;
        }
        n = fnode(strcmp(c->av[c->pos], "+") == 0 ? F_EXECPLUS : F_EXEC);
        n->argc = c->pos - start;
        n->argv = xmalloc((n->argc + 1) * sizeof(char *));
        memcpy(n->argv, c->av + start, n->argc * sizeof(char *));
        n->argv[n->argc] = NULL;
        if (n->type == F_EXECPLUS) n->argc--;   /* drop the {} */
        c->pos++;
        return n;
    }
    sh_cmd_error(c->cmd, a[0] == '-' ? "unknown predicate '%s'" : "paths must precede expression: '%s'", a);
    c->err = 1;
    return NULL;
}

static FNode *f_and(FCtx *c)
{
    FNode *l = f_primary(c);
    while (!c->err && c->pos < c->ac) {
        const char *a = c->av[c->pos];
        FNode *n;
        if (strcmp(a, "-o") == 0 || strcmp(a, "-or") == 0 || strcmp(a, ")") == 0 || strcmp(a, ",") == 0) break;
        if (strcmp(a, "-a") == 0 || strcmp(a, "-and") == 0) c->pos++;
        n = fnode(F_AND);
        n->l = l;
        n->r = f_primary(c);
        l = n;
    }
    return l;
}

static FNode *f_or(FCtx *c)
{
    FNode *l = f_and(c);
    while (!c->err && c->pos < c->ac &&
           (strcmp(c->av[c->pos], "-o") == 0 || strcmp(c->av[c->pos], "-or") == 0 || strcmp(c->av[c->pos], ",") == 0)) {
        FNode *n = fnode(F_OR);
        if (c->av[c->pos][0] == ',') n->type = F_AND;   /* approximate */
        c->pos++;
        n->l = l;
        n->r = f_and(c);
        l = n;
    }
    return l;
}

static int f_numcmp(double v, FNode *n)
{
    if (n->cmp > 0) return v > n->num;
    if (n->cmp < 0) return v < n->num;
    return v == n->num;
}

static int f_run_exec(FCtx *c, char **argv, int argc)
{
    int st;
    (void)c;
    sh_flush_all();
    st = exec_argv(argc, argv);
    return st;
}

static int f_exec_flush(FCtx *c, FNode *n)
{
    StrList a;
    int i, st;
    if (n->pend.n == 0) return 0;
    sl_init(&a);
    for (i = 0; i < n->argc; i++) sl_adddup(&a, n->argv[i]);
    for (i = 0; i < n->pend.n; i++) sl_adddup(&a, n->pend.v[i]);
    st = f_run_exec(c, sl_argv(&a), a.n);
    sl_free(&a);
    sl_free(&n->pend);
    if (st != 0) c->err = 1;
    return st;
}

static void f_flush_all(FCtx *c, FNode *n)
{
    if (!n) return;
    if (n->type == F_EXECPLUS) f_exec_flush(c, n);
    f_flush_all(c, n->l);
    f_flush_all(c, n->r);
}

static char *f_subst(const char *s, const char *path)
{
    SBuf b;
    const char *p;
    sb_init(&b);
    sb_addn(&b, "", 0);
    while ((p = strstr(s, "{}")) != NULL) {
        sb_addn(&b, s, (int)(p - s));
        sb_adds(&b, path);
        s = p + 2;
    }
    sb_adds(&b, s);
    return sb_take(&b);
}

static int f_eval(FCtx *c, FNode *n, const char *path, FInfo *fi)
{
    switch (n->type) {
    case F_AND: return f_eval(c, n->l, path, fi) && !c->quit && f_eval(c, n->r, path, fi);
    case F_OR: return f_eval(c, n->l, path, fi) || (!c->quit && f_eval(c, n->r, path, fi));
    case F_NOT: return !f_eval(c, n->l, path, fi);
    case F_TRUE: return 1;
    case F_FALSE: return 0;
    case F_NAME: case F_INAME: {
        char *b = base_of(path);
        int r;
        if (fi->type == OS_T_VOL || (strcmp(path, "/") == 0)) {
            xfree(b);
            b = xstrdup(path);
        }
#ifdef AMIGA
        r = pat_match(n->s, b, PM_DOTGLOB | PM_NOCASE);
#else
        r = pat_match(n->s, b, PM_DOTGLOB | (n->type == F_INAME ? PM_NOCASE : 0));
#endif
        xfree(b);
        return r;
    }
    case F_PATH: case F_IPATH:
#ifdef AMIGA
        return pat_match(n->s, path, PM_DOTGLOB | PM_NOCASE);
#else
        return pat_match(n->s, path, PM_DOTGLOB | (n->type == F_IPATH ? PM_NOCASE : 0));
#endif
    case F_REGEX: {
        ReMatch m[1];
        return n->re && re_exec(n->re, path, 1, m, 0);
    }
    case F_TYPE:
        if (n->cmp == 'd') return fi->type == OS_T_DIR || fi->type == OS_T_VOL;
        if (n->cmp == 'l') return fi->type == OS_T_LINK;
        return fi->type == OS_T_FILE;
    case F_NEWER: return (double)fi->mtime > n->num;
    case F_SIZE: {
        double units;
        unsigned long u;
        if (n->unit == 1) units = fi->size;
        else {
            units = fi->size / (double)n->unit;
            u = (unsigned long)units;
            if ((double)u < units) u++;
            units = (double)u;
        }
        return f_numcmp(units, n);
    }
    case F_MTIME: return f_numcmp((double)((c->now - fi->mtime) / 86400L), n);
    case F_MMIN: {
        long d = c->now - fi->mtime;
        return f_numcmp((double)((d + 59) / 60), n);
    }
    case F_EMPTY:
        if (fi->type == OS_T_DIR) {
            void *d = sh_opendir(path);
            int empty = 1;
            if (d) {
                empty = !os_readdir(d, c->ent);
                os_closedir(d);
            }
            return empty;
        }
        return fi->type == OS_T_FILE && fi->size == 0;
    case F_PRINT:
        sh_puts(1, path);
        sh_putc(1, '\n');
        return 1;
    case F_PRINT0:
        sh_puts(1, path);
        sh_write(1, "", 1);
        return 1;
    case F_PRUNE:
        fi->prune = 1;
        return 1;
    case F_QUIT:
        c->quit = 1;
        return 1;
    case F_DELETE:
        if (is_dot_or_dotdot(path) && (strcmp(path, ".") == 0)) return 1;
        if (sh_remove(path) != 0) {
            sh_cmd_error(c->cmd, "cannot delete '%s': %s", path, os_errstr());
            c->err = 1;
            return 0;
        }
        return 1;
    case F_EXEC: {
        StrList a;
        int i, st;
        sl_init(&a);
        for (i = 0; i < n->argc; i++) sl_add(&a, f_subst(n->argv[i], path));
        st = f_run_exec(c, sl_argv(&a), a.n);
        sl_free(&a);
        return st == 0;
    }
    case F_EXECPLUS:
        sl_adddup(&n->pend, path);
        if (n->pend.n >= 512) f_exec_flush(c, n);
        return 1;
    }
    return 0;
}

static void f_walk(FCtx *c, const char *path, FInfo *fi, int depth)
{
    int i;
    if (c->quit || u_break()) return;
    fi->prune = 0;
    if (!c->depthfirst && depth >= c->mindepth) f_eval(c, c->root, path, fi);
    if ((fi->type == OS_T_DIR || fi->type == OS_T_VOL) && depth < c->maxdepth && !fi->prune && depth < 200) {
        void *d = sh_opendir(path);
        FInfo *kids = NULL;
        char **names = NULL;
        int n = 0, cap = 0;
        if (!d) {
            sh_cmd_error(c->cmd, "'%s': %s", path, os_errstr());
            c->err = 1;
        } else {
            while (os_readdir(d, c->ent)) {
                if (n == cap) {
                    cap = cap ? cap * 2 : 16;
                    kids = xrealloc(kids, cap * sizeof(FInfo));
                    names = xrealloc(names, cap * sizeof(char *));
                }
                names[n] = xstrdup(c->ent->name);
                kids[n].type = c->ent->type;
                kids[n].size = c->ent->size;
                kids[n].mtime = c->ent->mtime;
                kids[n].prune = 0;
                n++;
            }
            os_closedir(d);
            for (i = 0; i < n; i++) {
                if (!c->quit && !u_break()) {
                    char *p = path_join(path, names[i]);
                    f_walk(c, p, &kids[i], depth + 1);
                    xfree(p);
                }
                xfree(names[i]);
            }
            xfree(names);
            xfree(kids);
        }
    }
    if (c->depthfirst && depth >= c->mindepth && !c->quit) f_eval(c, c->root, path, fi);
}

int util_find(int argc, char **argv)
{
    FCtx c;
    int npaths = 0, i;
    memset(&c, 0, sizeof(c));
    u_break_reset();
    c.cmd = argv[0];
    c.maxdepth = 1 << 30;
    c.now = os_time();
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '-' && a[1]) || strcmp(a, "(") == 0 || strcmp(a, "!") == 0) break;
        npaths++;
    }
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        sh_printf(1, "Usage: %s\n", "find [PATH]... [EXPRESSION]");
        return 0;
    }
    c.av = argv + 1 + npaths;
    c.ac = argc - 1 - npaths;
    c.pos = 0;
    f_has_action = 0;
    if (c.ac > 0) c.root = f_or(&c);
    if (!c.err && c.pos < c.ac) {
        sh_cmd_error(c.cmd, "unexpected '%s'", c.av[c.pos]);
        c.err = 1;
    }
    if (c.err) {
        fnode_free(c.root);
        return 1;
    }
    if (!c.root) c.root = fnode(F_TRUE);
    if (!f_has_action) {
        FNode *n = fnode(F_AND);
        n->l = c.root;
        n->r = fnode(F_PRINT);
        c.root = n;
    }
    c.ent = xmalloc(sizeof(OsDirEnt));
    for (i = 0; i < (npaths ? npaths : 1) && !c.quit && !u_break(); i++) {
        const char *p = npaths ? argv[1 + i] : ".";
        OsDirEnt e;
        FInfo fi;
        if (sh_stat(p, &e) != 0) {
            sh_cmd_error(c.cmd, "'%s': %s", p, os_errstr());
            c.err = 1;
            continue;
        }
        fi.type = e.type;
        fi.size = e.size;
        fi.mtime = e.mtime;
        fi.prune = 0;
        f_walk(&c, p, &fi, 0);
    }
    f_flush_all(&c, c.root);
    xfree(c.ent);
    fnode_free(c.root);
    if (u_break()) return 130;
    return c.err ? 1 : 0;
}

/* ================================================================== */
/* du / df                                                             */
/* ================================================================== */

typedef struct DuOpt { int all, human, summ, maxdepth, bytes; const char *cmd; int err; } DuOpt;

static void du_print(DuOpt *o, double sz, const char *path)
{
    char *s;
    if (o->human) s = u_size_str(sz, 1);
    else if (o->bytes) s = u_size_str(sz, 0);
    else {
        double k = sz / 1024.0;
        unsigned long u = (unsigned long)k;
        if ((double)u < k) u++;
        s = xstrdup(u_ultoa(u));
    }
    sh_printf(1, "%s\t%s\n", s, path);
    xfree(s);
}

static double du_walk(DuOpt *o, const char *path, OsDirEnt *e, int depth)
{
    double tot = e->size;
    if (u_break()) return 0;
    if (e->type == OS_T_DIR || e->type == OS_T_VOL) {
        void *d = sh_opendir(path);
        StrList l;
        int i;
        sl_init(&l);
        if (!d) {
            sh_cmd_error(o->cmd, "cannot read directory '%s': %s", path, os_errstr());
            o->err = 1;
        } else {
            while (os_readdir(d, e)) sl_adddup(&l, e->name);
            os_closedir(d);
        }
        for (i = 0; i < l.n && !u_break(); i++) {
            char *p = path_join(path, l.v[i]);
            if (sh_stat(p, e) == 0) tot += du_walk(o, p, e, depth + 1);
            xfree(p);
        }
        sl_free(&l);
        if (!o->summ && depth <= o->maxdepth) du_print(o, tot, path);
    } else if (o->all && !o->summ && depth <= o->maxdepth) {
        du_print(o, tot, path);
    } else if (depth == 0 && !o->summ) {
        du_print(o, tot, path);
    }
    return tot;
}

int util_du(int argc, char **argv)
{
    static const char *const lo[] = { "all", "human-readable", "summarize", "total", "max-depth=", "bytes", "apparent-size", NULL };
    UGetopt g;
    DuOpt o;
    int c, i, total = 0;
    double grand = 0;
    long v;
    memset(&o, 0, sizeof(o));
    o.cmd = argv[0];
    o.maxdepth = 1 << 30;
    u_break_reset();
    ug_init(&g, argc, argv, "ahsckd:bxL", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'a': case UG_LONG + 0: o.all = 1; break;
        case 'h': case UG_LONG + 1: o.human = 1; break;
        case 's': case UG_LONG + 2: o.summ = 1; break;
        case 'c': case UG_LONG + 3: total = 1; break;
        case 'd': case UG_LONG + 4:
            if (!u_atoi(g.arg, &v) || v < 0) {
                sh_cmd_error(argv[0], "invalid maximum depth '%s'", g.arg);
                ug_free(&g);
                return 1;
            }
            o.maxdepth = (int)v;
            break;
        case 'b': case UG_LONG + 5: o.bytes = 1; break;
        case 'k': case 'x': case 'L': case UG_LONG + 6: break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: du [-achks] [-d N] [FILE]...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) g.ops[g.nops++] = ".";
    for (i = 0; i < g.nops && !u_break(); i++) {
        OsDirEnt *e = xmalloc(sizeof(OsDirEnt));
        double t;
        if (sh_stat(g.ops[i], e) != 0) {
            sh_cmd_error(argv[0], "cannot access '%s': %s", g.ops[i], os_errstr());
            o.err = 1;
            xfree(e);
            continue;
        }
        t = du_walk(&o, g.ops[i], e, 0);
        if (o.summ) du_print(&o, t, g.ops[i]);
        grand += t;
        xfree(e);
    }
    if (total) du_print(&o, grand, "total");
    ug_free(&g);
    if (u_break()) return 130;
    return o.err;
}

typedef struct DfCtx { StrList vols; } DfCtx;

static int df_cb(const char *name, int isvol, void *ud)
{
    DfCtx *d = ud;
    if (isvol) sl_add(&d->vols, str_printf("%s:", name));
    return 0;
}

static void df_col(SBuf *b, double v, int human, int w)
{
    char *s;
    if (human) s = u_size_str(v, 1);
    else {
        double k = v / 1024.0;
        s = xstrdup(u_ultoa((unsigned long)k));
    }
    sb_printf(b, " %*s", w, s);
    xfree(s);
}

int util_df(int argc, char **argv)
{
    static const char *const lo[] = { "human-readable", NULL };
    UGetopt g;
    DfCtx d;
    int c, i, human = 0, rc = 0;
    SBuf b;
    ug_init(&g, argc, argv, "hkPTa", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'h': case UG_LONG + 0: human = 1; break;
        case 'k': case 'P': case 'T': case 'a': break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: df [-hk] [FILE]...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    sl_init(&d.vols);
    if (g.nops == 0) {
        os_list_volumes(df_cb, &d);
        if (d.vols.n == 0) sl_adddup(&d.vols, ".");
    } else {
        for (i = 0; i < g.nops; i++) sl_adddup(&d.vols, g.ops[i]);
    }
    ug_free(&g);
    sb_init(&b);
    sh_printf(1, "%-16s %10s %10s %10s %4s\n", "Filesystem", human ? "Size" : "1K-blocks", "Used", "Avail", "Use%");
    for (i = 0; i < d.vols.n; i++) {
        char *n = path_native(d.vols.v[i]);
        double total = -1, fr = os_freespace(n, &total);
        xfree(n);
        if (fr < 0 || total <= 0) {
            if (g.nops) {
                sh_cmd_error(argv[0], "%s: cannot read file system information", d.vols.v[i]);
                rc = 1;
            }
            continue;
        }
        sb_clear(&b);
        sb_printf(&b, "%-16s", d.vols.v[i]);
        df_col(&b, total, human, 10);
        df_col(&b, total - fr, human, 10);
        df_col(&b, fr, human, 10);
        {
            double pct = (total - fr) * 100.0 / total;
            unsigned long p = (unsigned long)pct;
            if ((double)p < pct) p++;
            sb_printf(&b, " %3lu%%\n", p);
        }
        sh_write(1, b.s, b.len);
    }
    sb_free(&b);
    sl_free(&d.vols);
    return rc;
}

/* ================================================================== */
/* ln / realpath / stat                                                */
/* ================================================================== */

int util_ln(int argc, char **argv)
{
    (void)argc;
    sh_cmd_error(argv[0], "links are not supported on this system");
    return 1;
}

int util_realpath(int argc, char **argv)
{
    static const char *const lo[] = { "canonicalize-existing", "canonicalize-missing", "no-symlinks", "quiet", "zero", NULL };
    UGetopt g;
    int c, i, rc = 0, missing = 0, nosym = 0, quiet = 0, term = '\n';
    ug_init(&g, argc, argv, "emsqz", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'e': case UG_LONG + 0: missing = 0; break;
        case 'm': case UG_LONG + 1: missing = 1; break;
        case 's': case UG_LONG + 2: nosym = 1; break;
        case 'q': case UG_LONG + 3: quiet = 1; break;
        case 'z': case UG_LONG + 4: term = 0; break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: realpath [-ems] FILE...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    for (i = 0; i < g.nops; i++) {
        char *r = NULL;
        if (!nosym && sh_exists(g.ops[i])) {
            char *n = path_native(g.ops[i]);
            if (path_is_vroot(n)) r = xstrdup("/");
            else r = os_fullpath(n);
            xfree(n);
        }
        if (!r && (missing || nosym || 0)) r = path_absolute(g.ops[i]);
        if (!r) {
            if (!quiet) sh_cmd_error(argv[0], "%s: %s", g.ops[i], os_errstr());
            rc = 1;
            continue;
        }
        sh_puts(1, r);
        sh_putc(1, term);
        xfree(r);
    }
    ug_free(&g);
    return rc;
}

static const char *type_name(int t)
{
    switch (t) {
    case OS_T_DIR: return "directory";
    case OS_T_VOL: return "volume";
    case OS_T_LINK: return "symbolic link";
    }
    return "regular file";
}

int util_stat(int argc, char **argv)
{
    static const char *const lo[] = { "format=", "terse", NULL };
    UGetopt g;
    int c, i, rc = 0, terse = 0;
    const char *fmtstr = NULL;
    SBuf b;
    ug_init(&g, argc, argv, "c:tL", lo);
    while ((c = ug_next(&g)) != -1) {
        switch (c) {
        case 'c': case UG_LONG + 0: fmtstr = g.arg; break;
        case 't': case UG_LONG + 1: terse = 1; break;
        case 'L': break;
        case UG_LONG + 999: ug_free(&g); sh_printf(1, "Usage: stat [-t] [-c FORMAT] FILE...\n"); return 0;
        default: ug_free(&g); return u_usage(argv[0]);
        }
    }
    if (g.nops == 0) {
        sh_cmd_error(argv[0], "missing operand");
        ug_free(&g);
        return 1;
    }
    sb_init(&b);
    for (i = 0; i < g.nops; i++) {
        OsDirEnt e;
        char pr[9], date[40], *sz;
        ShTm tm;
        if (sh_stat(g.ops[i], &e) != 0) {
            sh_cmd_error(argv[0], "cannot stat '%s': %s", g.ops[i], os_errstr());
            rc = 1;
            continue;
        }
        prot_str(e.prot, pr);
        sh_localtime(e.mtime, &tm);
        sh_strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S %z", &tm);
        sz = u_size_str(e.size, 0);
        sb_clear(&b);
        if (fmtstr) {
            const char *f;
            for (f = fmtstr; *f; f++) {
                if (*f == '\\' && f[1]) {
                    f++;
                    sb_addc(&b, *f == 'n' ? '\n' : *f == 't' ? '\t' : *f);
                    continue;
                }
                if (*f != '%' || !f[1]) {
                    sb_addc(&b, *f);
                    continue;
                }
                switch (*++f) {
                case 'n': sb_adds(&b, g.ops[i]); break;
                case 'N': sb_printf(&b, "'%s'", g.ops[i]); break;
                case 's': sb_adds(&b, sz); break;
                case 'F': sb_adds(&b, type_name(e.type)); break;
                case 'A': sb_printf(&b, "%c%s", e.type == OS_T_DIR ? 'd' : '-', pr); break;
                case 'y': sb_adds(&b, date); break;
                case 'Y': sb_printf(&b, "%ld", e.mtime); break;
                case 'C': sb_adds(&b, e.comment); break;
                case '%': sb_addc(&b, '%'); break;
                default: sb_addc(&b, '%'); sb_addc(&b, *f); break;
                }
            }
            sb_addc(&b, '\n');
        } else if (terse) {
            sb_printf(&b, "%s %s %ld %s\n", g.ops[i], sz, e.mtime, pr);
        } else {
            sb_printf(&b, "  File: %s\n  Size: %-15s Type: %s\nProtect: %s\nModify: %s\n",
                      g.ops[i], sz, type_name(e.type), pr, date);
            if (e.comment[0]) sb_printf(&b, "Comment: %s\n", e.comment);
        }
        sh_write(1, b.s, b.len);
        xfree(sz);
    }
    sb_free(&b);
    ug_free(&g);
    return rc;
}
