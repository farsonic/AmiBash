/*
 * path.c - path handling.
 *
 * On the Amiga the shell accepts both native AmigaDOS paths and
 * Unix-style paths:
 *
 *   DH0:Work/src      native, absolute
 *   :Work             native, root of the current volume
 *   /dh0/Work/src     Unix style: first component is a volume/assign
 *   /                 the virtual root, containing all volumes/assigns
 *   ../src  ./x       resolved logically against $PWD
 *   src/x             relative, passed through unchanged
 *
 * Note that a leading "/" therefore means "root", as in bash, and not
 * "parent directory" as in AmigaDOS; use "../" for the parent.
 */
#include <string.h>
#include <stdlib.h>
#include "sh.h"

/* ---- generic normaliser (used for Amiga translation and tests) ---- */

typedef struct PComp { const char *s; int n; } PComp;

#define MAXCOMP 128

/* push components of s onto stack; stack[0] is the volume, depth 0 is
 * the virtual root */
static int push_comps(PComp *st, int depth, const char *s)
{
    while (*s) {
        const char *e;
        while (*s == '/') s++;
        if (!*s) break;
        e = s;
        while (*e && *e != '/') e++;
        if (e - s == 1 && s[0] == '.') {
            /* skip */
        } else if (e - s == 2 && s[0] == '.' && s[1] == '.') {
            if (depth > 0) depth--;
        } else if (depth < MAXCOMP) {
            st[depth].s = s;
            st[depth].n = (int)(e - s);
            depth++;
        }
        s = e;
    }
    return depth;
}

/* push an absolute path (native "VOL:x" or unix "/vol/x") */
static int push_abs(PComp *st, const char *p, const char *cwd)
{
    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');
    if (colon && (!slash || colon < slash)) {
        if (colon == p) {
            /* ":x" = root of current volume */
            const char *cc = cwd ? strchr(cwd, ':') : NULL;
            if (cc) {
                st[0].s = cwd;
                st[0].n = (int)(cc - cwd);
            } else {
                st[0].s = "SYS";
                st[0].n = 3;
            }
        } else {
            st[0].s = p;
            st[0].n = (int)(colon - p);
        }
        return push_comps(st, 1, colon + 1);
    }
    return push_comps(st, 0, p);
}

/* Amiga style normalisation of p relative to absolute shell path cwd.
 * Returns malloc'd "VOL:a/b" or "/" for the virtual root. */
char *path_amiga_normalize(const char *p, const char *cwd)
{
    PComp st[MAXCOMP];
    int depth, i;
    SBuf b;
    if (p[0] == '/' || strchr(p, ':')) {
        depth = push_abs(st, p, cwd);
    } else {
        depth = cwd ? push_abs(st, cwd, NULL) : 0;
        depth = push_comps(st, depth, p);
    }
    if (depth == 0) return xstrdup("/");
    sb_init(&b);
    sb_addn(&b, st[0].s, st[0].n);
    sb_addc(&b, ':');
    for (i = 1; i < depth; i++) {
        if (i > 1) sb_addc(&b, '/');
        sb_addn(&b, st[i].s, st[i].n);
    }
    return sb_take(&b);
}

/* POSIX logical normalisation (bash cd semantics) */
static char *path_posix_normalize(const char *p, const char *cwd)
{
    PComp comps[MAXCOMP];
    int nc = 0, i;
    SBuf b;
    if (p[0] != '/' && cwd) nc = push_comps(comps, 0, cwd);
    nc = push_comps(comps, nc, p);
    sb_init(&b);
    if (nc == 0) sb_addc(&b, '/');
    for (i = 0; i < nc; i++) {
        sb_addc(&b, '/');
        sb_addn(&b, comps[i].s, comps[i].n);
    }
    return sb_take(&b);
}

static int has_dot_comp(const char *p)
{
    const char *s = p;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/') e++;
        if ((e - s == 1 && s[0] == '.') || (e - s == 2 && s[0] == '.' && s[1] == '.'))
            return 1;
        s = *e ? e + 1 : e;
    }
    return 0;
}

