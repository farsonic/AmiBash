/*
 * expand.c - word expansion: brace, tilde, parameter, command and
 * arithmetic substitution, field splitting, pathname expansion and
 * quote removal.
 *
 * A word is expanded into a character buffer with a parallel buffer of
 * per-character flags recording quoting, which drives field splitting
 * and globbing afterwards.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

#define Q_QUOTED 1      /* literal: no split, no glob                  */
#define Q_EXP    2      /* result of unquoted expansion: may split     */
#define Q_MARK   4      /* placeholder: word had quotes (keeps "")     */
#define Q_SEP    8      /* hard field separator ("$@")                 */

int expand_error;

typedef struct XCtx {
    SBuf s, q;
    int flags;
    int multi;          /* count of multi-value expansions seen        */
    int litfl;          /* flags for unquoted literal chars (Q_EXP in
                           the word of an unquoted ${x:-word})          */
} XCtx;

static int xword(XCtx *c, const char *s, const char *end, int in_dq);

static void put(XCtx *c, int ch, int fl)
{
    if (c->s.len + 2 < c->s.cap && c->q.len + 2 < c->q.cap) {
        /* inline fast path */
        c->s.s[c->s.len++] = (char)ch;
        c->s.s[c->s.len] = 0;
        c->q.s[c->q.len++] = (char)fl;
        c->q.s[c->q.len] = 0;
        return;
    }
    sb_addc(&c->s, ch);
    sb_addc(&c->q, fl);
}

static void putn(XCtx *c, const char *str, int n, int fl)
{
    int i;
    for (i = 0; i < n; i++) put(c, (unsigned char)str[i], fl);
}

static void putstr(XCtx *c, const char *str, int fl)
{
    putn(c, str, (int)strlen(str), fl);
}

static void xinit(XCtx *c, int flags)
{
    sb_init(&c->s);
    sb_init(&c->q);
    c->flags = flags;
    c->multi = 0;
    c->litfl = 0;
}

static void xfree_ctx(XCtx *c)
{
    sb_free(&c->s);
    sb_free(&c->q);
}

static const char *get_ifs(void)
{
    const char *ifs = var_get("IFS");
    return ifs ? ifs : " \t\n";
}

/* --------------------------------------------------------------- */
/* errors                                                           */

/* text being expanded (word, "..." contents, $((...)) or ${x:-word}):
 * bash names it in "bad substitution" messages */
static const char *xw_s, *xw_e;

static void bad_subst(const char *s, int n)
{
    if (xw_s && xw_e >= xw_s) {
        s = xw_s;
        n = (int)(xw_e - xw_s);
    }
    sh_error("%.*s: bad substitution", n, s);
    expand_error = 1;
}

/* --------------------------------------------------------------- */
/* parameter values                                                  */

typedef struct PVal {
    StrList v;
    int multi;          /* $@ $* ${a[@]} ${a[*]}                        */
    int star;           /* $* or [*]                                     */
    int set;            /* parameter is set                              */
} PVal;

static void pv_init(PVal *p)
{
    sl_init(&p->v);
    p->multi = p->star = p->set = 0;
}

static char *opt_flags_string(void)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (sh.opt_allexport) sb_addc(&b, 'a');
    if (sh.opt_braceexpand) sb_addc(&b, 'B');
    if (sh.opt_errexit) sb_addc(&b, 'e');
    if (sh.opt_noglob) sb_addc(&b, 'f');
    if (sh.opt_hashall) sb_addc(&b, 'h');
    if (sh.interactive) sb_addc(&b, 'i');
    if (sh.opt_monitor) sb_addc(&b, 'm');
    if (sh.opt_noexec) sb_addc(&b, 'n');
    if (sh.opt_nounset) sb_addc(&b, 'u');
    if (sh.opt_verbose) sb_addc(&b, 'v');
    if (sh.opt_xtrace) sb_addc(&b, 'x');
    if (sh.opt_histexpand) sb_addc(&b, 'H');
    if (sh.opt_noclobber) sb_addc(&b, 'C');
    return sb_take(&b);
}

/* evaluate an array subscript to a key string for var v */
static char *subscript_key(Var *v, const char *sub, int assoc)
{
    if (assoc) return expand_str(sub, X_NOSPLIT | X_NOGLOB);
    {
        ShInt idx;
        if (arith_expand_eval(sub, &idx)) {
            expand_error = 1;
            return NULL;
        }
        if (idx < 0 && v) {
            /* negative index counts from the end */
            int n = v->alen;
            if (n > 0) idx += v->arr[n - 1].idx + 1;
        }
        return str_itoa(idx);
    }
}

/* fetch values for "name" or "name[sub]" */
static void get_values(const char *name, int nlen, const char *sub, PVal *pv)
{
    char nm[256];
    pv_init(pv);
    if (nlen >= (int)sizeof(nm)) nlen = sizeof(nm) - 1;
    memcpy(nm, name, nlen);
    nm[nlen] = 0;
    if (!sub) {
        if (strcmp(nm, "@") == 0 || strcmp(nm, "*") == 0) {
            int i;
            for (i = 0; i < sh_params.n; i++) sl_adddup(&pv->v, sh_params.v[i]);
            pv->multi = 1;
            pv->star = nm[0] == '*';
            pv->set = sh_params.n > 0;
            return;
        }
        if (strcmp(nm, "#") == 0) {
            sl_add(&pv->v, str_itoa(sh_params.n));
        } else if (strcmp(nm, "?") == 0) {
            sl_add(&pv->v, str_itoa(sh.laststatus));
        } else if (strcmp(nm, "$") == 0) {
            sl_add(&pv->v, str_itoa(sh.pid));
        } else if (strcmp(nm, "!") == 0) {
            if (sh.lastbg <= 0) return;
            sl_add(&pv->v, str_itoa(sh.lastbg));
        } else if (strcmp(nm, "-") == 0) {
            sl_add(&pv->v, opt_flags_string());
        } else if (strcmp(nm, "0") == 0) {
            sl_adddup(&pv->v, sh.name ? sh.name : "bash");
        } else if (isdigit((unsigned char)nm[0])) {
            int i = atoi(nm);
            if (i < 1 || i > sh_params.n) return;
            sl_adddup(&pv->v, sh_params.v[i - 1]);
        } else {
            Var *v = var_lookup(nm);
            const char *val;
            if (v && (v->flags & (V_ARRAY | V_ASSOC))) {
                val = var_arr_get(v, (v->flags & V_ASSOC) ? "0" : "0");
                if (!val) return;
                sl_adddup(&pv->v, val);
            } else {
                val = var_get(nm);
                if (!val) return;
                sl_adddup(&pv->v, val);
            }
        }
        pv->set = 1;
        return;
    }
    /* array reference */
    {
        Var *v = var_lookup(nm);
        if ((strcmp(sub, "@") == 0 || strcmp(sub, "*") == 0)) {
            pv->multi = 1;
            pv->star = sub[0] == '*';
            if (v && (v->flags & (V_ARRAY | V_ASSOC))) {
                var_arr_values(v, &pv->v, 0);
            } else if (v && !(v->flags & V_UNSET)) {
                const char *val = var_get(nm);
                if (val) sl_adddup(&pv->v, val);
            }
            pv->set = pv->v.n > 0;
            return;
        }
        if (v && (v->flags & (V_ARRAY | V_ASSOC))) {
            char *key = subscript_key(v, sub, (v->flags & V_ASSOC) != 0);
            const char *val;
            if (!key) return;
            val = var_arr_get(v, key);
            xfree(key);
            if (val) {
                sl_adddup(&pv->v, val);
                pv->set = 1;
            }
        } else {
            /* scalar: index 0 is the value */
            char *key = subscript_key(v, sub, 0);
            if (!key) return;
            if (strcmp(key, "0") == 0) {
                const char *val = var_get(nm);
                if (val) {
                    sl_adddup(&pv->v, val);
                    pv->set = 1;
                }
            }
            xfree(key);
        }
    }
}

/* --------------------------------------------------------------- */
/* helpers for ${...} operators                                       */

static int is_special_param(int c)
{
    return c == '@' || c == '*' || c == '#' || c == '?' || c == '-' || c == '$' || c == '!' || c == '0';
}

/* parse parameter name at s (inside ${ }); returns length, 0 if none */
static int param_name_len(const char *s, const char *end)
{
    const char *p = s;
    if (p >= end) return 0;
    if (isalpha((unsigned char)*p) || *p == '_') {
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
        return (int)(p - s);
    }
    if (isdigit((unsigned char)*p)) {
        while (p < end && isdigit((unsigned char)*p)) p++;
        return (int)(p - s);
    }
    if (is_special_param((unsigned char)*p)) return 1;
    return 0;
}

/* find matching ']' for subscript starting after '[' */
static const char *sub_end(const char *s, const char *end)
{
    int depth = 1;
    while (s < end) {
        if (*s == '\\' && s + 1 < end) {
            s += 2;
            continue;
        }
        if (*s == '[') depth++;
        else if (*s == ']' && --depth == 0) return s;
        s++;
    }
    return NULL;
}

