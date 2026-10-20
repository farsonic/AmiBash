/*
 * arith.c - shell arithmetic: C-like expressions on 64-bit integers.
 */
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include "sh.h"

typedef struct AState {
    const char *s;        /* whole expression                        */
    const char *p;        /* current position                        */
    int noeval;           /* inside short-circuited branch           */
    jmp_buf jb;
    int depth;
    const char *tok;      /* start of the last token seen (messages)  */
} AState;

const char *arith_errprefix;   /* "((" or "let" for error messages     */

static ShInt a_comma(AState *a);
static ShInt a_assign(AState *a);

static void a_error(AState *a, const char *msg)
{
    const char *s = a->s;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (arith_errprefix)
        sh_error("%s: %s: %s (error token is \"%s\")", arith_errprefix, s, msg, a->tok ? a->tok : a->p);
    else
        sh_error("%s: %s (error token is \"%s\")", s, msg, a->tok ? a->tok : a->p);
    longjmp(a->jb, 1);
}

static void skipws(AState *a)
{
    while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n' || *a->p == '\r') a->p++;
    if (*a->p) a->tok = a->p;
}

static int peek_op(AState *a, const char *op)
{
    const char *p;
    skipws(a);
    p = a->p;
    /* operators are 1-3 chars: compare inline (hot path) */
    if (p[0] != op[0]) return 0;
    if (!op[1]) return 1;
    if (p[1] != op[1]) return 0;
    if (!op[2]) return 1;
    return p[2] == op[2];
}

static int accept(AState *a, const char *op)
{
    if (peek_op(a, op)) {
        a->p += op[1] ? (op[2] ? 3 : 2) : 1;
        return 1;
    }
    return 0;
}

static int digit_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 36;
    if (c == '@') return 62;
    if (c == '_') return 63;
    return 99;
}

/* parse a numeric constant at p; returns 1 ok */
static int parse_number(const char **pp, ShInt *out)
{
    const char *p = *pp;
    ShInt v = 0;
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
        if (!isxdigit((unsigned char)*p)) return 0;
    } else if (p[0] == '0' && isdigit((unsigned char)p[1])) {
        base = 8;
        p++;
    } else {
        const char *q = p;
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '#') {
            int b = 0;
            while (p < q) b = b * 10 + (*p++ - '0');
            if (b < 2 || b > 64) return -1;
            base = b;
            p = q + 1;
        }
    }
    if (base == 10) {
        /* fast decimal path, 32-bit while it cannot overflow */
        const char *start = p;
        unsigned long w = 0;
        while (*p >= '0' && *p <= '9' && w < 0x0CCCCCCCUL) w = w * 10 + (unsigned long)(*p++ - '0');
        v = (ShInt)w;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        if (isalnum((unsigned char)*p) || *p == '@' || *p == '_') return -1;
        if (p == start) return 0;
        *out = v;
        *pp = p;
        return 1;
    }
    {
        const char *start = p;
        while (*p) {
            int d = digit_val((unsigned char)*p);
            if (base <= 36 && d >= 36) d -= 26;     /* case-insensitive */
            if (d >= base) {
                if (isalnum((unsigned char)*p) || *p == '@' || *p == '_') return -1;
                break;
            }
            v = v * base + d;
            p++;
        }
        if (p == start && base != 8) return 0;
    }
    *out = v;
    *pp = p;
    return 1;
}

int arith_eval(const char *expr, ShInt *result);

