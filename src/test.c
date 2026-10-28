/*
 * test.c - "test", "[" and "[[ ]]" conditional expressions.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

const char *test_cmdname = "test";   /* "[" or "test" for messages */

static const char *const unops[] = {
    "-a", "-b", "-c", "-d", "-e", "-f", "-g", "-h", "-k", "-n", "-o", "-p",
    "-r", "-s", "-t", "-u", "-v", "-w", "-x", "-z", "-G", "-L", "-N", "-O",
    "-R", "-S", NULL
};

static const char *const binops[] = {
    "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", NULL
};

int is_test_unop(const char *op)
{
    int i;
    for (i = 0; unops[i]; i++)
        if (strcmp(op, unops[i]) == 0) return 1;
    return 0;
}

int is_test_binop(const char *op)
{
    int i;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, ">") == 0)
        return 1;
    for (i = 0; binops[i]; i++)
        if (strcmp(op, binops[i]) == 0) return 1;
    return 0;
}

static int option_set(const char *name);

/* -1 = bad operator */
int test_unary(const char *op, const char *arg)
{
    OsDirEnt e;
    int c = op[1];
    switch (c) {
    case 'n':
        return *arg != 0;
    case 'z':
        return *arg == 0;
    case 'o':
        return option_set(arg);
    case 'v': {
        const char *br = strchr(arg, '[');
        if (br) {
            char *name = xstrndup(arg, br - arg);
            char *sub = xstrndup(br + 1, strlen(br + 1) - (arg[strlen(arg) - 1] == ']'));
            Var *v = var_lookup(name);
            int r = 0;
            if (v) {
                if (strcmp(sub, "@") == 0 || strcmp(sub, "*") == 0) {
                    r = var_arr_count(v) > 0;
                } else if (v->flags & V_ASSOC) {
                    r = var_arr_get(v, sub) != NULL;
                } else {
                    ShInt idx;
                    if (!arith_expand_eval(sub, &idx)) {
                        char *k = str_itoa(idx);
                        r = var_arr_get(v, k) != NULL;
                        xfree(k);
                    }
                }
            }
            xfree(name);
            xfree(sub);
            return r;
        }
        return var_get(arg) != NULL || (var_lookup(arg) && var_arr_count(var_lookup(arg)) > 0);
    }
    case 'R': {
        Var *v = var_lookup(arg);
        return v && (v->flags & V_NAMEREF);
    }
    case 't': {
        ShInt fd;
        ShFile *f;
        if (!str_toint(arg, &fd) || fd < 0 || fd >= SH_NFD) return 0;
        f = sh_fd((int)fd);
        return f && f->kind == SHF_OS && f->tty;
    }
    }
    if (!is_test_unop(op)) return -1;
    if (!*arg) return 0;
    if (strcmp(arg, "/dev/null") == 0) return c == 'e' || c == 'a' || c == 'c' || c == 'r' || c == 'w';
    if (strcmp(arg, "/dev/stdin") == 0 || strcmp(arg, "/dev/stdout") == 0 || strcmp(arg, "/dev/stderr") == 0)
        return c == 'e' || c == 'a' || c == 'r' || c == 'w';
    if (sh_stat(arg, &e) != 0) return 0;
    switch (c) {
    case 'a':
    case 'e':
        return 1;
    case 'f':
        return e.type == OS_T_FILE;
    case 'd':
        return e.type == OS_T_DIR || e.type == OS_T_VOL;
    case 'h':
    case 'L':
        return e.type == OS_T_LINK;
    case 'r':
        return (e.prot & OS_P_READ) != 0;
    case 'w':
        return (e.prot & OS_P_WRITE) != 0;
    case 'x':
        return (e.prot & OS_P_EXEC) != 0 || e.type == OS_T_DIR;
    case 's':
        return e.size > 0;
    case 'O':
    case 'G':
        return 1;
    case 'N':
        return 0;
    default:
        return 0;
    }
}

static int file_time(const char *p, long *t)
{
    OsDirEnt e;
    if (sh_stat(p, &e) != 0) return 0;
    *t = e.mtime;
    return 1;
}