/* expand a word fragment as a pattern (quoted chars escaped) */
static char *expand_pattern_n(const char *s, const char *end, int in_dq)
{
    XCtx c;
    int i;
    SBuf out;
    xinit(&c, X_NOSPLIT | X_NOGLOB);
    xword(&c, s, end, in_dq);
    sb_init(&out);
    sb_addn(&out, "", 0);
    for (i = 0; i < c.s.len; i++) {
        int fl = (unsigned char)c.q.s[i];
        int ch = (unsigned char)c.s.s[i];
        if (fl & (Q_MARK | Q_SEP)) {
            if (fl & Q_SEP) sb_addc(&out, ' ');
            continue;
        }
        if ((fl & Q_QUOTED) && strchr("*?[]\\(|)!@+", ch)) sb_addc(&out, '\\');
        sb_addc(&out, ch);
    }
    xfree_ctx(&c);
    return sb_take(&out);
}

/* expand the replacement of ${x/p/rep}: quoted & and \ get a backslash */
static char *expand_replacement(const char *s, const char *end)
{
    XCtx c;
    int i;
    SBuf out;
    xinit(&c, X_NOSPLIT | X_NOGLOB);
    xword(&c, s, end, 0);
    sb_init(&out);
    sb_addn(&out, "", 0);
    for (i = 0; i < c.s.len; i++) {
        int fl = (unsigned char)c.q.s[i];
        int ch = (unsigned char)c.s.s[i];
        if (fl & Q_MARK) continue;
        if (fl & Q_SEP) {
            sb_addc(&out, ' ');
            continue;
        }
        if (ch == '\\' || (ch == '&' && (fl & Q_QUOTED))) sb_addc(&out, '\\');
        sb_addc(&out, ch);
    }
    xfree_ctx(&c);
    return sb_take(&out);
}

/* expand fragment to a plain string (no split/glob) */
static char *expand_frag(const char *s, const char *end, int in_dq)
{
    XCtx c;
    int i;
    SBuf out;
    xinit(&c, X_NOSPLIT | X_NOGLOB);
    xword(&c, s, end, in_dq);
    sb_init(&out);
    sb_addn(&out, "", 0);
    for (i = 0; i < c.s.len; i++) {
        int fl = (unsigned char)c.q.s[i];
        if (fl & Q_MARK) continue;
        if (fl & Q_SEP) {
            sb_addc(&out, ' ');
            continue;
        }
        sb_addc(&out, c.s.s[i]);
    }
    xfree_ctx(&c);
    return sb_take(&out);
}

static char *case_mod(const char *val, const char *pat, int upper, int all)
{
    char *r = xstrdup(val);
    char *p;
    for (p = r; *p; p++) {
        char one[2];
        one[0] = *p;
        one[1] = 0;
        if (!pat || !*pat || pat_match(pat, one, 0)) {
            int ch = (unsigned char)*p;
            if (upper == 2) ch = isupper(ch) ? tolower(ch) : toupper(ch);
            else ch = upper ? toupper(ch) : tolower(ch);
            *p = (char)ch;
        }
        if (!all) break;
    }
    return r;
}

/* append replacement text: unquoted & is the matched text (bash 5.2
 * patsub_replacement); rep has quoted & and \ escaped by a backslash */
static void add_rep(SBuf *b, const char *rep, const char *m, int mlen)
{
    for (; *rep; rep++) {
        if (*rep == '\\' && rep[1]) sb_addc(b, *++rep);
        else if (*rep == '&') sb_addn(b, m, mlen);
        else sb_addc(b, *rep);
    }
}

/* ${x/pat/rep} family; mode 0 first, 1 all, 2 prefix, 3 suffix */
static char *pat_replace(const char *val, const char *pat, const char *rep, int mode)
{
    SBuf b;
    int n = (int)strlen(val), i;
    int nocase = sh.sh_nocasematch ? PM_NOCASE : 0;
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (mode == 2) {
        int k = pat_prefix(pat, val, 1, nocase);
        if (k >= 0 && !(k == 0 && *pat)) {
            add_rep(&b, rep, val, k);
            sb_adds(&b, val + k);
        } else if (k == 0) {
            add_rep(&b, rep, val, 0);
            sb_adds(&b, val);
        } else {
            sb_adds(&b, val);
        }
        return sb_take(&b);
    }
    if (mode == 3) {
        int k = pat_suffix(pat, val, 1, nocase);
        if (k >= 0) {
            sb_addn(&b, val, n - k);
            add_rep(&b, rep, val + n - k, k);
        } else {
            sb_adds(&b, val);
        }
        return sb_take(&b);
    }
    if (!*pat) return xstrdup(val);
    i = 0;
    while (i <= n) {
        int k = pat_prefix(pat, val + i, 1, nocase);
        if (k > 0) {
            add_rep(&b, rep, val + i, k);
            i += k;
            if (mode == 0) {
                sb_adds(&b, val + i);
                return sb_take(&b);
            }
            continue;
        }
        if (i < n) sb_addc(&b, val[i]);
        i++;
    }
    return sb_take(&b);
}

/* quote a string for reuse as shell input ('...' style, as bash @Q) */
static char *quote_q(const char *s)
{
    SBuf b;
    const unsigned char *p;
    sb_init(&b);
    for (p = (const unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7f) break;
    if (*p) {
        /* control characters: $'...' like bash */
        sb_adds(&b, "$'");
        for (p = (const unsigned char *)s; *p; p++) {
            switch (*p) {
            case '\n': sb_adds(&b, "\\n"); break;
            case '\t': sb_adds(&b, "\\t"); break;
            case '\r': sb_adds(&b, "\\r"); break;
            case 7: sb_adds(&b, "\\a"); break;
            case 8: sb_adds(&b, "\\b"); break;
            case 27: sb_adds(&b, "\\E"); break;
            case 12: sb_adds(&b, "\\f"); break;
            case 11: sb_adds(&b, "\\v"); break;
            case '\\': sb_adds(&b, "\\\\"); break;
            case '\'': sb_adds(&b, "\\'"); break;
            default:
                if (*p < 0x20 || *p == 0x7f) sb_printf(&b, "\\%03o", *p);
                else sb_addc(&b, *p);
            }
        }
        sb_addc(&b, '\'');
        return sb_take(&b);
    }
    sb_addc(&b, '\'');
    for (; *s; s++) {
        if (*s == '\'') sb_adds(&b, "'\\''");
        else sb_addc(&b, *s);
    }
    sb_addc(&b, '\'');
    return sb_take(&b);
}

/* length of the last ansi_decode() result (it may contain NUL bytes) */
int ansi_decode_len;

/* decode backslash escapes ($'...', echo -e, printf %b, @E) */
char *ansi_decode(const char *s, int n, int echo_style, int *stop)
{
    SBuf b;
    const char *e = s + n;
    char *r;
    sb_init(&b);
    sb_addn(&b, "", 0);
    if (stop) *stop = 0;
    while (s < e) {
        int c = (unsigned char)*s++;
        if (c != '\\' || s >= e) {
            sb_addc(&b, c);
            continue;
        }
        c = (unsigned char)*s++;
        switch (c) {
        case 'a': sb_addc(&b, 7); break;
        case 'b': sb_addc(&b, 8); break;
        case 'e': case 'E': sb_addc(&b, 27); break;
        case 'f': sb_addc(&b, 12); break;
        case 'n': sb_addc(&b, '\n'); break;
        case 'r': sb_addc(&b, '\r'); break;
        case 't': sb_addc(&b, '\t'); break;
        case 'v': sb_addc(&b, 11); break;
        case '\\': sb_addc(&b, '\\'); break;
        case '\'': if (echo_style) sb_adds(&b, "\\'"); else sb_addc(&b, '\''); break;
        case '"': if (echo_style) sb_adds(&b, "\\\""); else sb_addc(&b, '"'); break;
        case '?': if (echo_style) sb_adds(&b, "\\?"); else sb_addc(&b, '?'); break;
        case 'c':
            if (echo_style) {
                if (stop) *stop = 1;
                ansi_decode_len = b.len;
                return sb_take(&b);
            }
            if (s < e) {
                sb_addc(&b, toupper((unsigned char)*s) ^ 0x40);
                s++;
            }
            break;
        case 'x': {
            int v = 0, k = 0;
            while (k < 2 && s < e && isxdigit((unsigned char)*s)) {
                v = v * 16 + (isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10));
                s++;
                k++;
            }
            if (k == 0) sb_adds(&b, "\\x");
            else sb_addc(&b, v);
            break;
        }
        case 'u': case 'U': {
            unsigned long v = 0;
            int k = 0, max = c == 'u' ? 4 : 8;
            while (k < max && s < e && isxdigit((unsigned char)*s)) {
                v = v * 16 + (isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10));
                s++;
                k++;
            }
            if (k == 0) {
                sb_addc(&b, '\\');
                sb_addc(&b, c);
            } else if (v < 0x80) {
                sb_addc(&b, (int)v);
            } else {
#ifdef AMIGA
                /* Amiga uses ISO-8859-1 */
                sb_addc(&b, v < 256 ? (int)v : '?');
#else
                if (v < 0x800) {
                    sb_addc(&b, 0xC0 | (int)(v >> 6));
                    sb_addc(&b, 0x80 | (int)(v & 0x3F));
                } else if (v < 0x10000) {
                    sb_addc(&b, 0xE0 | (int)(v >> 12));
                    sb_addc(&b, 0x80 | (int)((v >> 6) & 0x3F));
                    sb_addc(&b, 0x80 | (int)(v & 0x3F));
                } else {
                    sb_addc(&b, 0xF0 | (int)(v >> 18));
                    sb_addc(&b, 0x80 | (int)((v >> 12) & 0x3F));
                    sb_addc(&b, 0x80 | (int)((v >> 6) & 0x3F));
                    sb_addc(&b, 0x80 | (int)(v & 0x3F));
                }
#endif
            }
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            int v = c - '0', k = 1, max = 3;
            if (echo_style && c == '0') {
                v = 0;
                k = 0;
                max = 3;
            }
            while (k < max && s < e && *s >= '0' && *s <= '7') {
                v = v * 8 + (*s - '0');
                s++;
                k++;
            }
            sb_addc(&b, v & 0xFF);
            break;
        }
        default:
            sb_addc(&b, '\\');
            sb_addc(&b, c);
        }
    }
    ansi_decode_len = b.len;
    r = sb_take(&b);
    return r;
}