/* value of a variable as arithmetic */
static ShInt var_value(AState *a, const char *name, const char *sub)
{
    const char *val;
    ShInt v;
    if (sub) {
        Var *var = var_lookup(name);
        char *key;
        if (var && (var->flags & V_ASSOC)) {
            key = expand_str(sub, X_NOSPLIT | X_NOGLOB);
        } else {
            ShInt idx;
            if (arith_expand_eval(sub, &idx)) longjmp(a->jb, 1);
            if (idx < 0 && var && var->alen) idx += var->arr[var->alen - 1].idx + 1;
            key = str_itoa(idx);
        }
        if (var && (var->flags & (V_ARRAY | V_ASSOC))) val = var_arr_get(var, key);
        else val = strcmp(key, "0") == 0 ? var_get(name) : NULL;
        xfree(key);
    } else {
        Var *var = var_lookup(name);
        if (var && (var->flags & (V_ARRAY | V_ASSOC))) val = var_arr_get(var, "0");
        else val = var_get(name);
    }
    if (!val) {
        if (sh.opt_nounset && !a->noeval) {
            sh_error("%s: unbound variable", name);
            expansion_failed(2);        /* fatal in a script */
            longjmp(a->jb, 1);
        }
        return 0;
    }
    while (*val == ' ' || *val == '\t') val++;
    if (!*val) return 0;
    {
        const char *q = val;
        int r = parse_number(&q, &v);
        while (*q == ' ' || *q == '\t') q++;
        if (r == 1 && *q == 0) return v;
    }
    if (a->depth > 30) a_error(a, "expression recursion level exceeded");
    {
        AState sub_a;
        sub_a.s = val;
        sub_a.p = val;
        sub_a.noeval = a->noeval;
        sub_a.depth = a->depth + 1;
        sub_a.tok = NULL;
        if (setjmp(sub_a.jb)) longjmp(a->jb, 1);
        v = a_comma(&sub_a);
        skipws(&sub_a);
        if (*sub_a.p) a_error(&sub_a, "syntax error in expression");
    }
    return v;
}

static void set_var(AState *a, const char *name, const char *sub, ShInt v)
{
    char *s;
    if (a->noeval) return;
    s = str_itoa(v);
    if (sub) {
        Var *var = var_lookup(name);
        char *key;
        if (var && (var->flags & V_ASSOC)) {
            key = expand_str(sub, X_NOSPLIT | X_NOGLOB);
        } else {
            ShInt idx;
            if (arith_expand_eval(sub, &idx)) {
                xfree(s);
                longjmp(a->jb, 1);
            }
            key = str_itoa(idx);
        }
        if (var_arr_set(name, key, s)) {
            xfree(key);
            xfree(s);
            longjmp(a->jb, 1);
        }
        xfree(key);
    } else if (var_set(name, s, 0)) {
        xfree(s);
        longjmp(a->jb, 1);
    }
    xfree(s);
}

/* read identifier and optional subscript; returns 1 if found */
static int read_lvalue(AState *a, char *name, int nsz, char **sub)
{
    const char *p;
    int n = 0;
    skipws(a);
    p = a->p;
    *sub = NULL;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return 0;
    while (isalnum((unsigned char)*p) || *p == '_') {
        if (n < nsz - 1) name[n++] = *p;
        p++;
    }
    name[n] = 0;
    if (*p == '[') {
        int depth = 1;
        const char *q = p + 1;
        while (*q && depth) {
            if (*q == '[') depth++;
            else if (*q == ']') depth--;
            if (depth) q++;
        }
        if (!*q) a_error(a, "missing `]'");
        *sub = xstrndup(p + 1, q - p - 1);
        p = q + 1;
    }
    a->p = p;
    return 1;
}

static ShInt a_primary(AState *a)
{
    ShInt v;
    skipws(a);
    if (*a->p == '(') {
        a->p++;
        v = a_comma(a);
        if (!accept(a, ")")) a_error(a, "missing `)'");
        return v;
    }
    if (isdigit((unsigned char)*a->p)) {
        const char *start = a->p;
        int r = parse_number(&a->p, &v);
        if (r != 1) {
            /* bash reports the whole constant: "08: value too great..." */
            const char *e = start;
            char num[64];
            int l;
            while (isalnum((unsigned char)*e) || *e == '#' || *e == '@' || *e == '_') e++;
            l = (int)(e - start) < 63 ? (int)(e - start) : 63;
            memcpy(num, start, l);
            num[l] = 0;
            if (r < 0) {
                if (arith_errprefix) sh_error("%s: %s: value too great for base (error token is \"%s\")", arith_errprefix, num, num);
                else sh_error("%s: value too great for base (error token is \"%s\")", num, num);
                longjmp(a->jb, 1);
            }
            a_error(a, "syntax error: invalid arithmetic operator");
        }
        return v;
    }
    if (isalpha((unsigned char)*a->p) || *a->p == '_') {
        char name[128];
        char *sub;
        read_lvalue(a, name, sizeof(name), &sub);
        skipws(a);
        if ((a->p[0] == '+' && a->p[1] == '+') || (a->p[0] == '-' && a->p[1] == '-')) {
            int inc = a->p[0] == '+' ? 1 : -1;
            a->p += 2;
            v = var_value(a, name, sub);
            set_var(a, name, sub, v + inc);
            xfree(sub);
            return v;
        }
        v = var_value(a, name, sub);
        xfree(sub);
        return v;
    }
    if (!*a->p) a_error(a, "syntax error: operand expected");
    a_error(a, "syntax error: operand expected");
    return 0;
}