static int to_int(const char *s, ShInt *v)
{
    if (str_toint(s, v)) return 1;
    sh_error("%s: %s: integer expected", test_cmdname, s);
    return 0;
}

/* -1 bad operator, -2 error */
int test_binary(const char *a, const char *op, const char *b)
{
    ShInt x, y;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
    if (strcmp(op, "<") == 0) return strcmp(a, b) < 0;
    if (strcmp(op, ">") == 0) return strcmp(a, b) > 0;
    if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0) {
        long ta, tb;
        int ha = file_time(a, &ta), hb = file_time(b, &tb);
        if (op[1] == 'n') return ha && (!hb || ta > tb);
        return hb && (!ha || ta < tb);
    }
    if (strcmp(op, "-ef") == 0) {
        char *na = path_native(a), *nb = path_native(b);
        char *fa = os_fullpath(na), *fb = os_fullpath(nb);
        int r = fa && fb && str_casecmp(fa, fb) == 0;
        xfree(na);
        xfree(nb);
        xfree(fa);
        xfree(fb);
        return r;
    }
    if (op[0] != '-' || !is_test_binop(op)) return -1;
    if (!to_int(a, &x) || !to_int(b, &y)) return -2;
    if (strcmp(op, "-eq") == 0) return x == y;
    if (strcmp(op, "-ne") == 0) return x != y;
    if (strcmp(op, "-lt") == 0) return x < y;
    if (strcmp(op, "-le") == 0) return x <= y;
    if (strcmp(op, "-gt") == 0) return x > y;
    if (strcmp(op, "-ge") == 0) return x >= y;
    return -1;
}

static int option_set(const char *name)
{
    static const struct { const char *n; int off; } opts[] = {
        {"errexit", SHOFF(opt_errexit)}, {"nounset", SHOFF(opt_nounset)},
        {"xtrace", SHOFF(opt_xtrace)}, {"verbose", SHOFF(opt_verbose)},
        {"noglob", SHOFF(opt_noglob)}, {"noclobber", SHOFF(opt_noclobber)},
        {"allexport", SHOFF(opt_allexport)}, {"pipefail", SHOFF(opt_pipefail)},
        {"noexec", SHOFF(opt_noexec)}, {"monitor", SHOFF(opt_monitor)},
        {"emacs", SHOFF(opt_emacs)}, {"vi", SHOFF(opt_vi)}, {"hashall", SHOFF(opt_hashall)},
        {"ignoreeof", SHOFF(opt_ignoreeof)}, {"braceexpand", SHOFF(opt_braceexpand)},
        {"histexpand", SHOFF(opt_histexpand)}, {"history", SHOFF(opt_history)},
        {"posix", SHOFF(opt_posix)}, {NULL, 0}
    };
    int i;
    for (i = 0; opts[i].n; i++)
        if (strcmp(opts[i].n, name) == 0) return *SHOPT(opts[i].off) != 0;
    return 0;
}

/* ---- POSIX test with recursive descent for > 4 args ---- */

typedef struct TState { int argc; char **argv; int pos; int err; } TState;

static int t_or(TState *t);

static const char *t_peek(TState *t, int k)
{
    return t->pos + k < t->argc ? t->argv[t->pos + k] : NULL;
}

static int t_prim(TState *t)
{
    const char *a = t_peek(t, 0), *b, *c;
    if (!a) {
        sh_error("%s: argument expected", test_cmdname);
        t->err = 1;
        return 0;
    }
    if (strcmp(a, "!") == 0) {
        t->pos++;
        return !t_prim(t);
    }
    if (strcmp(a, "(") == 0) {
        int r;
        t->pos++;
        r = t_or(t);
        if (!t_peek(t, 0) || strcmp(t_peek(t, 0), ")") != 0) {
            sh_error("%s: `)' expected", test_cmdname);
            t->err = 1;
            return 0;
        }
        t->pos++;
        return r;
    }
    b = t_peek(t, 1);
    if (b && is_test_binop(b)) {
        int r;
        c = t_peek(t, 2);
        if (!c) {
            sh_error("%s: argument expected", test_cmdname);
            t->err = 1;
            return 0;
        }
        t->pos += 3;
        r = test_binary(a, b, c);
        if (r < 0) {
            t->err = 1;
            return 0;
        }
        return r;
    }
    if (is_test_unop(a) && b) {
        t->pos += 2;
        return test_unary(a, b);
    }
    t->pos++;
    return *a != 0;
}