static char *attr_string(Var *v)
{
    char buf[16];
    var_attr_string(v, buf);
    return xstrdup(buf);
}

/* emit collected values into ctx */
static void emit_values(XCtx *c, PVal *pv, int in_dq)
{
    int i;
    int fl = in_dq ? Q_QUOTED : Q_EXP;
    if (!pv->multi) {
        if (pv->v.n) putstr(c, pv->v.v[0], fl);
        return;
    }
    c->multi++;
    if (pv->star && (in_dq || (c->flags & X_NOSPLIT))) {
        const char *ifs = get_ifs();
        for (i = 0; i < pv->v.n; i++) {
            if (i && *ifs) put(c, (unsigned char)*ifs, fl);
            putstr(c, pv->v.v[i], fl);
        }
        if (in_dq && pv->v.n == 0) put(c, 0, Q_MARK);
        return;
    }
    for (i = 0; i < pv->v.n; i++) {
        if (i) put(c, 0, Q_SEP);
        if (in_dq) put(c, 0, Q_MARK);
        putstr(c, pv->v.v[i], fl);
    }
}

/* expand the word of ${x-word} / ${x+word}: inside "..." nested double
 * quotes still quote; unquoted, its literal text is subject to splitting */
static void xword_op(XCtx *c, const char *w, const char *bend, int in_dq)
{
    if (in_dq) {
        xword(c, w, bend, 3);
    } else {
        int save = c->litfl;
        c->litfl = Q_EXP;
        xword(c, w, bend, 0);
        c->litfl = save;
    }
}

