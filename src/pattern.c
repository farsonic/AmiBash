/*
 * pattern.c - shell pattern matching and pathname expansion.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

static int fold(int c, int nocase)
{
    return nocase ? tolower((unsigned char)c) : (unsigned char)c;
}

static int class_match(const char *cls, int n, int c)
{
#define CL(name, test) if (n == (int)strlen(name) && strncmp(cls, name, n) == 0) return test
    CL("alpha", isalpha(c) != 0);
    CL("digit", isdigit(c) != 0);
    CL("alnum", isalnum(c) != 0);
    CL("upper", isupper(c) != 0);
    CL("lower", islower(c) != 0);
    CL("space", isspace(c) != 0);
    CL("blank", c == ' ' || c == '\t');
    CL("punct", ispunct(c) != 0);
    CL("print", isprint(c) != 0);
    CL("graph", isgraph(c) != 0);
    CL("cntrl", iscntrl(c) != 0);
    CL("xdigit", isxdigit(c) != 0);
    CL("word", isalnum(c) || c == '_');
#undef CL
    return 0;
}

/* match bracket expression at p (after '['); returns pointer after ']'
 * or NULL if malformed; *matched set */
static const char *bracket(const char *p, const char *pe, int c, int nocase, int *matched)
{
    int neg = 0, ok = 0, first = 1;
    int fc = fold(c, nocase);
    if (p < pe && (*p == '!' || *p == '^')) {
        neg = 1;
        p++;
    }
    while (p < pe) {
        int lo, hi;
        if (*p == ']' && !first) {
            *matched = ok != neg;
            return p + 1;
        }
        first = 0;
        if (*p == '[' && p + 1 < pe && p[1] == ':') {
            const char *e = p + 2;
            while (e + 1 < pe && !(e[0] == ':' && e[1] == ']')) e++;
            if (e + 1 < pe) {
                if (class_match(p + 2, (int)(e - p - 2), (unsigned char)c)) ok = 1;
                else if (nocase && class_match(p + 2, (int)(e - p - 2), toupper((unsigned char)c))) ok = 1;
                p = e + 2;
                continue;
            }
        }
        if (*p == '\\' && p + 1 < pe) p++;
        lo = (unsigned char)*p++;
        hi = lo;
        if (p + 1 < pe && *p == '-' && p[1] != ']') {
            p++;
            if (*p == '\\' && p + 1 < pe) p++;
            hi = (unsigned char)*p++;
        }
        if (nocase) {
            if ((fold(lo, 1) <= fc && fc <= fold(hi, 1)) ||
                (lo <= toupper(fc) && toupper(fc) <= hi) || (lo <= fc && fc <= hi))
                ok = 1;
        } else if (lo <= c && c <= hi) {
            ok = 1;
        }
    }
    return NULL;
}

/* find end of extglob group: p at '(' ; returns pointer at matching ')' */
static const char *ext_end(const char *p, const char *pe)
{
    int depth = 0;
    for (; p < pe; p++) {
        if (*p == '\\') {
            p++;
            continue;
        }
        if (*p == '(') depth++;
        else if (*p == ')' && --depth == 0) return p;
    }
    return NULL;
}

static int match(const char *p, const char *pe, const char *s, const char *se, int flags);

/* does s..se match any alternative of group g..ge (contents between parens)? */
static int alt_match(const char *g, const char *ge, const char *s, const char *se, int flags)
{
    const char *a = g;
    int depth = 0;
    const char *q;
    for (q = g; q <= ge; q++) {
        if (q < ge && *q == '\\') {
            q++;
            continue;
        }
        if (q < ge && *q == '(') depth++;
        else if (q < ge && *q == ')') depth--;
        else if (q == ge || (*q == '|' && depth == 0)) {
            if (match(a, q, s, se, flags)) return 1;
            a = q + 1;
        }
    }
    return 0;
}