char *path_native(const char *p)
{
#ifdef AMIGA
    const char *colon;
    static const char *const map[][2] = {
        {"/dev/null", "NIL:"}, {"/dev/tty", "*"}, {"/dev/console", "*"},
        {"/tmp", "T:"}, {"/var/tmp", "T:"}, {"/bin", "C:"}, {"/usr/bin", "C:"},
        {"/usr/local/bin", "C:"}, {"/etc", "S:"}, {"/home", "SYS:"}, {NULL, NULL}
    };
    int i;
    if (!sh.sh_pathconv) return xstrdup(p);
    if (!*p) return xstrdup(p);
    /* well-known Unix locations */
    for (i = 0; map[i][0]; i++) {
        size_t l = strlen(map[i][0]);
        if (strncmp(p, map[i][0], l) == 0 && (p[l] == 0 || p[l] == '/')) {
            const char *rest = p[l] ? p + l + 1 : "";
            const char *to = map[i][1];
            if (strcmp(to, "T:") == 0) to = os_tmpdir();    /* T: may be missing */
            if (!*rest) return xstrdup(to);
            return path_amiga_normalize(rest, to);
        }
    }
    colon = strchr(p, ':');
    if (colon) {
        const char *slash = strchr(p, '/');
        if (slash && slash < colon) {
            /* "/foo:bar" is odd; treat as unix path */
            return path_amiga_normalize(p, sh.pwd);
        }
        if (!has_dot_comp(colon + 1) && colon[1] != '/') return xstrdup(p);
        return path_amiga_normalize(p, sh.pwd);
    }
    if (p[0] != '/' && !has_dot_comp(p) && sh.pwd && strcmp(sh.pwd, "/") != 0)
        return xstrdup(p);
    return path_amiga_normalize(p, sh.pwd);
#else
    return xstrdup(p);
#endif
}

char *path_absolute(const char *p)
{
#ifdef AMIGA
    return path_amiga_normalize(p, sh.pwd);
#else
    return path_posix_normalize(p, sh.pwd);
#endif
}

int path_is_vroot(const char *native)
{
#ifdef AMIGA
    return native[0] == '/' && native[1] == 0;
#else
    (void)native;
    return 0;
#endif
}

char *path_join(const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n == 0) return xstrdup(name);
    if (dir[n - 1] == '/' || dir[n - 1] == ':') return str_cat(dir, name);
    return str_printf("%s/%s", dir, name);
}

const char *path_basename(const char *p)
{
    const char *b = p, *s;
    for (s = p; *s; s++)
        if ((*s == '/' || *s == ':') && s[1]) b = s + 1;
    return b;
}

char *path_dirname(const char *p)
{
    int n = (int)strlen(p);
    while (n > 1 && p[n - 1] == '/') n--;
    while (n > 0 && p[n - 1] != '/' && p[n - 1] != ':') n--;
    if (n == 0) return xstrdup(".");
    if (p[n - 1] == ':') return xstrndup(p, n);
    while (n > 1 && p[n - 1] == '/') n--;
    return xstrndup(p, n);
}

int path_has_dir(const char *p)
{
    if (p[0] == ':' && !p[1]) return 0;        /* the ":" builtin */
    return strchr(p, '/') != NULL || strchr(p, ':') != NULL;
}

int path_is_absolute(const char *p)
{
    return p[0] == '/' || strchr(p, ':') != NULL;
}

int sh_stat(const char *path, OsDirEnt *e)
{
    char *np = path_native(path);
    int r;
    if (path_is_vroot(np)) {
        memset(e, 0, sizeof(*e));
        strcpy(e->name, "/");
        e->type = OS_T_DIR;
        e->prot = OS_P_READ | OS_P_EXEC;
        xfree(np);
        return 0;
    }
    r = os_stat(np, e);
    xfree(np);
    return r;
}

void *sh_opendir(const char *path)
{
    char *np = path_native(path);
    void *d = os_opendir(np);
    xfree(np);
    return d;
}

int sh_mkdir(const char *path)
{
    char *np = path_native(path);
    int r = os_mkdir(np);
    xfree(np);
    return r;
}

int sh_remove(const char *path)
{
    char *np = path_native(path);
    int r = os_remove(np);
    xfree(np);
    return r;
}

int sh_rename(const char *from, const char *to)
{
    char *a = path_native(from), *b = path_native(to);
    int r = os_rename(a, b);
    xfree(a);
    xfree(b);
    return r;
}

int sh_is_dir(const char *path)
{
    OsDirEnt e;
    return sh_stat(path, &e) == 0 && (e.type == OS_T_DIR || e.type == OS_T_VOL);
}

int sh_exists(const char *path)
{
    OsDirEnt e;
    return sh_stat(path, &e) == 0;
}