/* process ${ body } ; body..bend excludes braces */
static int xbrace(XCtx *c, const char *body, const char *bend, int in_dq)
{
    const char *p = body;
    int nlen;
    const char *name, *sub = NULL, *sube = NULL;
    char *subs = NULL;
    PVal pv;
    int i;
    int length_op = 0, indirect = 0;

    if (p >= bend) {
        bad_subst("${}", 3);
        return -1;
    }
    /* ${#...} length, unless it's ${#} or ${#op...} */
    if (*p == '#' && p + 1 < bend) {
        int l = param_name_len(p + 1, bend);
        if (l > 0) {
            const char *q = p + 1 + l;
            if (q == bend || *q == '[') {
                length_op = 1;
                p++;
            }
        }
    }
    if (!length_op && *p == '!' && p + 1 < bend) {
        int l = param_name_len(p + 1, bend);
        if (l > 0) {
            const char *q = p + 1 + l;
            if (q < bend && (*q == '*' || *q == '@') && q + 1 == bend) {
                /* ${!prefix*} : variable names */
                char *pre = xstrndup(p + 1, l);
                StrList names;
                sl_init(&names);
                var_names(&names, pre);
                pv_init(&pv);
                for (i = 0; i < names.n; i++) sl_adddup(&pv.v, names.v[i]);
                pv.multi = 1;
                pv.star = *q == '*';
                emit_values(c, &pv, in_dq);
                sl_free(&pv.v);
                sl_free(&names);
                xfree(pre);
                return 0;
            }
            if (q + 3 <= bend && q[0] == '[' && (q[1] == '@' || q[1] == '*') && q[2] == ']' && q + 3 == bend) {
                /* ${!arr[@]} : keys */
                char *nm = xstrndup(p + 1, l);
                Var *v = var_lookup(nm);
                pv_init(&pv);
                pv.multi = 1;
                pv.star = q[1] == '*';
                if (v && (v->flags & (V_ARRAY | V_ASSOC))) var_arr_values(v, &pv.v, 1);
                else if (v && var_get(nm)) sl_adddup(&pv.v, "0");
                emit_values(c, &pv, in_dq);
                sl_free(&pv.v);
                xfree(nm);
                return 0;
            }
            indirect = 1;
            p++;
        }
    }
    name = p;
    nlen = param_name_len(p, bend);
    if (nlen == 0) {
        bad_subst(body - 2, (int)(bend - body) + 3);
        return -1;
    }
    p += nlen;
    if (p < bend && *p == '[' && (isalpha((unsigned char)*name) || *name == '_')) {
        sube = sub_end(p + 1, bend);
        if (!sube) {
            bad_subst(body - 2, (int)(bend - body) + 3);
            return -1;
        }
        sub = p + 1;
        subs = xstrndup(sub, sube - sub);
        p = sube + 1;
    }
    if (indirect) {
        /* ${!name}: value of name is the parameter to use */
        PVal ip;
        get_values(name, nlen, subs, &ip);
        xfree(subs);
        subs = NULL;
        if (!ip.set || ip.multi) {
            sl_free(&ip.v);
            if (!ip.set) {
                sh_error("%.*s: invalid indirect expansion", nlen, name);
                expand_error = 1;
                return -1;
            }
            get_values("", 0, NULL, &pv);
        } else {
            char *target = xstrdup(ip.v.v[0]);
            char *br = strchr(target, '[');
            int tl = param_name_len(target, target + strlen(target));
            sl_free(&ip.v);
            if (tl == 0 || (target[tl] && !(target[tl] == '[' && br &&
                                             target[strlen(target) - 1] == ']'))) {
                sh_error("%s: invalid variable name", target);
                xfree(target);
                expand_error = 1;
                return -1;
            }
            if (br && target[strlen(target) - 1] == ']') {
                target[strlen(target) - 1] = 0;
                *br = 0;
                get_values(target, (int)strlen(target), br + 1, &pv);
            } else {
                get_values(target, (int)strlen(target), NULL, &pv);
            }
            xfree(target);
        }
    } else {
        get_values(name, nlen, subs, &pv);
    }
    if (expand_error) {
        sl_free(&pv.v);
        xfree(subs);
        return -1;
    }
    if (length_op) {
        PVal r;
        int unbound;
        if (p != bend) {
            sl_free(&pv.v);
            xfree(subs);
            bad_subst(body - 2, (int)(bend - body) + 3);
            return -1;
        }
        pv_init(&r);
        if (pv.multi) sl_add(&r.v, str_itoa(pv.v.n));
        else sl_add(&r.v, str_itoa(pv.v.n ? (ShInt)strlen(pv.v.v[0]) : 0));
        unbound = !pv.set && !pv.multi;
        if (pv.multi && subs && sh.opt_nounset) {
            /* ${#arr[@]} of an unset (only declared) array */
            char nm[256];
            Var *av;
            int l = nlen < 255 ? nlen : 255;
            memcpy(nm, name, l);
            nm[l] = 0;
            av = var_lookup(nm);
            unbound = !av || (av->flags & V_UNSET);
        }
        if (unbound && sh.opt_nounset) {
            sh_error("%s%.*s: unbound variable", isdigit((unsigned char)*name) ? "$" : "", nlen, name);
            /* bash: fatal for scalars, only aborts the command for arrays */
            expand_error = pv.multi ? 1 : 2;
            expansion_failed(expand_error);
        }
        emit_values(c, &r, in_dq);
        sl_free(&r.v);
        sl_free(&pv.v);
        xfree(subs);
        return expand_error ? -1 : 0;
    }

    if (p == bend) {
        /* plain ${name} */
        if (!pv.set && sh.opt_nounset && !pv.multi) {
            sh_error("%s%.*s%s%s%s: unbound variable", isdigit((unsigned char)*name) ? "$" : "",
                     nlen, name, subs ? "[" : "", subs ? subs : "", subs ? "]" : "");
            expand_error = 2;   /* fatal in scripts */
            expansion_failed(2);
        } else {
            emit_values(c, &pv, in_dq);
        }
        sl_free(&pv.v);
        xfree(subs);
        return expand_error ? -1 : 0;
    }

    /* operators */
    {
        int colon = 0;
        int op = *p;
        const char *w;
        int isnull;
        if (op == ':' && p + 1 < bend && strchr("-=+?", p[1])) {
            colon = 1;
            p++;
            op = *p;
        }
        w = p + 1;
        if (pv.multi) isnull = pv.v.n == 0 || (colon && pv.v.n == 1 && !*pv.v.v[0]);
        else isnull = !pv.set || (colon && pv.v.n && !*pv.v.v[0]);
        if (pv.multi && colon && pv.v.n == 0) isnull = 1;
        if (!strchr("-=+?", op) && !pv.set && !pv.multi && sh.opt_nounset) {
            sh_error("%s%.*s%s%s%s: unbound variable", isdigit((unsigned char)*name) ? "$" : "",
                     nlen, name, subs ? "[" : "", subs ? subs : "", subs ? "]" : "");
            expand_error = 2;
            expansion_failed(2);
            sl_free(&pv.v);
            xfree(subs);
            return -1;
        }
        switch (op) {
        case '-':
            if (isnull) xword_op(c, w, bend, in_dq);
            else emit_values(c, &pv, in_dq);
            break;
        case '=':
            if (isnull) {
                char *val = expand_frag(w, bend, in_dq ? 3 : 0);
                char nm[256];
                if (pv.multi || isdigit((unsigned char)*name) || is_special_param((unsigned char)*name)) {
                    sh_error("$%.*s: cannot assign in this way", nlen, name);
                    expand_error = 1;
                    xfree(val);
                    break;
                }
                memcpy(nm, name, nlen < 255 ? nlen : 255);
                nm[nlen < 255 ? nlen : 255] = 0;
                if (subs) var_arr_set(nm, subs, val);
                else var_set(nm, val, 0);
                putstr(c, val, in_dq ? Q_QUOTED : Q_EXP);
                xfree(val);
            } else {
                emit_values(c, &pv, in_dq);
            }
            break;
        case '+':
            if (!isnull) xword_op(c, w, bend, in_dq);
            else if (in_dq && !pv.multi) put(c, 0, Q_MARK);
            else if (pv.multi) c->multi++;      /* "${a[@]+x}": no field */
            break;
        case '?':
            if (isnull) {
                char *msg = w < bend ? expand_frag(w, bend, in_dq ? 3 : 0) : xstrdup(colon ? "parameter null or not set" : "parameter not set");
                sh_error("%.*s: %s", nlen, name, msg);
                xfree(msg);
                expand_error = 2;
            } else {
                emit_values(c, &pv, in_dq);
            }
            break;
        case ':': {
            /* substring ${x:off[:len]} */
            const char *lp = NULL, *q;
            int depth = 0;
            char *offs, *lens = NULL;
            ShInt off, len = 0;
            int haslen = 0;
            for (q = w; q < bend; q++) {
                if (*q == '(') depth++;
                else if (*q == ')') depth--;
                else if (*q == '?' ) depth++;     /* ternary ?: */
                else if (*q == ':' && depth > 0) depth--;
                else if (*q == ':' && depth == 0) {
                    lp = q;
                    break;
                }
            }
            offs = xstrndup(w, (lp ? lp : bend) - w);
            if (arith_expand_eval(offs, &off)) {
                xfree(offs);
                expand_error = 1;
                break;
            }
            xfree(offs);
            if (lp) {
                lens = xstrndup(lp + 1, bend - lp - 1);
                if (arith_expand_eval(lens, &len)) {
                    xfree(lens);
                    expand_error = 1;
                    break;
                }
                xfree(lens);
                haslen = 1;
            }
            if (pv.multi) {
                PVal r;
                int n, start;
                StrList all;
                sl_init(&all);
                if (*name == '@' || *name == '*') {
                    /* positional: offset 0 includes $0 */
                    sl_adddup(&all, sh.name ? sh.name : "bash");
                    for (i = 0; i < pv.v.n; i++) sl_adddup(&all, pv.v.v[i]);
                    if (off == 0) start = 0;
                    else start = (int)off;
                } else {
                    char nm[256];
                    Var *av;
                    int l = nlen < 255 ? nlen : 255;
                    memcpy(nm, name, l);
                    nm[l] = 0;
                    av = var_lookup(nm);
                    for (i = 0; i < pv.v.n; i++) sl_adddup(&all, pv.v.v[i]);
                    start = (int)off;
                    if (av && (av->flags & V_ARRAY) && av->alen == pv.v.n && av->alen) {
                        /* sparse arrays: the offset is an index value */
                        ShInt first = off < 0 ? av->arr[av->alen - 1].idx + 1 + off : off;
                        if (first < 0) start = all.n;
                        else {
                            for (start = 0; start < av->alen && av->arr[start].idx < first; start++)
                                ;
                        }
                    }
                }
                n = all.n;
                if (start < 0) start += (*name == '@' || *name == '*') ? n : n;
                if (start < 0) start = n;
                pv_init(&r);
                r.multi = 1;
                r.star = pv.star;
                if (haslen && len < 0) {
                    sh_error("%s: substring expression < 0", lens ? "len" : "len");
                    expand_error = 1;
                    sl_free(&all);
                    break;
                }
                for (i = start; i < n && (!haslen || i < start + len); i++) sl_adddup(&r.v, all.v[i]);
                emit_values(c, &r, in_dq);
                sl_free(&r.v);
                sl_free(&all);
            } else {
                const char *val = pv.v.n ? pv.v.v[0] : "";
                ShInt n = (ShInt)strlen(val);
                ShInt st = off < 0 ? n + off : off;
                ShInt ln;
                if (st < 0 || st > n) {
                    if (in_dq) put(c, 0, Q_MARK);
                    break;
                }
                ln = n - st;
                if (haslen) {
                    if (len < 0) {
                        ln = n + len - st;
                        if (ln < 0) {
                            sh_error("%s: substring expression < 0", "len");
                            expand_error = 1;
                            break;
                        }
                    } else if (len < ln) {
                        ln = len;
                    }
                }
                putn(c, val + st, (int)ln, in_dq ? Q_QUOTED : Q_EXP);
                if (in_dq) put(c, 0, Q_MARK);
            }
            break;
        }
        case '#':
        case '%': {
            int longest = p + 1 < bend && p[1] == op;
            char *pat = expand_pattern_n(w + longest, bend, 0);
            PVal r;
            int nocase = sh.sh_nocasematch ? PM_NOCASE : 0;
            pv_init(&r);
            r.multi = pv.multi;
            r.star = pv.star;
            for (i = 0; i < pv.v.n; i++) {
                const char *val = pv.v.v[i];
                int k = op == '#' ? pat_prefix(pat, val, longest, nocase) : pat_suffix(pat, val, longest, nocase);
                if (k < 0) sl_adddup(&r.v, val);
                else if (op == '#') sl_adddup(&r.v, val + k);
                else sl_add(&r.v, xstrndup(val, strlen(val) - k));
            }
            xfree(pat);
            emit_values(c, &r, in_dq);
            sl_free(&r.v);
            break;
        }
        case '/': {
            int mode = 0;
            const char *q, *pe;
            char *pat, *rep;
            PVal r;
            if (w < bend && *w == '/') mode = 1, w++;
            else if (w < bend && *w == '#') mode = 2, w++;
            else if (w < bend && *w == '%') mode = 3, w++;
            /* find unquoted '/' separating pattern and replacement */
            pe = bend;
            for (q = w; q < bend; q++) {
                if (*q == '\\' && q + 1 < bend) {
                    q++;
                    continue;
                }
                if (*q == '\'' ) {
                    const char *e = scan_skip_squote(q + 1);
                    if (e) q = e - 1;
                    continue;
                }
                if (*q == '"') {
                    const char *e = scan_skip_dquote(q + 1);
                    if (e) q = e - 1;
                    continue;
                }
                if (*q == '$') {
                    const char *e = scan_skip_dollar(q);
                    if (e && e > q + 1) q = e - 1;
                    continue;
                }
                if (*q == '/') {
                    pe = q;
                    break;
                }
            }
            pat = expand_pattern_n(w, pe, 0);
            rep = pe < bend ? expand_replacement(pe + 1, bend) : xstrdup("");
            pv_init(&r);
            r.multi = pv.multi;
            r.star = pv.star;
            for (i = 0; i < pv.v.n; i++) sl_add(&r.v, pat_replace(pv.v.v[i], pat, rep, mode));
            xfree(pat);
            xfree(rep);
            emit_values(c, &r, in_dq);
            sl_free(&r.v);
            break;
        }
        case '^':
        case '~':
        case ',': {
            int all = p + 1 < bend && p[1] == op;
            char *pat = expand_pattern_n(w + all, bend, 0);
            PVal r;
            pv_init(&r);
            r.multi = pv.multi;
            r.star = pv.star;
            for (i = 0; i < pv.v.n; i++) sl_add(&r.v, case_mod(pv.v.v[i], pat, op == '^' ? 1 : op == '~' ? 2 : 0, all));
            xfree(pat);
            emit_values(c, &r, in_dq);
            sl_free(&r.v);
            break;
        }
        case '@': {
            PVal r;
            int t = w < bend ? *w : 0;
            pv_init(&r);
            r.multi = pv.multi;
            r.star = pv.star;
            if (w + 1 != bend) {
                bad_subst(body - 2, (int)(bend - body) + 3);
                break;
            }
            for (i = 0; i < pv.v.n; i++) {
                const char *val = pv.v.v[i];
                switch (t) {
                case 'Q': sl_add(&r.v, quote_q(val)); break;
                case 'E': sl_add(&r.v, ansi_decode(val, (int)strlen(val), 0, NULL)); break;
                case 'P': sl_add(&r.v, prompt_expand(val)); break;
                case 'U': sl_add(&r.v, case_mod(val, NULL, 1, 1)); break;
                case 'L': sl_add(&r.v, case_mod(val, NULL, 0, 1)); break;
                case 'u': sl_add(&r.v, case_mod(val, NULL, 1, 0)); break;
                case 'a': {
                    char nm[256];
                    int l = nlen < 255 ? nlen : 255;
                    memcpy(nm, name, l);
                    nm[l] = 0;
                    sl_add(&r.v, attr_string(var_lookup(nm)));
                    break;
                }
                case 'A': {
                    char *q = quote_q(val);
                    sl_add(&r.v, str_printf("%.*s=%s", nlen, name, q));
                    xfree(q);
                    break;
                }
                default:
                    sl_adddup(&r.v, val);
                    bad_subst(body - 2, (int)(bend - body) + 3);
                }
            }
            if (!expand_error) emit_values(c, &r, in_dq);
            sl_free(&r.v);
            break;
        }
        default:
            bad_subst(body - 2, (int)(bend - body) + 3);
        }
    }
    sl_free(&pv.v);
    xfree(subs);
    return expand_error ? -1 : 0;
}