static int t_and(TState *t)
{
    int r = t_prim(t);
    while (!t->err && t_peek(t, 0) && strcmp(t_peek(t, 0), "-a") == 0) {
        int r2;
        t->pos++;
        r2 = t_prim(t);
        r = r && r2;
    }
    return r;
}

static int t_or(TState *t)
{
    int r = t_and(t);
    while (!t->err && t_peek(t, 0) && strcmp(t_peek(t, 0), "-o") == 0) {
        int r2;
        t->pos++;
        r2 = t_and(t);
        r = r || r2;
    }
    return r;
}

static int test_n(int argc, char **argv);

static int test_n(int argc, char **argv)
{
    int r;
    switch (argc) {
    case 0:
        return 1;
    case 1:
        return argv[0][0] ? 0 : 1;
    case 2:
        if (strcmp(argv[0], "!") == 0) return test_n(1, argv + 1) == 0 ? 1 : 0;
        if (argv[0][0] == '-' && argv[0][1]) {
            r = test_unary(argv[0], argv[1]);
            if (r < 0) {
                sh_error("%s: %s: unary operator expected", test_cmdname, argv[0]);
                return 2;
            }
            return !r;
        }
        sh_error("%s: %s: unary operator expected", test_cmdname, argv[0]);
        return 2;
    case 3:
        if (is_test_binop(argv[1])) {
            r = test_binary(argv[0], argv[1], argv[2]);
            if (r < 0) return 2;
            return !r;
        }
        if (strcmp(argv[1], "-a") == 0) return !(argv[0][0] && argv[2][0]);
        if (strcmp(argv[1], "-o") == 0) return !(argv[0][0] || argv[2][0]);
        if (strcmp(argv[0], "!") == 0) {
            r = test_n(2, argv + 1);
            return r == 2 ? 2 : !r;
        }
        if (strcmp(argv[0], "(") == 0 && strcmp(argv[2], ")") == 0) return test_n(1, argv + 1);
        sh_error("%s: %s: binary operator expected", test_cmdname, argv[1]);
        return 2;
    case 4:
        if (strcmp(argv[0], "!") == 0) {
            r = test_n(3, argv + 1);
            return r == 2 ? 2 : !r;
        }
        if (strcmp(argv[0], "(") == 0 && strcmp(argv[3], ")") == 0) return test_n(2, argv + 1);
        /* fall through */
    default: {
        TState t;
        t.argc = argc;
        t.argv = argv;
        t.pos = 0;
        t.err = 0;
        r = t_or(&t);
        if (!t.err && t.pos < argc) {
            sh_error("%s: too many arguments", test_cmdname);
            return 2;
        }
        if (t.err) return 2;
        return !r;
    }
    }
}

int test_eval(int argc, char **argv)
{
    return test_n(argc, argv);
}

/* ---- [[ ]] ---- */

static int cond_arith(const char *s, ShInt *v)
{
    return arith_expand_eval(s, v) == 0;
}

static void cond_trace(const char *a, const char *op, const char *b)
{
    SBuf t;
    if (!sh.opt_xtrace) return;
    sb_init(&t);
    sb_adds(&t, "[[ ");
    if (a) sb_adds(&t, a);
    if (op) {
        if (a) sb_addc(&t, ' ');
        sb_adds(&t, op);
    }
    if (b) {
        sb_addc(&t, ' ');
        sb_adds(&t, b);
    }
    sb_adds(&t, " ]]");
    xtrace_line(t.s, NULL, 0, NULL);
    sb_free(&t);
}