/* repeated alternatives: s..se is a concatenation of >= min matches */
static int rep_match(const char *g, const char *ge, const char *s, const char *se, int flags, int min)
{
    const char *k;
    if (s == se) return min == 0;
    for (k = s + 1; k <= se; k++) {
        if (alt_match(g, ge, s, k, flags) && rep_match(g, ge, k, se, flags, 0)) return 1;
    }
    return 0;
}

static int match(const char *p, const char *pe, const char *s, const char *se, int flags)
{
    int nocase = flags & PM_NOCASE;
    while (p < pe) {
        int c = (unsigned char)*p;
        if (sh.sh_extglob && p + 1 < pe && p[1] == '(' && strchr("@*+?!", c)) {
            const char *ge = ext_end(p + 1, pe);
            if (ge) {
                const char *g = p + 2, *rest = ge + 1, *k;
                for (k = s; k <= se; k++) {
                    int m;
                    switch (c) {
                    case '@': m = alt_match(g, ge, s, k, flags); break;
                    case '?': m = k == s || alt_match(g, ge, s, k, flags); break;
                    case '*': m = rep_match(g, ge, s, k, flags, 0); break;
                    case '+': m = rep_match(g, ge, s, k, flags, 1); break;
                    default: m = !alt_match(g, ge, s, k, flags); break;
                    }
                    if (m && match(rest, pe, k, se, flags)) return 1;
                }
                return 0;
            }
        }
        switch (c) {
        case '*': {
            const char *k;
            while (p < pe && *p == '*') p++;
            if (p == pe) return 1;
            for (k = s; k <= se; k++)
                if (match(p, pe, k, se, flags)) return 1;
            return 0;
        }
        case '?':
            if (s >= se) return 0;
            p++;
            s++;
            break;
        case '[': {
            int m = 0;
            const char *e;
            if (s >= se) return 0;
            e = bracket(p + 1, pe, (unsigned char)*s, nocase, &m);
            if (!e) {
                /* literal '[' */
                if (*s != '[') return 0;
                p++;
                s++;
                break;
            }
            if (!m) return 0;
            p = e;
            s++;
            break;
        }
        case '\\':
            if (p + 1 < pe) p++;
            /* fall through */
        default:
            if (s >= se || fold(*p, nocase) != fold(*s, nocase)) return 0;
            p++;
            s++;
        }
    }
    return s == se;
}

int pat_match(const char *pat, const char *str, int flags)
{
    return match(pat, pat + strlen(pat), str, str + strlen(str), flags);
}

int pat_prefix(const char *pat, const char *str, int longest, int flags)
{
    int n = (int)strlen(str), i;
    const char *pe = pat + strlen(pat);
    if (longest) {
        for (i = n; i >= 0; i--)
            if (match(pat, pe, str, str + i, flags)) return i;
    } else {
        for (i = 0; i <= n; i++)
            if (match(pat, pe, str, str + i, flags)) return i;
    }
    return -1;
}

int pat_suffix(const char *pat, const char *str, int longest, int flags)
{
    int n = (int)strlen(str), i;
    const char *pe = pat + strlen(pat);
    if (longest) {
        for (i = 0; i <= n; i++)
            if (match(pat, pe, str + i, str + n, flags)) return n - i;
    } else {
        for (i = n; i >= 0; i--)
            if (match(pat, pe, str + i, str + n, flags)) return n - i;
    }
    return -1;
}

int pat_has_glob(const char *pat)
{
    const char *p;
    for (p = pat; *p; p++) {
        if (*p == '\\') {
            if (p[1]) p++;
            continue;
        }
        if (*p == '*' || *p == '?') return 1;
        if (*p == '[' && strchr(p + 1, ']')) return 1;
        if (sh.sh_extglob && p[1] == '(' && strchr("@+!", *p)) return 1;
    }
    return 0;
}

char *pat_unescape(const char *pat)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (; *pat; pat++) {
        if (*pat == '\\' && pat[1]) pat++;
        sb_addc(&b, *pat);
    }
    return sb_take(&b);
}