/* --------------------------------------------------------------- */
/* substitutions                                                     */

/* strip backslashes in backtick command text */
static char *backtick_text(const char *s, const char *e, int in_dq)
{
    SBuf b;
    sb_init(&b);
    sb_addn(&b, "", 0);
    while (s < e) {
        if (*s == '\\' && s + 1 < e && (s[1] == '$' || s[1] == '`' || s[1] == '\\' || (in_dq && s[1] == '"'))) {
            sb_addc(&b, s[1]);
            s += 2;
        } else {
            sb_addc(&b, *s++);
        }
    }
    return sb_take(&b);
}

/* expand arithmetic text: params, cmdsubs; quotes removed */
static char *arith_text(const char *s, const char *e)
{
    const char *p;
    /* fast path: nothing to expand */
    for (p = s; p < e; p++)
        if (*p == '$' || *p == '`' || *p == '"' || *p == '\'' || *p == '\\') break;
    if (p == e) return xstrndup(s, e - s);
    {
    XCtx c;
    SBuf out;
    int i;
    xinit(&c, X_NOSPLIT | X_NOGLOB | X_HEREDOC);
    xword(&c, s, e, 2);
    sb_init(&out);
    sb_addn(&out, "", 0);
    for (i = 0; i < c.s.len; i++) {
        int fl = (unsigned char)c.q.s[i];
        if (fl & (Q_MARK | Q_SEP)) continue;
        if (c.s.s[i] == '"' && !(fl & Q_EXP)) continue;
        sb_addc(&out, c.s.s[i]);
    }
    xfree_ctx(&c);
    return sb_take(&out);
    }
}

int arith_expand_eval(const char *expr, ShInt *result)
{
    char *t;
    int r;
    expand_error = 0;
    t = arith_text(expr, expr + strlen(expr));
    if (expand_error) {
        xfree(t);
        return 1;
    }
    r = arith_eval(t, result);
    xfree(t);
    return r;
}

/* process substitution temp files, removed after the command */
static StrList procsub_files;

int procsub_mark(void)
{
    return procsub_files.n;
}

/* remove the files created since mark (by the command that used them) */
void procsub_cleanup_to(int mark)
{
    while (procsub_files.n > mark) {
        char *name = procsub_files.v[--procsub_files.n];
        char *np = path_native(name);
        os_remove(np);
        xfree(np);
        xfree(name);
    }
    if (procsub_files.n == 0) sl_free(&procsub_files);
}

void procsub_cleanup(void)
{
    procsub_cleanup_to(0);
}

static char *process_subst(const char *cmd, int out)
{
    static long count;
    char *name;
    ShFile *f;
    FdSave save;
    if (out) {
        sh_error(">(...) process substitution is not supported on this system");
        expand_error = 1;
        return NULL;
    }
    name = str_printf("%sbashps.%ld.%ld", os_tmpdir(), os_pid(), ++count);
    f = shf_open(name, OS_WR);
    if (!f) {
        sh_error("%s: %s", name, os_errstr());
        xfree(name);
        expand_error = 1;
        return NULL;
    }
    fd_save(&save);
    fd_assign(1, f);
    {
        int mark = procsub_mark();
        exec_subshell_string(cmd);
        procsub_cleanup_to(mark);
    }
    sh_flush_all();
    fd_restore(&save);
    sl_adddup(&procsub_files, name);
    return name;
}

/* --------------------------------------------------------------- */
/* the word scanner                                                  */

static int xword_inner(XCtx *c, const char *s, const char *end, int in_dq);

/* in_dq: 0 = unquoted, 1 = inside "...", 2 = heredoc/arith body,
 * 3 = word of ${x:-word} inside "..." (nested "..." are quotes) */
static int xword(XCtx *c, const char *s, const char *end, int in_dq)
{
    const char *ss = xw_s, *se = xw_e;
    int r;
    xw_s = s;
    xw_e = end;
    r = xword_inner(c, s, end, in_dq);
    xw_s = ss;
    xw_e = se;
    return r;
}