static ShInt a_unary(AState *a)
{
    skipws(a);
    if ((a->p[0] == '+' && a->p[1] == '+') || (a->p[0] == '-' && a->p[1] == '-')) {
        int inc = a->p[0] == '+' ? 1 : -1;
        char name[128];
        char *sub;
        ShInt v;
        a->p += 2;
        if (!read_lvalue(a, name, sizeof(name), &sub)) a_error(a, "syntax error: operand expected");
        v = var_value(a, name, sub) + inc;
        set_var(a, name, sub, v);
        xfree(sub);
        return v;
    }
    if (*a->p == '-') {
        a->p++;
        return -a_unary(a);
    }
    if (*a->p == '+') {
        a->p++;
        return a_unary(a);
    }
    if (*a->p == '!') {
        a->p++;
        return !a_unary(a);
    }
    if (*a->p == '~') {
        a->p++;
        return ~a_unary(a);
    }
    return a_primary(a);
}

static ShInt a_power(AState *a)
{
    ShInt b = a_unary(a);
    if (peek_op(a, "**")) {
        ShInt e, r = 1;
        a->p += 2;
        e = a_power(a);          /* right associative */
        if (e < 0) a_error(a, "exponent less than 0");
        while (e-- > 0) r *= b;
        return r;
    }
    return b;
}

static ShInt a_mul(AState *a)
{
    ShInt v = a_power(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '*' && a->p[1] != '*' && a->p[1] != '=') {
            a->p++;
            v *= a_power(a);
        } else if ((a->p[0] == '/' || a->p[0] == '%') && a->p[1] != '=') {
            int op = *a->p++;
            const char *at = a->p;
            ShInt r = a_power(a);
            if (r == 0) {
                if (a->noeval) {
                    v = 0;
                    continue;
                }
                a->p = at;
                a_error(a, "division by 0");
            }
            if (r == -1) v = op == '/' ? -v : 0;   /* avoid overflow trap */
            else v = op == '/' ? v / r : v % r;
        } else {
            return v;
        }
    }
}

static ShInt a_add(AState *a)
{
    ShInt v = a_mul(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '+' && a->p[1] != '+' && a->p[1] != '=') {
            a->p++;
            v += a_mul(a);
        } else if (a->p[0] == '-' && a->p[1] != '-' && a->p[1] != '=') {
            a->p++;
            v -= a_mul(a);
        } else {
            return v;
        }
    }
}

static ShInt a_shift(AState *a)
{
    ShInt v = a_add(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '<' && a->p[1] == '<' && a->p[2] != '=') {
            a->p += 2;
            v = v << (int)a_add(a);
        } else if (a->p[0] == '>' && a->p[1] == '>' && a->p[2] != '=') {
            a->p += 2;
            v = v >> (int)a_add(a);
        } else {
            return v;
        }
    }
}

static ShInt a_rel(AState *a)
{
    ShInt v = a_shift(a);
    for (;;) {
        skipws(a);
        if (accept(a, "<=")) v = v <= a_shift(a);
        else if (accept(a, ">=")) v = v >= a_shift(a);
        else if (a->p[0] == '<' && a->p[1] != '<') {
            a->p++;
            v = v < a_shift(a);
        } else if (a->p[0] == '>' && a->p[1] != '>') {
            a->p++;
            v = v > a_shift(a);
        } else {
            return v;
        }
    }
}

static ShInt a_eq(AState *a)
{
    ShInt v = a_rel(a);
    for (;;) {
        if (accept(a, "==")) v = v == a_rel(a);
        else if (accept(a, "!=")) v = v != a_rel(a);
        else return v;
    }
}