char *pat_escape(const char *s)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (; *s; s++) {
        if (strchr("*?[]\\(|)!@+", *s)) sb_addc(&b, '\\');
        sb_addc(&b, *s);
    }
    return sb_take(&b);
}

/* ---------------- pathname expansion ---------------- */

/* split off the root part of a pattern: "/" "DH0:" ":" or "" */
static int root_len(const char *pat)
{
    const char *c;
#ifdef AMIGA
    c = strchr(pat, ':');
    if (c) {
        const char *s = strchr(pat, '/');
        if (!s || c < s) {
            /* volume names may not contain glob chars here */
            return (int)(c - pat) + 1;
        }
    }
#else
    (void)c;
#endif
    return pat[0] == '/' ? 1 : 0;
}

static void glob_rec(const char *base, const char *rest, StrList *out, int flags);

/* globstar "**": zero or more directory levels below base */
static void glob_star(const char *base, const char *slash, StrList *out, int flags, int depth)
{
    void *d;
    OsDirEnt e;
    StrList names;
    int i;
    if (depth == 0) {
        if (slash) glob_rec(base, slash, out, flags);
        else if (*base) sl_add(out, str_cat(base, "/"));
    }
    if (depth > 64) return;
    d = sh_opendir(*base ? base : ".");
    if (!d) return;
    sl_init(&names);
    while (os_readdir(d, &e)) {
        if (e.name[0] == '.' && (!(flags & PM_DOTGLOB) || !e.name[1] ||
                                 (e.name[1] == '.' && !e.name[2]))) continue;
        sl_adddup(&names, e.name);
        sl_adddup(&names, e.type == OS_T_DIR ? "d" : "f");
    }
    os_closedir(d);
    for (i = 0; i + 1 < names.n; i += 2) {
        char *np = *base ? path_join(base, names.v[i]) : xstrdup(names.v[i]);
        if (names.v[i + 1][0] == 'd') {
            if (slash) glob_rec(np, slash, out, flags);
            else sl_adddup(out, np);
            glob_star(np, slash, out, flags, depth + 1);
        } else if (!slash) {
            sl_adddup(out, np);
        }
        xfree(np);
    }
    sl_free(&names);
}

static void glob_rec(const char *base, const char *rest, StrList *out, int flags)
{
    const char *slash;
    char *comp;
    int clen;
    if (*rest == '/' && *base) {
        while (*rest == '/') rest++;
        if (!*rest) {
            /* pattern ended in '/': keep it ("*\/" -> "dir/") */
            sl_add(out, str_cat(base, "/"));
            return;
        }
    }
    while (*rest == '/') rest++;
    if (!*rest) {
        sl_adddup(out, base);
        return;
    }
    slash = strchr(rest, '/');
    clen = slash ? (int)(slash - rest) : (int)strlen(rest);
    comp = xstrndup(rest, clen);
    if (sh.sh_globstar && strcmp(comp, "**") == 0) {
        glob_star(base, slash, out, flags, 0);
    } else if (!pat_has_glob(comp)) {
        char *lit = pat_unescape(comp);
        char *np = *base ? path_join(base, lit) : xstrdup(lit);
        xfree(lit);
        if (slash) {
            char *nb = xstrdup(np);
            if (sh_is_dir(np)) glob_rec(nb, slash, out, flags);
            xfree(nb);
        } else if (sh_exists(np)) {
            sl_adddup(out, np);
        }
        xfree(np);
    } else {
        void *d = sh_opendir(*base ? base : ".");
        if (d) {
            OsDirEnt e;
            StrList names;
            int i;
            sl_init(&names);
            while (os_readdir(d, &e)) {
                if (e.name[0] == '.' && comp[0] != '.' && !(flags & PM_DOTGLOB)) continue;
                if (slash && e.type == OS_T_FILE) continue;
                if (pat_match(comp, e.name, flags)) sl_adddup(&names, e.name);
            }
            os_closedir(d);
            for (i = 0; i < names.n; i++) {
                char *np = *base ? path_join(base, names.v[i]) : xstrdup(names.v[i]);
                if (slash) glob_rec(np, slash, out, flags);
                else sl_add(out, xstrdup(np));
                xfree(np);
            }
            sl_free(&names);
        }
    }
    xfree(comp);
}