static int xword_inner(XCtx *c, const char *s, const char *end, int in_dq)
{
    const char *wstart = s;
    int tilde_ok = !in_dq;
    int seen_eq = 0;
    int qfl = in_dq ? Q_QUOTED : c->litfl;
    while (s < end && !expand_error) {
        int ch = (unsigned char)*s;
        if (tilde_ok && ch == '~' && !(c->flags & (X_PATTERN | X_REGEX)) &&
            (s == wstart || (c->flags & X_ASSIGN))) {
            /* tilde prefix */
            const char *e = s + 1;
            char *pre, *val = NULL;
            while (e < end && *e != '/' && !((c->flags & X_ASSIGN) && *e == ':')) {
                if (strchr("'\"\\$`", *e)) break;
                e++;
            }
            if (e == end || *e == '/' || *e == ':') {
                pre = xstrndup(s + 1, e - s - 1);
                if (!*pre) {
                    const char *h = var_get("HOME");
                    val = xstrdup(h ? h : os_default_home());
                } else if (strcmp(pre, "+") == 0) {
                    const char *v = var_get("PWD");
                    if (v) val = xstrdup(v);
                } else if (strcmp(pre, "-") == 0) {
                    const char *v = var_get("OLDPWD");
                    if (v) val = xstrdup(v);
                } else if (str_isnum(pre) || ((pre[0] == '+' || pre[0] == '-') && str_isnum(pre + 1))) {
                    int k = atoi(pre[0] == '+' || pre[0] == '-' ? pre + 1 : pre);
                    int n = sh.dirstack.n;
                    if (pre[0] == '-') k = n - k;
                    if (k == 0) val = xstrdup(sh.pwd ? sh.pwd : ".");
                    else if (k > 0 && k <= n) val = xstrdup(sh.dirstack.v[n - k]);
                } else {
                    /* ~user: on the Amiga there is only one user */
                    if (strcmp(pre, os_username()) == 0) {
                        const char *h = var_get("HOME");
                        val = xstrdup(h ? h : os_default_home());
                    }
                }
                xfree(pre);
                if (val) {
                    /* HOME may end in ':' (e.g. SYS:) - avoid "SYS:/x" */
                    int vl = (int)strlen(val);
                    putstr(c, val, Q_QUOTED);
                    if (vl && val[vl - 1] == ':' && e < end && *e == '/') e++;
                    else if (vl && val[vl - 1] == '/' && e < end && *e == '/') e++;
                    xfree(val);
                    s = e;
                    tilde_ok = 0;
                    continue;
                }
            }
        }
        tilde_ok = 0;
        switch (ch) {
        case '\\':
            if (s + 1 >= end) {
                put(c, '\\', qfl);
                s++;
                break;
            }
            if (in_dq) {
                int n = (unsigned char)s[1];
                if (n == '$' || n == '`' || n == '\\' || n == '\n' || (n == '"' && (in_dq == 1 || in_dq == 3))) {
                    if (n != '\n') put(c, n, Q_QUOTED);
                    s += 2;
                } else {
                    put(c, '\\', Q_QUOTED);
                    s++;
                }
            } else {
                if (s[1] != '\n') put(c, (unsigned char)s[1], Q_QUOTED);
                s += 2;
            }
            break;
        case '\'':
            if (in_dq) {
                put(c, ch, Q_QUOTED);
                s++;
            } else {
                const char *e = scan_skip_squote(s + 1);
                if (!e) e = end + 1;
                put(c, 0, Q_MARK);
                putn(c, s + 1, (int)(e - 1 - (s + 1)), Q_QUOTED);
                s = e;
            }
            break;
        case '"':
            if (in_dq && in_dq != 3) {
                put(c, ch, Q_QUOTED);
                s++;
            } else {
                const char *e = scan_skip_dquote(s + 1);
                int before = c->s.len, mbefore = c->multi;
                if (!e) e = end + 1;
                xword(c, s + 1, e - 1, 1);
                if (c->s.len == before && c->multi == mbefore) put(c, 0, Q_MARK);
                s = e;
            }
            break;
        case '`': {
            const char *e = scan_skip_backtick(s + 1);
            char *cmd, *out;
            if (!e) e = end + 1;
            cmd = backtick_text(s + 1, e - 1, in_dq);
            out = command_subst(cmd);
            xfree(cmd);
            putstr(c, out, in_dq ? Q_QUOTED : Q_EXP);
            xfree(out);
            s = e;
            break;
        }
        case '<':
        case '>':
            if (!in_dq && s == wstart && s + 1 < end && s[1] == '(') {
                const char *e = scan_skip_parens(s + 2);
                char *cmd, *name;
                if (!e) e = end;
                cmd = xstrndup(s + 2, (e - 1) - (s + 2));
                name = process_subst(cmd, ch == '>');
                xfree(cmd);
                if (name) {
                    putstr(c, name, Q_QUOTED);
                    xfree(name);
                }
                s = e;
                break;
            }
            put(c, ch, qfl);
            s++;
            break;
        case '$': {
            const char *n = s + 1;
            if (n >= end) {
                put(c, '$', qfl);
                s++;
                break;
            }
            if (*n == '(') {
                if (n + 1 < end && n[1] == '(') {
                    const char *e = scan_skip_dollar(s);
                    /* arithmetic if it ends with "))" and parses as such */
                    if (e && e[-1] == ')' && e[-2] == ')') {
                        ShInt v;
                        char *t = arith_text(s + 3, e - 2);
                        int isarith = 1;
                        /* distinguish $((cmd) ) from $(( expr )) */
                        {
                            int na;
                            const char *q = s + 3;
                            int depth = 0;
                            na = 0;
                            for (; q < e - 2; q++) {
                                if (*q == '(') depth++;
                                else if (*q == ')') {
                                    if (depth == 0) { na = 1; break; }
                                    depth--;
                                }
                            }
                            if (na) isarith = 0;
                        }
                        if (isarith) {
                            if (!expand_error && arith_eval(t, &v) == 0) {
                                char *num = str_itoa(v);
                                putstr(c, num, in_dq ? Q_QUOTED : Q_EXP);
                                xfree(num);
                            } else {
                                expand_error = 1;
                            }
                            xfree(t);
                            s = e;
                            break;
                        }
                        xfree(t);
                    }
                }
                {
                    const char *e = scan_cmdsub_end(n + 1);
                    char *cmd, *out;
                    if (!e) e = end - 1;
                    cmd = xstrndup(n + 1, e - (n + 1));
                    out = command_subst(cmd);
                    xfree(cmd);
                    putstr(c, out, in_dq ? Q_QUOTED : Q_EXP);
                    xfree(out);
                    s = e + 1;
                }
                break;
            }
            if (*n == '{') {
                const char *e = scan_skip_brace(n + 1);
                if (!e || e > end) {
                    bad_subst(s, (int)(end - s));
                    return -1;
                }
                xbrace(c, n + 1, e - 1, in_dq);
                s = e;
                break;
            }
            if (*n == '\'' && !in_dq) {
                const char *e = scan_skip_ansi(n + 1);
                char *dec;
                if (!e) e = end + 1;
                dec = ansi_decode(n + 1, (int)(e - 1 - (n + 1)), 0, NULL);
                put(c, 0, Q_MARK);
                putstr(c, dec, Q_QUOTED);
                xfree(dec);
                s = e;
                break;
            }
            if (*n == '[') {
                /* obsolete $[ expr ] arithmetic */
                const char *e = n + 1;
                int depth = 1;
                while (e < end && depth) {
                    if (*e == '[') depth++;
                    else if (*e == ']') depth--;
                    if (depth) e++;
                }
                if (e < end) {
                    ShInt v;
                    char *t = arith_text(n + 1, e);
                    if (!expand_error && arith_eval(t, &v) == 0) {
                        char *num = str_itoa(v);
                        putstr(c, num, in_dq ? Q_QUOTED : Q_EXP);
                        xfree(num);
                    } else {
                        expand_error = 1;
                    }
                    xfree(t);
                    s = e + 1;
                    break;
                }
            }
            if (*n == '"' && !in_dq) {
                s++;
                break;           /* $"..." = "..." */
            }
            {
                int l = 0;
                if (isalpha((unsigned char)*n) || *n == '_') {
                    while (n + l < end && (isalnum((unsigned char)n[l]) || n[l] == '_')) l++;
                } else if (isdigit((unsigned char)*n) || is_special_param((unsigned char)*n)) {
                    l = 1;
                }
                if (l == 0) {
                    put(c, '$', qfl);
                    s++;
                    break;
                }
                {
                    PVal pv;
                    get_values(n, l, NULL, &pv);
                    if (!pv.set && sh.opt_nounset && !(*n == '@' || *n == '*')) {
                        sh_error("%s%.*s: unbound variable", isdigit((unsigned char)*n) ? "$" : "", l, n);
                        expand_error = 2;   /* fatal in scripts */
                        expansion_failed(2);
                    } else {
                        emit_values(c, &pv, in_dq);
                    }
                    sl_free(&pv.v);
                }
                s = n + l;
            }
            break;
        }
        default:
            if ((c->flags & X_ASSIGN) && !in_dq) {
                if (ch == ':' || (ch == '=' && !seen_eq)) tilde_ok = 1;
                if (ch == '=') seen_eq = 1;
            }
            put(c, ch, qfl);
            s++;
        }
    }
    return expand_error ? -1 : 0;
}

/* --------------------------------------------------------------- */
/* splitting, globbing                                               */

typedef struct Field { SBuf s, q; int has; } Field;

static void field_push(StrList *fs, StrList *fq, Field *f)
{
    sl_add(fs, xstrndup(f->s.s ? f->s.s : "", f->s.len));
    sl_add(fq, xstrndup(f->q.s ? f->q.s : "", f->q.len));
    /* store length implicitly: strings may contain NUL? no */
    sb_clear(&f->s);
    sb_clear(&f->q);
    f->has = 0;
}

static void field_add(Field *f, int ch, int fl)
{
    sb_addc(&f->s, ch);
    sb_addc(&f->q, fl ? fl : 0x10);  /* never store NUL flag bytes */
    f->has = 1;
}

static void split_fields(XCtx *c, StrList *fs, StrList *fq)
{
    const char *ifs = get_ifs();
    Field f;
    int i, ws_after = 0;
    sb_init(&f.s);
    sb_init(&f.q);
    f.has = 0;
    for (i = 0; i < c->s.len; i++) {
        int ch = (unsigned char)c->s.s[i];
        int fl = (unsigned char)c->q.s[i];
        if (fl & Q_SEP) {
            if (f.has) field_push(fs, fq, &f);
            ws_after = 0;
            continue;
        }
        if (fl & Q_MARK) {
            f.has = 1;
            if (!f.s.s) sb_addn(&f.s, "", 0), sb_addn(&f.q, "", 0);
            continue;
        }
        if ((fl & Q_EXP) && ch && strchr(ifs, ch)) {
            int isws = ch == ' ' || ch == '\t' || ch == '\n';
            if (isws) {
                if (f.has) {
                    field_push(fs, fq, &f);
                    ws_after = 1;
                }
            } else {
                if (f.has) field_push(fs, fq, &f);
                else if (!ws_after) {
                    f.has = 1;
                    field_push(fs, fq, &f);
                }
                ws_after = 0;
            }
            continue;
        }
        field_add(&f, ch, fl);
        ws_after = 0;
    }
    if (f.has) field_push(fs, fq, &f);
    sb_free(&f.s);
    sb_free(&f.q);
}

static void join_field(XCtx *c, StrList *fs, StrList *fq)
{
    Field f;
    int i;
    sb_init(&f.s);
    sb_init(&f.q);
    sb_addn(&f.s, "", 0);
    sb_addn(&f.q, "", 0);
    for (i = 0; i < c->s.len; i++) {
        int fl = (unsigned char)c->q.s[i];
        if (fl & Q_MARK) continue;
        if (fl & Q_SEP) {
            field_add(&f, ' ', Q_QUOTED);
            continue;
        }
        field_add(&f, (unsigned char)c->s.s[i], fl);
    }
    field_push(fs, fq, &f);
    sb_free(&f.s);
    sb_free(&f.q);
}

