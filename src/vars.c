/*
 * vars.c - shell variables, arrays, scopes, functions and aliases.
 *
 * Function-local variables use dynamic scoping: making a variable local
 * saves its previous state on the scope stack.  Emulated subshells use
 * a journal: the first modification of a variable after a journal mark
 * saves its old state, and var_rollback() restores everything.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

#define NBUCKETS 128

static Var *table[NBUCKETS];
Params sh_params;

static unsigned long rand_state = 1;
static long seconds_base;

/* ---- saved states (scopes and journal) ---- */

typedef struct Saved {
    char *name;
    Var *copy;            /* NULL: variable did not exist */
    int level;
} Saved;

static Saved *scopes;     /* local-variable save stack */
static int nscopes, capscopes;
static int scope_level;

static Saved *journal;
static int njournal, capjournal;
static int jgen_cur;      /* 0 = journal inactive */
static int jgen_next = 1;
static int jstack[64];    /* generation stack */
static int jdepth;

static Var *var_create(const char *name);
static void journal_touch(const char *name, Var *v);

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % NBUCKETS;
}

Var *var_lookup_raw(const char *name)
{
    Var *v;
    for (v = table[hash(name)]; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

/* follow namerefs (declare -n): returns the name finally referred to */
const char *var_resolve(const char *name)
{
    int depth;
    for (depth = 0; depth < 8; depth++) {
        Var *v = var_lookup_raw(name);
        if (!v || !(v->flags & V_NAMEREF) || !v->val || !*v->val) break;
        if (strcmp(v->val, name) == 0) break;
        name = v->val;
    }
    return name;
}

/* lookup following namerefs; *name is updated to the final name */
static Var *var_lookup_resolved(const char **name)
{
    Var *v = var_lookup_raw(*name);
    if (!v || !(v->flags & V_NAMEREF)) return v;
    *name = var_resolve(*name);
    return var_lookup_raw(*name);
}

Var *var_lookup(const char *name)
{
    Var *v;
    if (name[0] == 'P' && strcmp(name, "PIPESTATUS") == 0) pipestatus_sync();
    v = var_lookup_raw(name);
    if (!v || !(v->flags & V_NAMEREF)) return v;    /* one hash lookup */
    return var_lookup_raw(var_resolve(name));
}

/* set the target name of a nameref variable itself */
int var_set_nameref(const char *name, const char *target)
{
    Var *v = var_lookup_raw(name);
    if (v && (v->flags & V_READONLY)) {
        sh_error("%s: readonly variable", name);
        return 1;
    }
    if (strcmp(name, target) == 0) {
        sh_error("%s: nameref variable self references not allowed", name);
        return 1;
    }
    if (!v) v = var_create(name);
    else journal_touch(name, v);
    xfree(v->val);
    v->val = xstrdup(target);
    v->flags = (v->flags & ~V_UNSET) | V_NAMEREF;
    return 0;
}

static void arr_free(Var *v)
{
    int i;
    for (i = 0; i < v->alen; i++) {
        xfree(v->arr[i].key);
        xfree(v->arr[i].val);
    }
    xfree(v->arr);
    v->arr = NULL;
    v->alen = v->acap = 0;
}

static Var *var_copy(Var *v)
{
    Var *c = xcalloc(sizeof(Var));
    int i;
    c->name = xstrdup(v->name);
    c->val = v->val ? xstrdup(v->val) : NULL;
    c->flags = v->flags;
    c->scope = v->scope;
    c->jgen = v->jgen;
    if (v->alen) {
        c->arr = xmalloc(v->alen * sizeof(ArrEl));
        c->alen = c->acap = v->alen;
        for (i = 0; i < v->alen; i++) {
            c->arr[i].idx = v->arr[i].idx;
            c->arr[i].key = v->arr[i].key ? xstrdup(v->arr[i].key) : NULL;
            c->arr[i].val = xstrdup(v->arr[i].val);
        }
    }
    return c;
}

/* replace contents of v by contents of src (src is consumed) */
static void var_assign_from(Var *v, Var *src)
{
    xfree(v->val);
    arr_free(v);
    v->val = src->val;
    v->flags = src->flags;
    v->scope = src->scope;
    v->jgen = src->jgen;
    v->arr = src->arr;
    v->alen = src->alen;
    v->acap = src->acap;
    xfree(src->name);
    xfree(src);
}

static void sync_env(Var *v)
{
    if (v->flags & V_EXPORT) {
        if (v->val && !(v->flags & V_UNSET)) os_setenv(v->name, v->val);
        else if (v->alen && v->arr[0].idx == 0) os_setenv(v->name, v->arr[0].val);
    }
}

static void var_delete(const char *name)
{
    unsigned h = hash(name);
    Var **pp, *v;
    for (pp = &table[h]; (v = *pp) != NULL; pp = &v->next) {
        if (strcmp(v->name, name) == 0) {
            *pp = v->next;
            if (v->flags & V_EXPORT) os_unsetenv(v->name);
            xfree(v->name);
            xfree(v->val);
            arr_free(v);
            xfree(v);
            return;
        }
    }
}

static void save_push(Saved **arr, int *n, int *cap, const char *name, Var *v, int level)
{
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *arr = xrealloc(*arr, *cap * sizeof(Saved));
    }
    (*arr)[*n].name = xstrdup(name);
    (*arr)[*n].copy = v ? var_copy(v) : NULL;
    (*arr)[*n].level = level;
    (*n)++;
}

/* record a variable's state before modification (subshell journal) */
static void journal_touch(const char *name, Var *v)
{
    if (!jgen_cur) return;
    if (v && v->jgen == jgen_cur) return;
    save_push(&journal, &njournal, &capjournal, name, v, jgen_cur);
    if (v) v->jgen = jgen_cur;
}

static Var *var_create(const char *name)
{
    unsigned h = hash(name);
    Var *v;
    journal_touch(name, NULL);
    v = xcalloc(sizeof(Var));
    v->name = xstrdup(name);
    v->jgen = jgen_cur;
    v->next = table[h];
    table[h] = v;
    return v;
}

int var_journal_mark(void)
{
    if (jdepth < 64) jstack[jdepth++] = jgen_cur;
    jgen_cur = jgen_next++;
    return njournal;
}

void var_rollback(int mark)
{
    while (njournal > mark) {
        Saved *s = &journal[--njournal];
        if (!s->copy) {
            var_delete(s->name);
        } else {
            Var *v = var_lookup(s->name);
            if (!v) {
                unsigned h = hash(s->name);
                v = xcalloc(sizeof(Var));
                v->name = xstrdup(s->name);
                v->next = table[h];
                table[h] = v;
            }
            if ((v->flags & V_EXPORT) && !(s->copy->flags & V_EXPORT)) os_unsetenv(v->name);
            var_assign_from(v, s->copy);
            sync_env(v);
        }
        xfree(s->name);
    }
    if (jdepth > 0) jgen_cur = jstack[--jdepth];
    else jgen_cur = 0;
}

/* ---- special variables ---- */

static const char *special_value(Var *v)
{
    static char buf[32];
    char *s;
    if (strcmp(v->name, "RANDOM") == 0) {
        rand_state = rand_state * 1103515245UL + 12345UL;
        s = str_itoa((ShInt)((rand_state >> 16) & 0x7FFF));
    } else if (strcmp(v->name, "SRANDOM") == 0) {
        rand_state = rand_state * 1103515245UL + 12345UL;
        s = str_itoa((ShInt)(rand_state ^ (rand_state >> 13)) & 0xFFFFFFFFL);
    } else if (strcmp(v->name, "SECONDS") == 0) {
        s = str_itoa(os_time() - seconds_base);
    } else if (strcmp(v->name, "LINENO") == 0) {
        s = str_itoa(sh.lineno);
    } else if (strcmp(v->name, "EPOCHSECONDS") == 0) {
        s = str_itoa(os_time());
    } else if (strcmp(v->name, "BASH_MONOSECONDS") == 0) {
        s = str_itoa(os_monoseconds());
    } else if (strcmp(v->name, "BASHPID") == 0) {
        s = str_itoa(sh.pid);
    } else if (strcmp(v->name, "BASH_SUBSHELL") == 0) {
        s = str_itoa(sh.subshell);
    } else if (strcmp(v->name, "BASHOPTS") == 0 || strcmp(v->name, "SHELLOPTS") == 0) {
        static char *optstr;
        xfree(optstr);
        optstr = shell_options_string(v->name[0] == 'S');
        return optstr;
    } else {
        return v->val;
    }
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    xfree(s);
    return NEARP(buf);
}

const char *var_get(const char *name)
{
    Var *v = var_lookup(name);
    if (v && (v->flags & V_NAMEREF) && !(v->flags & V_UNSET) && v->val &&
        strcmp(v->val, v->name) != 0) return NULL;   /* dangling nameref */
    if (!v || (v->flags & V_UNSET)) return NULL;
    if (v->flags & V_SPECIAL) return special_value(v);
    if (v->flags & (V_ARRAY | V_ASSOC)) return var_arr_get(v, "0");
    return v->val;
}

static char *apply_case(Var *v, const char *val)
{
    char *r = xstrdup(val), *p;
    if (v->flags & V_UPPER)
        for (p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
    else if (v->flags & V_LOWER)
        for (p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    return r;
}

/* compute value according to attributes (integer, case) */
static char *attr_value(Var *v, const char *val, int *err)
{
    *err = 0;
    if (v->flags & V_INTEGER) {
        ShInt r;
        if (arith_expand_eval(val, &r)) {
            *err = 1;
            return NULL;
        }
        return str_itoa(r);
    }
    if (v->flags & (V_UPPER | V_LOWER)) return apply_case(v, val);
    return xstrdup(val);
}

static int readonly_err(const char *name)
{
    sh_error("%s: readonly variable", name);
    return 1;
}

int var_set(const char *name, const char *val, int flags)
{
    Var *v;
    v = var_lookup_resolved(&name);
    char *nv;
    int err;
    if (v && (v->flags & V_READONLY)) return readonly_err(name);
    if (!v) v = var_create(name);
    else journal_touch(name, v);
    if (v->flags & V_SPECIAL) {
        if (strcmp(name, "RANDOM") == 0 || strcmp(name, "SRANDOM") == 0) {
            rand_state = (unsigned long)atol(val);
            return 0;
        }
        if (strcmp(name, "SECONDS") == 0) {
            seconds_base = os_time() - atol(val);
            return 0;
        }
        v->flags &= ~V_SPECIAL;
    }
    v->flags |= flags & ~V_LOCAL;
    if (sh.opt_allexport) v->flags |= V_EXPORT;
    nv = attr_value(v, val, &err);
    if (err) return 1;
    v->flags &= ~V_UNSET;
    if (v->flags & (V_ARRAY | V_ASSOC)) {
        var_arr_set(name, "0", nv);
        xfree(nv);
        return 0;
    }
    xfree(v->val);
    v->val = nv;
    sync_env(v);
    if (strcmp(name, "HISTSIZE") == 0) hist_set_max(atoi(val));
    return 0;
}

int var_set_int(const char *name, ShInt val)
{
    char *s = str_itoa(val);
    int r = var_set(name, s, 0);
    xfree(s);
    return r;
}

int var_append(const char *name, const char *val)
{
    Var *v;
    v = var_lookup_resolved(&name);
    if (v && (v->flags & V_READONLY)) return readonly_err(name);
    if (v && (v->flags & V_INTEGER)) {
        const char *old = var_get(name);
        char *expr = str_printf("%s+(%s)", old ? old : "0", val);
        int r = var_set(name, expr, 0);
        xfree(expr);
        return r;
    }
    if (v && (v->flags & (V_ARRAY | V_ASSOC))) {
        const char *old = var_arr_get(v, "0");
        char *nv = str_cat(old ? old : "", val);
        int r = var_arr_set(name, "0", nv);
        xfree(nv);
        return r;
    }
    {
        const char *old = var_get(name);
        char *nv = str_cat(old ? old : "", val);
        int r = var_set(name, nv, 0);
        xfree(nv);
        return r;
    }
}

int var_unset(const char *name)
{
    Var *v;
    v = var_lookup_resolved(&name);
    if (!v) return 0;
    if (v->flags & V_READONLY) return readonly_err(name);
    journal_touch(name, v);
    if (v->scope > 0 && v->scope == scope_level) {
        /* unsetting a local keeps it local but unset */
        xfree(v->val);
        v->val = NULL;
        arr_free(v);
        if (v->flags & V_EXPORT) os_unsetenv(name);
        v->flags = (v->flags & V_LOCAL) | V_UNSET;
        return 0;
    }
    var_delete(name);
    return 0;
}

/* ---- arrays ---- */

static int is_assoc(Var *v)
{
    return (v->flags & V_ASSOC) != 0;
}

static int arr_find(Var *v, const char *key, int *ins)
{
    int i;
    if (is_assoc(v)) {
        for (i = 0; i < v->alen; i++)
            if (strcmp(v->arr[i].key, key) == 0) return i;
        *ins = v->alen;
        return -1;
    }
    {
        ShInt idx = 0;
        int lo = 0, hi = v->alen - 1;
        str_toint(key, &idx);
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (v->arr[mid].idx == idx) return mid;
            if (v->arr[mid].idx < idx) lo = mid + 1;
            else hi = mid - 1;
        }
        *ins = lo;
        return -1;
    }
}

Var *var_make_array(const char *name, int assoc)
{
    Var *v;
    v = var_lookup_resolved(&name);
    if (!v) v = var_create(name);
    else journal_touch(name, v);
    if (!(v->flags & (V_ARRAY | V_ASSOC))) {
        char *old = (v->flags & V_UNSET) ? NULL : v->val;
        v->val = NULL;
        v->flags |= assoc ? V_ASSOC : V_ARRAY;
        v->flags &= ~(V_UNSET | V_SPECIAL);
        if (old) {
            v->arr = xmalloc(4 * sizeof(ArrEl));
            v->acap = 4;
            v->alen = 1;
            v->arr[0].idx = 0;
            v->arr[0].key = assoc ? xstrdup("0") : NULL;
            v->arr[0].val = old;
        }
    }
    return v;
}

int var_arr_set(const char *name, const char *key, const char *val)
{
    Var *v;
    v = var_lookup_resolved(&name);
    int ins = 0, i;
    char *nv;
    int err;
    if (v && (v->flags & V_READONLY)) return readonly_err(name);
    if (!v || !(v->flags & (V_ARRAY | V_ASSOC))) v = var_make_array(name, 0);
    else journal_touch(name, v);
    nv = attr_value(v, val, &err);
    if (err) return 1;
    v->flags &= ~V_UNSET;
    i = arr_find(v, key, &ins);
    if (i >= 0) {
        xfree(v->arr[i].val);
        v->arr[i].val = nv;
    } else {
        if (v->alen >= v->acap) {
            v->acap = v->acap ? v->acap * 2 : 4;
            v->arr = xrealloc(v->arr, v->acap * sizeof(ArrEl));
        }
        memmove(v->arr + ins + 1, v->arr + ins, (v->alen - ins) * sizeof(ArrEl));
        v->arr[ins].key = is_assoc(v) ? xstrdup(key) : NULL;
        if (!is_assoc(v)) {
            ShInt idx = 0;
            str_toint(key, &idx);
            v->arr[ins].idx = idx;
        } else {
            v->arr[ins].idx = 0;
        }
        v->arr[ins].val = nv;
        v->alen++;
        i = ins;
    }
    if (i == 0 && !is_assoc(v) && v->arr[0].idx == 0) sync_env(v);
    return 0;
}

const char *var_arr_get(Var *v, const char *key)
{
    int ins, i;
    if (!v) return NULL;
    if (!(v->flags & (V_ARRAY | V_ASSOC))) return strcmp(key, "0") == 0 ? v->val : NULL;
    i = arr_find(v, key, &ins);
    return i >= 0 ? v->arr[i].val : NULL;
}

int var_arr_unset(const char *name, const char *key)
{
    Var *v;
    v = var_lookup_resolved(&name);
    int ins, i;
    if (!v) return 0;
    if (v->flags & V_READONLY) return readonly_err(name);
    if (!(v->flags & (V_ARRAY | V_ASSOC))) {
        if (strcmp(key, "0") == 0) return var_unset(name);
        return 0;
    }
    journal_touch(name, v);
    i = arr_find(v, key, &ins);
    if (i < 0) return 0;
    xfree(v->arr[i].key);
    xfree(v->arr[i].val);
    memmove(v->arr + i, v->arr + i + 1, (v->alen - i - 1) * sizeof(ArrEl));
    v->alen--;
    return 0;
}

void var_arr_clear(Var *v)
{
    journal_touch(v->name, v);
    arr_free(v);
}

int var_arr_count(Var *v)
{
    if (!v) return 0;
    if (v->flags & (V_ARRAY | V_ASSOC)) return v->alen;
    return v->val && !(v->flags & V_UNSET) ? 1 : 0;
}

void var_arr_values(Var *v, StrList *out, int keys)
{
    int i;
    if (!(v->flags & (V_ARRAY | V_ASSOC))) {
        if (v->val) sl_adddup(out, keys ? "0" : v->val);
        return;
    }
    for (i = 0; i < v->alen; i++) {
        if (keys) {
            if (is_assoc(v)) sl_adddup(out, v->arr[i].key);
            else sl_add(out, str_itoa(v->arr[i].idx));
        } else {
            sl_adddup(out, v->arr[i].val);
        }
    }
}

/* ---- scopes ---- */

void var_push_scope(void)
{
    scope_level++;
}

void var_pop_scope(void)
{
    while (nscopes > 0 && scopes[nscopes - 1].level == scope_level) {
        Saved *s = &scopes[--nscopes];
        Var *v = var_lookup(s->name);
        if (v) journal_touch(s->name, v);
        if (!s->copy) {
            var_delete(s->name);
        } else {
            if (!v) {
                unsigned h = hash(s->name);
                v = xcalloc(sizeof(Var));
                v->name = xstrdup(s->name);
                v->next = table[h];
                table[h] = v;
            }
            if ((v->flags & V_EXPORT) && !(s->copy->flags & V_EXPORT)) os_unsetenv(v->name);
            s->copy->jgen = v->jgen;
            var_assign_from(v, s->copy);
            sync_env(v);
        }
        xfree(s->name);
    }
    if (scope_level > 0) scope_level--;
}

int var_scope_level(void)
{
    return scope_level;
}

int var_make_local(const char *name)
{
    Var *v = var_lookup_raw(name);
    if (scope_level == 0) {
        sh_error("local: can only be used in a function");
        return 1;
    }
    if (v && v->scope == scope_level && (v->flags & V_LOCAL)) return 0;   /* already local */
    if (v && (v->flags & V_READONLY)) return readonly_err(name);
    save_push(&scopes, &nscopes, &capscopes, name, v, scope_level);
    if (!v) v = var_create(name);
    else journal_touch(name, v);
    /* the local starts unset; the exported attribute is inherited */
    xfree(v->val);
    v->val = NULL;
    arr_free(v);
    v->flags = (v->flags & V_EXPORT) | V_LOCAL | V_UNSET;
    v->scope = scope_level;
    return 0;
}

/* ---- listing ---- */

static int cmp_var(const void *a, const void *b)
{
    return strcmp((*(Var *const *)a)->name, (*(Var *const *)b)->name);
}

static Var **sorted_vars(int *n)
{
    Var **list, *v;
    int i, cnt = 0;
    pipestatus_sync();
    for (i = 0; i < NBUCKETS; i++)
        for (v = table[i]; v; v = v->next) cnt++;
    list = xmalloc((cnt + 1) * sizeof(Var *));
    cnt = 0;
    for (i = 0; i < NBUCKETS; i++)
        for (v = table[i]; v; v = v->next) list[cnt++] = v;
    qsort(list, cnt, sizeof(Var *), cmp_var);
    *n = cnt;
    return list;
}

void var_names(StrList *out, const char *prefix)
{
    int n, i;
    Var **list = sorted_vars(&n);
    size_t pl = prefix ? strlen(prefix) : 0;
    for (i = 0; i < n; i++) {
        if (list[i]->flags & V_UNSET) continue;
        if (!pl || strncmp(list[i]->name, prefix, pl) == 0) sl_adddup(out, list[i]->name);
    }
    xfree(list);
}

/* value quoted for reuse as input (set output) */
static void put_quoted(SBuf *b, const char *s)
{
    const char *p;
    int safe = *s != 0;
    for (p = s; *p; p++)
        if (!(isalnum((unsigned char)*p) || strchr("_./:@%^+=,-", *p))) safe = 0;
    if (safe) {
        sb_adds(b, s);
        return;
    }
    for (p = s; *p; p++)
        if ((unsigned char)*p < 32 || *p == 127) break;
    if (*p) {
        /* control characters: $'...' form */
        sb_adds(b, "$'");
        for (p = s; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '\n') sb_adds(b, "\\n");
            else if (c == '\t') sb_adds(b, "\\t");
            else if (c == '\r') sb_adds(b, "\\r");
            else if (c == 27) sb_adds(b, "\\E");
            else if (c == '\'') sb_adds(b, "\\'");
            else if (c == '\\') sb_adds(b, "\\\\");
            else if (c < 32 || c == 127) sb_printf(b, "\\%03o", c);
            else sb_addc(b, c);
        }
        sb_addc(b, '\'');
        return;
    }
    sb_addc(b, '\'');
    for (p = s; *p; p++) {
        if (*p == '\'') sb_adds(b, "'\\''");
        else sb_addc(b, *p);
    }
    sb_addc(b, '\'');
}

static void put_dquoted(SBuf *b, const char *s)
{
    sb_addc(b, '"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\' || *s == '$' || *s == '`') sb_addc(b, '\\');
        sb_addc(b, *s);
    }
    sb_addc(b, '"');
}

/* attribute letters in bash's order (declare -p, ${x@a}); buf >= 12 */
void var_attr_string(Var *v, char *buf)
{
    int k = 0;
    if (v) {
        if (v->flags & V_ARRAY) buf[k++] = 'a';
        if (v->flags & V_ASSOC) buf[k++] = 'A';
        if (v->flags & V_INTEGER) buf[k++] = 'i';
        if (v->flags & V_NAMEREF) buf[k++] = 'n';
        if (v->flags & V_READONLY) buf[k++] = 'r';
        if (v->flags & V_EXPORT) buf[k++] = 'x';
        if (v->flags & V_LOWER) buf[k++] = 'l';
        if (v->flags & V_UPPER) buf[k++] = 'u';
    }
    buf[k] = 0;
}

/* "value" or $'value' when it has control characters */
static void put_declval(SBuf *b, const char *s)
{
    const char *p;
    for (p = s; *p; p++)
        if ((unsigned char)*p < 32 || *p == 127) {
            put_quoted(b, s);
            return;
        }
    put_dquoted(b, s);
}

void var_print_decl(int fd, Var *v)
{
    SBuf b;
    char fl[16];
    int i;
    sb_init(&b);
    var_attr_string(v, fl);
    if (!fl[0]) strcpy(fl, "-");
    sb_printf(&b, "declare -%s %s", fl, v->name);
    if (v->flags & (V_ARRAY | V_ASSOC)) {
        if (!(v->flags & V_UNSET)) {
            sb_adds(&b, "=(");
            for (i = 0; i < v->alen; i++) {
                if (v->flags & V_ASSOC) {
                    const char *kp;
                    int plain = *v->arr[i].key != 0;
                    for (kp = v->arr[i].key; *kp; kp++)
                        if (!(isalnum((unsigned char)*kp) || strchr("_./:@%^+=,-", *kp))) plain = 0;
                    sb_addc(&b, '[');
                    if (plain) sb_adds(&b, v->arr[i].key);
                    else put_declval(&b, v->arr[i].key);
                    sb_adds(&b, "]=");
                } else {
                    char *n = str_itoa(v->arr[i].idx);
                    sb_printf(&b, "[%s]=", n);
                    xfree(n);
                }
                put_declval(&b, v->arr[i].val);
                if (i + 1 < v->alen || (v->flags & V_ASSOC)) sb_addc(&b, ' ');
            }
            sb_addc(&b, ')');
        }
    } else if (!(v->flags & V_UNSET)) {
        const char *val = (v->flags & V_SPECIAL) ? special_value(v) : v->val;
        if (val) {
            sb_addc(&b, '=');
            put_declval(&b, val);
        }
    }
    sb_addc(&b, '\n');
    sh_write(fd, b.s, b.len);
    sb_free(&b);
}

/* fmt 0: set (name=value), 1: declare -p style; filter: required flags */
void var_list(int fd, int filter, int fmt)
{
    int n, i;
    Var **list = sorted_vars(&n);
    for (i = 0; i < n; i++) {
        Var *v = list[i];
        if (filter && !(v->flags & filter)) continue;
        if (fmt == 1) {
            var_print_decl(fd, v);
        } else {
            SBuf b;
            if (v->flags & V_UNSET) continue;
            sb_init(&b);
            sb_printf(&b, "%s=", v->name);
            if (v->flags & (V_ARRAY | V_ASSOC)) {
                int k;
                sb_addc(&b, '(');
                for (k = 0; k < v->alen; k++) {
                    if (v->flags & V_ASSOC) sb_printf(&b, "[%s]=", v->arr[k].key);
                    else {
                        char *num = str_itoa(v->arr[k].idx);
                        sb_printf(&b, "[%s]=", num);
                        xfree(num);
                    }
                    put_dquoted(&b, v->arr[k].val);
                    if (k + 1 < v->alen) sb_addc(&b, ' ');
                }
                sb_addc(&b, ')');
            } else {
                put_quoted(&b, (v->flags & V_SPECIAL) ? special_value(v) : (v->val ? v->val : ""));
            }
            sb_addc(&b, '\n');
            sh_write(fd, b.s, b.len);
            sb_free(&b);
        }
    }
    xfree(list);
}

char *shell_quote(const char *s)
{
    SBuf b;
    sb_init(&b);
    if (!*s) {
        sb_adds(&b, "''");
        return sb_take(&b);
    }
    put_quoted(&b, s);
    return sb_take(&b);
}

void var_export_all(void)
{
    int i;
    Var *v;
    for (i = 0; i < NBUCKETS; i++)
        for (v = table[i]; v; v = v->next) sync_env(v);
}

/* ---- positional parameters ---- */

void params_set(char **argv, int n)
{
    int i;
    Params np;
    np.v = xmalloc((n + 1) * sizeof(char *));
    for (i = 0; i < n; i++) np.v[i] = xstrdup(argv[i]);
    np.v[n] = NULL;
    np.n = n;
    params_free(&sh_params);
    sh_params = np;
}

void params_free(Params *p)
{
    int i;
    for (i = 0; i < p->n; i++) xfree(p->v[i]);
    xfree(p->v);
    p->v = NULL;
    p->n = 0;
}

void params_copy(Params *dst, Params *src)
{
    int i;
    dst->n = src->n;
    dst->v = xmalloc((src->n + 1) * sizeof(char *));
    for (i = 0; i < src->n; i++) dst->v[i] = xstrdup(src->v[i]);
    dst->v[src->n] = NULL;
}

/* ---- functions ---- */

static Func *funcs;

/* journal of function and alias changes made inside emulated subshells */
typedef struct FSaved {
    int alias;            /* 1: alias entry                           */
    char *name;
    int existed;
    Node *body;           /* functions: old definition (arena ref'd)  */
    Arena *arena;
    char *val;            /* aliases: old value                       */
} FSaved;
static FSaved *fjournal;
static int nfjournal, capfjournal, fjdepth;

static void fj_push(int alias, const char *name, int existed, Node *body, Arena *arena, const char *val)
{
    FSaved *e;
    if (!fjdepth) return;
    if (nfjournal >= capfjournal) {
        capfjournal = capfjournal ? capfjournal * 2 : 16;
        fjournal = xrealloc(fjournal, capfjournal * sizeof(FSaved));
    }
    e = &fjournal[nfjournal++];
    e->alias = alias;
    e->name = xstrdup(name);
    e->existed = existed;
    e->body = body;
    e->arena = arena;
    if (arena) arena_ref(arena);
    e->val = val ? xstrdup(val) : NULL;
}

int func_journal_mark(void)
{
    fjdepth++;
    return nfjournal;
}

void func_rollback(int mark)
{
    while (nfjournal > mark) {
        FSaved *e = &fjournal[--nfjournal];
        if (e->alias) {
            int save = fjdepth;
            fjdepth = 0;
            if (e->existed) alias_set(e->name, e->val);
            else alias_unset(e->name);
            fjdepth = save;
            xfree(e->val);
        } else if (!e->existed) {
            int save = fjdepth;
            fjdepth = 0;
            func_unset(e->name);
            fjdepth = save;
        } else {
            Func *f = func_lookup(e->name);
            if (!f) {
                f = xcalloc(sizeof(Func));
                f->name = xstrdup(e->name);
                f->next = funcs;
                funcs = f;
            } else if (f->arena && f->refs == 0) {
                arena_free(f->arena);
            }
            f->body = e->body;
            f->arena = e->arena;      /* the journal's reference */
        }
        xfree(e->name);
    }
    if (fjdepth > 0) fjdepth--;
}

Func *func_lookup(const char *name)
{
    Func *f;
    for (f = funcs; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

void func_define(const char *name, Node *body, Arena *arena)
{
    Func *f = func_lookup(name);
    fj_push(0, name, f != NULL, f ? f->body : NULL, f ? f->arena : NULL, NULL);
    arena_ref(arena);
    if (f) {
        if (f->refs > 0) {
            /* function is running: keep old arena alive (leak until exit) */
            f->arena = NULL;
        } else {
            arena_free(f->arena);
        }
        f->body = body;
        f->arena = arena;
        return;
    }
    f = xcalloc(sizeof(Func));
    f->name = xstrdup(name);
    f->body = body;
    f->arena = arena;
    f->next = funcs;
    funcs = f;
}

int func_unset(const char *name)
{
    Func **pp, *f;
    for (pp = &funcs; (f = *pp) != NULL; pp = &f->next) {
        if (strcmp(f->name, name) == 0) {
            fj_push(0, name, 1, f->body, f->arena, NULL);
            *pp = f->next;
            if (f->refs == 0) arena_free(f->arena);
            xfree(f->name);
            xfree(f);
            return 0;
        }
    }
    return 1;
}

void func_names(StrList *out, const char *prefix)
{
    Func *f;
    size_t pl = prefix ? strlen(prefix) : 0;
    for (f = funcs; f; f = f->next)
        if (!pl || strncmp(f->name, prefix, pl) == 0) sl_adddup(out, f->name);
    sl_sort(out, 0);
}

/* ---- aliases ---- */

typedef struct Alias { char *name, *val; struct Alias *next; } Alias;
static Alias *aliases;

const char *alias_get(const char *name)
{
    Alias *a;
    for (a = aliases; a; a = a->next)
        if (strcmp(a->name, name) == 0) return a->val;
    return NULL;
}

void alias_set(const char *name, const char *val)
{
    Alias *a;
    fj_push(1, name, alias_get(name) != NULL, NULL, NULL, alias_get(name));
    for (a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            xfree(a->val);
            a->val = xstrdup(val);
            return;
        }
    }
    a = xmalloc(sizeof(Alias));
    a->name = xstrdup(name);
    a->val = xstrdup(val);
    a->next = aliases;
    aliases = a;
}

int alias_unset(const char *name)
{
    Alias **pp, *a;
    if (strcmp(name, "-a") == 0) {
        while (aliases) {
            a = aliases;
            fj_push(1, a->name, 1, NULL, NULL, a->val);
            aliases = a->next;
            xfree(a->name);
            xfree(a->val);
            xfree(a);
        }
        return 0;
    }
    for (pp = &aliases; (a = *pp) != NULL; pp = &a->next) {
        if (strcmp(a->name, name) == 0) {
            fj_push(1, name, 1, NULL, NULL, a->val);
            *pp = a->next;
            xfree(a->name);
            xfree(a->val);
            xfree(a);
            return 0;
        }
    }
    return 1;
}

void alias_names(StrList *out, const char *prefix)
{
    Alias *a;
    size_t pl = prefix ? strlen(prefix) : 0;
    for (a = aliases; a; a = a->next)
        if (!pl || strncmp(a->name, prefix, pl) == 0) sl_adddup(out, a->name);
    sl_sort(out, 0);
}

/* ---- init ---- */

static void import_env(const char *n, const char *v, void *ud)
{
    (void)ud;
    if (!str_isname(n)) return;
    if (n[0] == '_' && !n[1]) return;   /* $_ is the shell's own */
    var_set(n, v, V_EXPORT);
}

void vars_init(void)
{
    static const char *const specials[] = {
        "RANDOM", "SRANDOM", "SECONDS", "LINENO", "EPOCHSECONDS", "BASHPID",
        "BASHOPTS", "SHELLOPTS", "BASH_SUBSHELL", "BASH_MONOSECONDS", NULL
    };
    int i;
    char *s;
    rand_state = os_random_seed();
    seconds_base = os_time();
    os_environ(import_env, NULL);
    for (i = 0; specials[i]; i++) {
        Var *v = var_lookup(specials[i]);
        if (!v) v = var_create(specials[i]);
        v->flags |= V_SPECIAL;
        v->flags &= ~V_EXPORT;
    }
    if (!var_get("HOME")) var_set("HOME", os_default_home(), V_EXPORT);
    if (!var_get("PATH")) var_set("PATH", os_default_path(), V_EXPORT);
    if (!var_get("USER")) var_set("USER", os_username(), 0);
    var_set("HOSTNAME", os_hostname(), 0);
    if (!var_get("SHELL")) var_set("SHELL", "/bin/bash", 0);
    var_set("BASH", "/bin/bash", 0);
    var_set("BASH_VERSION", AMIBASH_VERSION, 0);
    var_set("AMIBASH_VERSION", AMIBASH_SHORTVER " (" AMIBASH_BUILD " build)", 0);
    var_make_array("BASH_VERSINFO", 0);
    {
        /* "5.3.20(1)-amiga": major, minor, patch level, build */
        const char *p = AMIBASH_VERSION;
        int k;
        for (k = 0; k < 4; k++) {
            char num[12], key[2];
            int n = 0;
            while (*p >= '0' && *p <= '9' && n < 11) num[n++] = *p++;
            num[n] = 0;
            key[0] = (char)('0' + k);
            key[1] = 0;
            var_arr_set("BASH_VERSINFO", key, n ? num : "0");
            if (*p) p++;
        }
    }
    var_arr_set("BASH_VERSINFO", "4", "release");
#ifdef AMIGA
    var_arr_set("BASH_VERSINFO", "5", "m68k-amiga-amigaos");
    var_set("OSTYPE", "amigaos", 0);
    var_set("MACHTYPE", "m68k-amiga-amigaos", 0);
    var_set("HOSTTYPE", "m68k", 0);
#else
    var_arr_set("BASH_VERSINFO", "5", "x86_64-pc-linux-gnu");
    var_set("OSTYPE", "linux-gnu", 0);
    var_set("MACHTYPE", "x86_64-pc-linux-gnu", 0);
    var_set("HOSTTYPE", "x86_64", 0);
#endif
    {
        Var *bv = var_lookup("BASH_VERSINFO");
        if (bv) bv->flags |= V_READONLY;      /* as in bash */
    }
    var_set("IFS", " \t\n", 0);
    var_set("PS2", "> ", 0);
    var_set("PS4", "+ ", 0);
    var_set("OPTIND", "1", 0);
    var_set("HISTSIZE", "500", 0);
    s = str_itoa(os_pid());
    var_set("PPID", s, 0);
    xfree(s);
    var_set("UID", "1000", 0);
    var_set("EUID", "1000", 0);
    var_set("SHLVL", var_get("SHLVL") ? var_get("SHLVL") : "0", V_EXPORT);
    {
        ShInt lvl = 0;
        str_toint(var_get("SHLVL"), &lvl);
        var_set_int("SHLVL", lvl + 1);
    }
}