/* GLOBSORT (bash 5.3): [+|-]name|numeric|size|mtime|atime|ctime|blocks|nosort.
 * AmigaOS keeps one date per file, so atime and ctime are its mtime, and
 * blocks come from the size */
typedef struct GsEnt { char *name; double key; } GsEnt;
static int gs_key, gs_rev;       /* 0 name, 1 numeric, 2 by key        */

static int gs_alldigits(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static int gs_cmp(const void *a, const void *b)
{
    const GsEnt *x = a, *y = b;
    int r = 0;
    if (gs_key == 1) {
        int dx = gs_alldigits(x->name), dy = gs_alldigits(y->name);
        if (dx != dy) r = dx ? -1 : 1;              /* the all-digit names first */
        else if (dx) {
            const char *p = x->name, *q = y->name;
            while (*p == '0' && p[1]) p++;
            while (*q == '0' && q[1]) q++;
            r = strlen(p) != strlen(q) ? (strlen(p) < strlen(q) ? -1 : 1) : strcmp(p, q);
        }
    } else if (gs_key == 2 && x->key != y->key)
        r = x->key < y->key ? -1 : 1;
    if (!r) r = strcmp(x->name, y->name);
    return gs_rev ? -r : r;
}

/* sort glob results as GLOBSORT says; 0: leave them unsorted */
static int glob_sort(StrList *res)
{
    const char *g = var_get("GLOBSORT"), *k;
    GsEnt *e;
    int i, what = 0;           /* 0 name 1 numeric 2 size 3 mtime 4 blocks */
    gs_rev = 0;
    if (!g || !*g) {
        sl_sort(res, 0);         /* bash (C locale) sorts by byte value */
        return 1;
    }
    k = g;
    if (*k == '+') k++;
    else if (*k == '-') k++, gs_rev = 1;
    if (!strcmp(k, "nosort")) return 0;
    if (!*k || !strcmp(k, "name")) what = 0;
    else if (!strcmp(k, "numeric")) what = 1;
    else if (!strcmp(k, "size")) what = 2;
    else if (!strcmp(k, "mtime") || !strcmp(k, "atime") || !strcmp(k, "ctime")) what = 3;
    else if (!strcmp(k, "blocks")) what = 4;
    else gs_rev = 0;             /* invalid: the usual order */
    if (res->n < 2) return 1;
    e = xmalloc(res->n * sizeof(GsEnt));
    for (i = 0; i < res->n; i++) {
        OsDirEnt st;
        e[i].name = res->v[i];
        e[i].key = 0;
        if (what >= 2 && os_stat(res->v[i], &st) == 0)
            e[i].key = what == 3 ? (double)st.mtime : what == 4 ? (double)(long)((st.size + 511) / 512) : st.size;
    }
    gs_key = what == 0 ? 0 : what == 1 ? 1 : 2;
    qsort(e, res->n, sizeof(GsEnt), gs_cmp);
    for (i = 0; i < res->n; i++) res->v[i] = e[i].name;
    xfree(e);
    return 1;
}

int glob_expand(const char *pat, StrList *out, int flags)
{
    StrList res;
    int rl = root_len(pat), i;
    char *base = xstrndup(pat, rl);
    sl_init(&res);
    if (sh.sh_nocaseglob) flags |= PM_NOCASE;
    if (sh.sh_dotglob) flags |= PM_DOTGLOB;
    glob_rec(base, pat + rl, &res, flags);
    xfree(base);
    glob_sort(&res);
    for (i = 0; i < res.n; i++) {
        sl_add(out, res.v[i]);
        res.v[i] = NULL;
    }
    i = res.n;
    xfree(res.v);
    return i;
}