static int field_has_glob(const char *s, const char *q)
{
    int i;
    for (i = 0; s[i]; i++) {
        if (q[i] & Q_QUOTED) continue;
        if (s[i] == '*' || s[i] == '?') return 1;
        if (s[i] == '[') return 1;
        if (sh.sh_extglob && s[i + 1] == '(' && strchr("@+!", s[i])) return 1;
    }
    return 0;
}

static char *field_pattern(const char *s, const char *q)
{
    SBuf b;
    int i;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (i = 0; s[i]; i++) {
        if ((q[i] & Q_QUOTED) && strchr("*?[]\\(|)!@+", s[i])) sb_addc(&b, '\\');
        else if (!(q[i] & Q_QUOTED) && s[i] == '\\') sb_addc(&b, '\\');
        sb_addc(&b, s[i]);
    }
    return sb_take(&b);
}

static int finish(XCtx *c, StrList *out)
{
    StrList fs, fq;
    int i;
    sl_init(&fs);
    sl_init(&fq);
    if (c->flags & X_NOSPLIT) join_field(c, &fs, &fq);
    else split_fields(c, &fs, &fq);
    for (i = 0; i < fs.n; i++) {
        char *s = fs.v[i], *q = fq.v[i];
        if (c->flags & (X_PATTERN | X_REGEX)) {
            SBuf b;
            int k;
            const char *special = (c->flags & X_REGEX) ? ".[]()*+?{}|^$\\" : "*?[]\\(|)!@+";
            sb_init(&b);
            sb_addn(&b, "", 0);
            for (k = 0; s[k]; k++) {
                if ((q[k] & Q_QUOTED) && strchr(special, s[k])) sb_addc(&b, '\\');
                sb_addc(&b, s[k]);
            }
            sl_add(out, sb_take(&b));
            continue;
        }
        if (!(c->flags & X_NOGLOB) && !sh.opt_noglob && field_has_glob(s, q)) {
            char *pat = field_pattern(s, q);
            int n = glob_expand(pat, out, 0);
            xfree(pat);
            if (n > 0) continue;
            if (sh.sh_failglob) {
                sh_error("no match: %s", s);
                expand_error = 1;
                continue;
            }
            if (sh.sh_nullglob) continue;
        }
        sl_adddup(out, s);
    }
    sl_free(&fs);
    sl_free(&fq);
    return expand_error;
}

/* --------------------------------------------------------------- */
/* brace expansion                                                   */

/* find a valid brace group in s; returns 1 and sets *b (at '{') and
 * *e (at '}') */
static int find_brace(const char *s, const char **b, const char **e)
{
    const char *p = s;
    while (*p) {
        if (*p == '\\') {
            if (p[1]) p++;
            p++;
            continue;
        }
        if (*p == '\'') {
            const char *x = scan_skip_squote(p + 1);
            if (!x) return 0;
            p = x;
            continue;
        }
        if (*p == '"') {
            const char *x = scan_skip_dquote(p + 1);
            if (!x) return 0;
            p = x;
            continue;
        }
        if (*p == '$' && (p[1] == '{' || p[1] == '(')) {
            const char *x = scan_skip_dollar(p);
            if (!x) return 0;
            p = x;
            continue;
        }
        if (*p == '`') {
            const char *x = scan_skip_backtick(p + 1);
            if (!x) return 0;
            p = x;
            continue;
        }
        if (*p == '{') {
            /* look for matching } with a top-level comma or .. */
            const char *q = p + 1;
            int depth = 0, comma = 0;
            while (*q) {
                if (*q == '\\') {
                    if (q[1]) q++;
                    q++;
                    continue;
                }
                if (*q == '\'' || *q == '"') {
                    const char *x = *q == '\'' ? scan_skip_squote(q + 1) : scan_skip_dquote(q + 1);
                    if (!x) break;
                    q = x;
                    continue;
                }
                if (*q == '$' && (q[1] == '{' || q[1] == '(')) {
                    const char *x = scan_skip_dollar(q);
                    if (!x) break;
                    q = x;
                    continue;
                }
                if (*q == '{') depth++;
                else if (*q == '}') {
                    if (depth == 0) break;
                    depth--;
                } else if (*q == ',' && depth == 0) {
                    comma = 1;
                }
                q++;
            }
            if (*q == '}') {
                if (comma) {
                    *b = p;
                    *e = q;
                    return 1;
                }
                /* sequence {a..b[..c]} */
                {
                    const char *dd = strstr(p + 1, "..");
                    if (dd && dd < q) {
                        *b = p;
                        *e = q;
                        return 2;
                    }
                }
            }
        }
        p++;
    }
    return 0;
}

static int seq_parse(const char *s, int n, StrList *out)
{
    char buf[128];
    char *a, *b2, *c3;
    ShInt x, y, inc = 1;
    int width = 0;
    if (n >= (int)sizeof(buf)) return 0;
    memcpy(buf, s, n);
    buf[n] = 0;
    a = buf;
    b2 = strstr(a, "..");
    if (!b2) return 0;
    *b2 = 0;
    b2 += 2;
    c3 = strstr(b2, "..");
    if (c3) {
        *c3 = 0;
        c3 += 2;
        if (!str_toint(c3, &inc)) return 0;
        if (inc < 0) inc = -inc;
        if (inc == 0) inc = 1;
    }
    if (str_toint(a, &x) && str_toint(b2, &y)) {
        ShInt v;
        if ((a[0] == '0' && a[1]) || (a[0] == '-' && a[1] == '0' && a[2]))
            width = (int)strlen(a);
        if ((b2[0] == '0' && b2[1]) || (b2[0] == '-' && b2[1] == '0' && b2[2]))
            if ((int)strlen(b2) > width) width = (int)strlen(b2);
        for (v = x; x <= y ? v <= y : v >= y; v += x <= y ? inc : -inc) {
            char *num = str_itoa(v);
            if (width && (int)strlen(num) < width) {
                SBuf sb;
                int neg = num[0] == '-';
                int k = width - (int)strlen(num);
                sb_init(&sb);
                if (neg) sb_addc(&sb, '-');
                while (k-- > 0) sb_addc(&sb, '0');
                sb_adds(&sb, num + neg);
                xfree(num);
                num = sb_take(&sb);
            }
            sl_add(out, num);
        }
        return 1;
    }
    if (strlen(a) == 1 && strlen(b2) == 1 && isalpha((unsigned char)a[0]) && isalpha((unsigned char)b2[0])) {
        int ca = (unsigned char)a[0], cb = (unsigned char)b2[0], v;
        for (v = ca; ca <= cb ? v <= cb : v >= cb; v += ca <= cb ? (int)inc : -(int)inc) {
            char t[3];
            int k = 0;
            if (!isalnum(v)) t[k++] = '\\';    /* {Z..a} has [ \\ ] ^ _ ` */
            t[k++] = (char)v;
            t[k] = 0;
            sl_adddup(out, t);
        }
        return 1;
    }
    return 0;
}

int brace_expand(const char *raw, StrList *out)
{
    const char *b, *e;
    int kind = find_brace(raw, &b, &e);
    StrList alts;
    int i;
    if (!kind) {
        sl_adddup(out, raw);
        return 1;
    }
    sl_init(&alts);
    if (kind == 2) {
        if (!seq_parse(b + 1, (int)(e - b - 1), &alts)) {
            /* not a valid sequence: expand braces later in the word */
            char *pre = xstrndup(raw, e - raw + 1);
            StrList rest;
            sl_init(&rest);
            brace_expand(e + 1, &rest);
            for (i = 0; i < rest.n; i++) sl_add(out, str_cat(pre, rest.v[i]));
            sl_free(&rest);
            xfree(pre);
            sl_free(&alts);
            return 1;
        }
    } else {
        const char *p = b + 1, *a = p;
        int depth = 0;
        while (p <= e) {
            if (p < e && *p == '\\') {
                p += 2;
                continue;
            }
            if (p < e && (*p == '\'' || *p == '"')) {
                const char *x = *p == '\'' ? scan_skip_squote(p + 1) : scan_skip_dquote(p + 1);
                p = x ? x : e;
                continue;
            }
            if (p < e && *p == '$' && (p[1] == '{' || p[1] == '(')) {
                const char *x = scan_skip_dollar(p);
                p = x ? x : e;
                continue;
            }
            if (p < e && *p == '{') depth++;
            else if (p < e && *p == '}') depth--;
            else if (p == e || (*p == ',' && depth == 0)) {
                sl_add(&alts, xstrndup(a, p - a));
                a = p + 1;
            }
            p++;
        }
    }
    for (i = 0; i < alts.n; i++) {
        SBuf w;
        char *nw;
        sb_init(&w);
        sb_addn(&w, raw, (int)(b - raw));
        sb_adds(&w, alts.v[i]);
        sb_adds(&w, e + 1);
        nw = sb_take(&w);
        brace_expand(nw, out);
        xfree(nw);
    }
    sl_free(&alts);
    return 1;
}