static ShInt a_band(AState *a)
{
    ShInt v = a_eq(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '&' && a->p[1] != '&' && a->p[1] != '=') {
            a->p++;
            v &= a_eq(a);
        } else {
            return v;
        }
    }
}

static ShInt a_bxor(AState *a)
{
    ShInt v = a_band(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '^' && a->p[1] != '=') {
            a->p++;
            v ^= a_band(a);
        } else {
            return v;
        }
    }
}

static ShInt a_bor(AState *a)
{
    ShInt v = a_bxor(a);
    for (;;) {
        skipws(a);
        if (a->p[0] == '|' && a->p[1] != '|' && a->p[1] != '=') {
            a->p++;
            v |= a_bxor(a);
        } else {
            return v;
        }
    }
}

static ShInt a_land(AState *a)
{
    ShInt v = a_bor(a);
    while (accept(a, "&&")) {
        ShInt r;
        if (!v) a->noeval++;
        r = a_bor(a);
        if (!v) a->noeval--;
        v = v && r;
    }
    return v;
}

static ShInt a_lor(AState *a)
{
    ShInt v = a_land(a);
    while (accept(a, "||")) {
        ShInt r;
        if (v) a->noeval++;
        r = a_land(a);
        if (v) a->noeval--;
        v = v || r;
    }
    return v;
}

static ShInt a_cond(AState *a)
{
    ShInt c = a_lor(a);
    if (accept(a, "?")) {
        ShInt t, f;
        if (!c) a->noeval++;
        t = a_assign(a);
        if (!c) a->noeval--;
        if (!accept(a, ":")) a_error(a, "syntax error: `:' expected for conditional expression");
        if (c) a->noeval++;
        f = a_cond(a);
        if (c) a->noeval--;
        return c ? t : f;
    }
    return c;
}

static ShInt a_assign(AState *a)
{
    const char *save;
    char name[128];
    char *sub = NULL;
    static const char *const ops[] = {
        "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=", NULL
    };
    skipws(a);
    save = a->p;
    if (read_lvalue(a, name, sizeof(name), &sub)) {
        int i;
        skipws(a);
        for (i = 0; ops[i]; i++) {
            size_t l = strlen(ops[i]);
            if (strncmp(a->p, ops[i], l) == 0 && !(i == 0 && a->p[1] == '=')) {
                ShInt r, v = 0;
                const char *at;
                a->p += l;
                at = a->p;
                /* like bash, the old value is read before the right side
                 * is evaluated: x += x *= 2 with x=3 gives 9 */
                if (i > 0) v = var_value(a, name, sub);
                r = a_assign(a);
                switch (i) {
                case 0: v = r; break;
                case 1: v *= r; break;
                case 2:
                case 3:
                    if (r == 0) {
                        if (a->noeval) {
                            v = 0;
                            break;
                        }
                        xfree(sub);
                        a->p = at;
                        a_error(a, "division by 0");
                    }
                    v = i == 2 ? v / r : v % r;
                    break;
                case 4: v += r; break;
                case 5: v -= r; break;
                case 6: v <<= (int)r; break;
                case 7: v >>= (int)r; break;
                case 8: v &= r; break;
                case 9: v ^= r; break;
                case 10: v |= r; break;
                }
                set_var(a, name, sub, v);
                xfree(sub);
                return v;
            }
        }
        xfree(sub);
        a->p = save;
    }
    return a_cond(a);
}

static ShInt a_comma(AState *a)
{
    ShInt v = a_assign(a);
    while (accept(a, ",")) v = a_assign(a);
    return v;
}

int arith_eval(const char *expr, ShInt *result)
{
    AState a;
    a.s = expr;
    a.p = expr;
    a.noeval = 0;
    a.depth = 0;
    a.tok = NULL;
    if (setjmp(a.jb)) {
        *result = 0;
        return 1;
    }
    skipws(&a);
    if (!*a.p) {
        *result = 0;
        return 0;
    }
    *result = a_comma(&a);
    skipws(&a);
    if (*a.p) {
        if (*a.p == ')') a_error(&a, "syntax error in expression");
        a_error(&a, "syntax error in expression");
    }
    return 0;
}
