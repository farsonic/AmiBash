/*
 * driver.c - standalone test driver for the AmiBash built-in utilities.
 * Usage: driver UTIL [ARGS...]
 * Provides minimal stand-ins for the shell parts the utilities need
 * (variables, exec_argv, pattern matching and - unless src/regex.c is
 * linked - regular expressions via the host's POSIX regex).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "sh.h"

Shell sh;
extern char **environ;

/* ---- variables ---- */
static Var *vars;

Var *var_lookup(const char *name)
{
    Var *v;
    for (v = vars; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

const char *var_get(const char *name)
{
    Var *v = var_lookup(name);
    return v && !(v->flags & V_UNSET) ? v->val : NULL;
}

int var_set(const char *name, const char *val, int flags)
{
    Var *v = var_lookup(name);
    if (v && (v->flags & V_READONLY)) {
        sh_error("%s: readonly variable", name);
        return 1;
    }
    if (!v) {
        v = xcalloc(sizeof(Var));
        v->name = xstrdup(name);
        v->next = vars;
        vars = v;
    }
    xfree(v->val);
    v->val = xstrdup(val);
    v->flags = (v->flags | flags) & ~V_UNSET;
    if (v->flags & V_EXPORT) setenv(name, val, 1);
    return 0;
}

int var_unset(const char *name)
{
    Var **pp, *v;
    for (pp = &vars; (v = *pp) != NULL; pp = &v->next) {
        if (strcmp(v->name, name) == 0) {
            *pp = v->next;
            unsetenv(name);
            xfree(v->name);
            xfree(v->val);
            xfree(v);
            return 0;
        }
    }
    return 0;
}

void var_names(StrList *out, const char *prefix)
{
    Var *v;
    for (v = vars; v; v = v->next)
        if (str_startswith(v->name, prefix)) sl_adddup(out, v->name);
    sl_sort(out, 0);
}

/* ---- patterns ---- */
int pat_match(const char *pat, const char *str, int flags)
{
    int f = 0;
    if (flags & PM_NOCASE) f |= FNM_CASEFOLD;
    if (!(flags & PM_DOTGLOB)) f |= FNM_PERIOD;
    return fnmatch(pat, str, f) == 0;
}

/* ---- exec ---- */
int exec_argv(int argc, char **argv)
{
    int i;
    const Builtin *b;
    if (argc == 0) return 0;
    if (strcmp(argv[0], "echo") == 0) {
        for (i = 1; i < argc; i++) {
            if (i > 1) sh_putc(1, ' ');
            sh_puts(1, argv[i]);
        }
        sh_putc(1, '\n');
        return 0;
    }
    if (strcmp(argv[0], "true") == 0) return 0;
    if (strcmp(argv[0], "false") == 0) return 1;
    if (strcmp(argv[0], "exit") == 0) return argc > 1 ? atoi(argv[1]) : 0;
    for (b = util_builtins; b->name; b++)
        if (strcmp(b->name, argv[0]) == 0) return b->fn(argc, argv);
    sh_cmd_error(argv[0], "command not found");
    return 127;
}

int main(int argc, char **argv)
{
    const Builtin *b;
    int st;
    char **e;
    os_init();
    io_init();
    for (e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (eq) {
            char *n = xstrndup(*e, eq - *e);
            Var *v = xcalloc(sizeof(Var));
            v->name = n;
            v->val = xstrdup(eq + 1);
            v->flags = V_EXPORT;
            v->next = vars;
            vars = v;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "usage: driver UTIL [ARGS]\n");
        return 2;
    }
    for (b = util_builtins; b->name; b++) {
        if (strcmp(b->name, argv[1]) == 0) {
            st = b->fn(argc - 1, argv + 1);
            sh_flush_all();
            return st;
        }
    }
    fprintf(stderr, "driver: unknown utility %s\n", argv[1]);
    return 127;
}