/* --------------------------------------------------------------- */
/* public entry points                                               */

static int expand_one(const char *raw, StrList *out, int flags)
{
    XCtx c;
    int r;
    xinit(&c, flags);
    xword(&c, raw, raw + strlen(raw), (flags & X_HEREDOC) ? 2 : (flags & X_DQUOTE) ? 1 : 0);
    if (expand_error) {
        xfree_ctx(&c);
        return 1;
    }
    r = finish(&c, out);
    xfree_ctx(&c);
    return r;
}

/* a word with no quoting, expansion or glob characters expands to itself */
static int is_literal_word(const char *s)
{
    if (!*s) return 0;                   /* "" vs nothing: take slow path */
    for (; *s; s++) {
        switch (*s) {
        case '$': case '`': case '\'': case '"': case '\\': case '*': case '?':
        case '[': case '~': case '{': case '(': case '<': case '>': case '@': case '+': case '!':
            return 0;
        }
    }
    return 1;
}

int expand_word(const char *raw, StrList *out, int flags)
{
    expand_error = 0;
    if (!(flags & X_HEREDOC) && is_literal_word(raw)) {
        sl_adddup(out, raw);
        return 0;
    }
    if (!(flags & (X_NOBRACE | X_NOSPLIT | X_ASSIGN)) && sh.opt_braceexpand && strchr(raw, '{')) {
        StrList bl;
        int i, r = 0;
        sl_init(&bl);
        brace_expand(raw, &bl);
        for (i = 0; i < bl.n && !r; i++) r = expand_one(bl.v[i], out, flags);
        sl_free(&bl);
        return r;
    }
    return expand_one(raw, out, flags);
}

int expand_words(Word *w, StrList *out, int flags)
{
    for (; w; w = w->next)
        if (expand_word(w->text, out, flags)) return 1;
    return 0;
}

char *expand_str(const char *raw, int flags)
{
    if (!(flags & X_HEREDOC) && (is_literal_word(raw) || !*raw)) return xstrdup(raw);
    StrList l;
    char *r;
    int save = expand_error;
    sl_init(&l);
    expand_error = 0;
    if (expand_one(raw, &l, flags | X_NOSPLIT | X_NOGLOB) || l.n == 0) {
        sl_free(&l);
        if (expand_error) return NULL;
        expand_error = save;
        return xstrdup("");
    }
    r = l.v[0];
    l.v[0] = NULL;
    sl_free(&l);
    expand_error = save;
    return r;
}

int cmdsubst_status = -1;

/* $(< file): the command is a lone input redirection */
static int is_file_subst(const char *cmd, const char **word)
{
    const char *p = cmd, *e;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p != '<' || p[1] == '<' || p[1] == '(' || p[1] == '&') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    e = scan_word_end(p);
    if (!e || e == p) return 0;
    *word = p;
    while (*e == ' ' || *e == '\t' || *e == '\n') e++;
    return *e == 0;
}

char *command_subst(const char *cmd)
{
    ShFile *mem = shf_mem();
    FdSave save;
    char *r;
    long n;
    int st, save_errexit = sh.opt_errexit;
    const char *fw;
    fd_save(&save);
    fd_assign(1, shf_ref(mem));
    if (is_file_subst(cmd, &fw)) {
        char *w = xstrndup(fw, (int)(scan_word_end(fw) - fw));
        char *path = expand_str(w, 0);
        ShFile *f = path ? shf_open(path, OS_RD) : NULL;
        st = 1;
        if (f) {
            char buf[512];
            long k;
            while ((k = shf_read(f, buf, sizeof(buf))) > 0) shf_write(mem, buf, k);
            shf_close(f);
            st = 0;
        } else if (path) {
            sh_error("%s: %s", path, os_errstr());
        }
        xfree(path);
        xfree(w);
    } else {
        /* bash does not inherit set -e into $(...) (no inherit_errexit) */
        sh.opt_errexit = 0;
        while (*cmd == '\n' || *cmd == ' ' || *cmd == '\t') cmd++;
        st = exec_subshell_string(cmd);
        sh.opt_errexit = save_errexit;
    }
    cmdsubst_status = st;
    sh_flush_all();
    fd_restore(&save);
    n = mem->mlen;
    while (n > 0 && mem->mem[n - 1] == '\n') n--;
    {
        /* copy, dropping NUL bytes like bash does */
        long i, o = 0;
        r = xmalloc(n + 1);
        for (i = 0; i < n; i++)
            if (mem->mem[i]) r[o++] = mem->mem[i];
        r[o] = 0;
    }
    shf_close(mem);
    sh.laststatus = st;
    return r;
}

char *tilde_expand_str(const char *s)
{
    return expand_str(s, X_ASSIGN);
}

/* --------------------------------------------------------------- */
/* prompts                                                            */

char *prompt_expand(const char *ps)
{
    SBuf b;
    char *r;
    const char *s;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (s = ps; *s; s++) {
        if (*s != '\\' || !s[1]) {
            sb_addc(&b, *s);
            continue;
        }
        s++;
        switch (*s) {
        case 'u': {
            const char *u = var_get("USER");
            sb_adds(&b, u ? u : os_username());
            break;
        }
        case 'h':
        case 'H': {
            const char *h = var_get("HOSTNAME");
            const char *hn = h ? h : os_hostname();
            if (*s == 'h') {
                const char *dot = strchr(hn, '.');
                sb_addn(&b, hn, dot ? (int)(dot - hn) : (int)strlen(hn));
            } else {
                sb_adds(&b, hn);
            }
            break;
        }
        case 'w':
        case 'W': {
            const char *pwd = sh.pwd ? sh.pwd : "";
            const char *home = var_get("HOME");
            int hl = home ? (int)strlen(home) : 0;
            if (hl && strncmp(pwd, home, hl) == 0 &&
                (pwd[hl] == 0 || pwd[hl] == '/' || home[hl - 1] == ':' || home[hl - 1] == '/')) {
                if (*s == 'W' && pwd[hl] == 0) {
                    sb_addc(&b, '~');
                } else if (*s == 'W') {
                    sb_adds(&b, path_basename(pwd));
                } else {
                    sb_addc(&b, '~');
                    if (pwd[hl] && pwd[hl] != '/') sb_addc(&b, '/');
                    sb_adds(&b, pwd + hl);
                }
            } else if (*s == 'W') {
                sb_adds(&b, path_basename(pwd));
            } else {
                sb_adds(&b, pwd);
            }
            break;
        }
        case '$':
            sb_addc(&b, '$');
            break;
        case 'n':
            sb_addc(&b, '\n');
            break;
        case 'r':
            sb_addc(&b, '\r');
            break;
        case 'a':
            sb_addc(&b, 7);
            break;
        case 'e':
            sb_addc(&b, 27);
            break;
        case 's':
            sb_adds(&b, "bash");
            break;
        case 'v':
            sb_adds(&b, "5.2");
            break;
        case 'V':
            sb_adds(&b, "5.2.0");
            break;
        case '!':
            sb_printf(&b, "%d", hist_base() + hist_count());
            break;
        case '#':
            sb_printf(&b, "%d", hist_count() + 1);
            break;
        case 'j':
            sb_addc(&b, '0');
            break;
        case 'l':
            sb_adds(&b, "con");
            break;
        case 't': case 'T': case '@': case 'A': case 'd': case 'D': {
            ShTm tm;
            char tb[128];
            const char *fmt = "%H:%M:%S";
            char ufmt[100];
            sh_localtime(os_time(), &tm);
            if (*s == 'T') fmt = "%I:%M:%S";
            else if (*s == '@') fmt = "%I:%M %p";
            else if (*s == 'A') fmt = "%H:%M";
            else if (*s == 'd') fmt = "%a %b %d";
            else if (*s == 'D' && s[1] == '{') {
                const char *e = strchr(s + 2, '}');
                if (e) {
                    int l = (int)(e - s - 2);
                    if (l > 98) l = 98;
                    memcpy(ufmt, s + 2, l);
                    ufmt[l] = 0;
                    fmt = l ? ufmt : "%X";
                    s = e;
                }
            }
            sh_strftime(tb, sizeof(tb), fmt, &tm);
            sb_adds(&b, tb);
            break;
        }
        case '[':
            sb_addc(&b, 1);
            break;
        case ']':
            sb_addc(&b, 2);
            break;
        case '\\':
            sb_addc(&b, '\\');
            break;
        case '0': case '1': case '2': case '3': {
            int v = 0, k;
            for (k = 0; k < 3 && s[k] >= '0' && s[k] <= '7'; k++) v = v * 8 + (s[k] - '0');
            s += k - 1;
            sb_addc(&b, v);
            break;
        }
        default:
            sb_addc(&b, '\\');
            sb_addc(&b, *s);
        }
    }
    /* parameter expansion of the result (promptvars) */
    {
        int save = sh.laststatus;
        r = expand_str(b.s, X_HEREDOC);
        sh.laststatus = save;
        expand_error = 0;
    }
    sb_free(&b);
    return r ? r : xstrdup("");
}