int cond_eval(CondNode *c)
{
    int r = 1;
    char *a = NULL, *b = NULL;
    switch (c->type) {
    case C_AND: {
        int l = cond_eval(c->l);
        if (l != 0) return l;
        return cond_eval(c->r);
    }
    case C_OR: {
        int l = cond_eval(c->l);
        if (l == 0) return 0;
        return cond_eval(c->r);
    }
    case C_NOT: {
        int l = cond_eval(c->l);
        return l == 2 ? 2 : !l;
    }
    case C_WORD:
        a = expand_str(c->w1, 0);
        if (!a) return 2;
        cond_trace(a, NULL, NULL);
        r = *a ? 0 : 1;
        break;
    case C_UNARY: {
        int t;
        a = expand_str(c->w1, 0);
        if (!a) return 2;
        cond_trace(NULL, c->op, a);
        t = test_unary(c->op, a);
        r = t < 0 ? 2 : !t;
        break;
    }
    case C_BINARY: {
        const char *op = c->op;
        a = expand_str(c->w1, 0);
        if (!a) return 2;
        if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "!=") == 0) {
            int m;
            b = expand_str(c->w2, X_PATTERN);
            if (!b) {
                r = 2;
                break;
            }
            cond_trace(a, op, b);
            {
                /* extended patterns are always recognised in [[ ]] */
                int save = sh.sh_extglob;
                sh.sh_extglob = 1;
                m = pat_match(b, a, sh.sh_nocasematch ? PM_NOCASE : 0);
                sh.sh_extglob = save;
            }
            r = op[0] == '!' ? m : !m;
        } else if (strcmp(op, "=~") == 0) {
            Regex *re;
            char *err = NULL;
            ReMatch m[10];
            b = expand_str(c->w2, X_REGEX);
            if (!b) {
                r = 2;
                break;
            }
            cond_trace(a, op, b);
            re = re_compile(b, sh.sh_nocasematch ? RE_ICASE : 0, &err);
            if (!re) {
                /* bash 5.3 says so */
                sh_error("[[: invalid regular expression `%s': %s", b, err ? err : "Invalid regular expression");
                xfree(err);
                r = 2;
                break;
            }
            if (re_exec(re, a, 10, m, 0)) {
                int i, n = re_nsub(re);
                Var *v = var_make_array("BASH_REMATCH", 0);
                var_arr_clear(v);
                for (i = 0; i <= n && i < 10; i++) {
                    char *k = str_itoa(i);
                    char *s = m[i].so >= 0 ? xstrndup(a + m[i].so, m[i].eo - m[i].so) : xstrdup("");
                    var_arr_set("BASH_REMATCH", k, s);
                    xfree(k);
                    xfree(s);
                }
                r = 0;
            } else {
                Var *v = var_make_array("BASH_REMATCH", 0);
                var_arr_clear(v);
                r = 1;
            }
            re_free(re);
        } else if (op[0] == '-' && strcmp(op, "-nt") != 0 && strcmp(op, "-ot") != 0 && strcmp(op, "-ef") != 0) {
            ShInt x, y;
            b = expand_str(c->w2, 0);
            if (b) cond_trace(a, op, b);
            if (!b || !cond_arith(a, &x) || !cond_arith(b, &y)) {
                r = 2;
                break;
            }
            if (strcmp(op, "-eq") == 0) r = !(x == y);
            else if (strcmp(op, "-ne") == 0) r = !(x != y);
            else if (strcmp(op, "-lt") == 0) r = !(x < y);
            else if (strcmp(op, "-le") == 0) r = !(x <= y);
            else if (strcmp(op, "-gt") == 0) r = !(x > y);
            else if (strcmp(op, "-ge") == 0) r = !(x >= y);
            else r = 2;
        } else {
            int t;
            b = expand_str(c->w2, 0);
            if (!b) {
                r = 2;
                break;
            }
            cond_trace(a, op, b);
            t = test_binary(a, op, b);
            r = t < 0 ? 2 : !t;
        }
        break;
    }
    }
    xfree(a);
    xfree(b);
    return r;
}
