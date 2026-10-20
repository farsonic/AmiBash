/*
 * exec.c - command execution.
 *
 * AmigaOS has no fork(), so:
 *  - subshells ( ... ), $( ... ) and pipeline stages run inside this
 *    process; variable changes are undone afterwards using the journal
 *    in vars.c, and cwd/options/positional parameters are restored;
 *  - pipelines run their stages one after another, each stage's output
 *    collected in a memory file that becomes the next stage's input
 *    (spilled to a temp file in T: when an external program needs a
 *    real file handle).
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

Arena *exec_arena;
extern int keep_redirs;
static int exec_depth;

static int exec_list_node(Node *n);

/* ------------------------------------------------------------------ */
/* helpers                                                              */

void sh_set_status(int st)
{
    sh.laststatus = st;
}

/* PIPESTATUS is materialised lazily (see pipestatus_sync) because it
 * changes after every command */
static int ps_vals[64], ps_n, ps_dirty;

static void set_pipestatus(int *st, int n)
{
    if (n > 64) n = 64;
    memcpy(ps_vals, st, n * sizeof(int));
    ps_n = n;
    ps_dirty = 1;
}

void pipestatus_sync(void)
{
    int i;
    Var *v;
    if (!ps_dirty) return;
    ps_dirty = 0;
    v = var_lookup_raw("PIPESTATUS");
    if (v && (v->flags & V_READONLY)) return;
    if (!v || !(v->flags & V_ARRAY)) v = var_make_array("PIPESTATUS", 0);
    var_arr_clear(v);
    for (i = 0; i < ps_n; i++) {
        char *k = str_itoa(i), *val = str_itoa(ps_vals[i]);
        var_arr_set("PIPESTATUS", k, val);
        xfree(k);
        xfree(val);
    }
}

int check_interrupt(void)
{
    if (sh.flow == FLOW_INTR) return 1;
    os_check_break();
    if (!os_break_flag) return 0;
    os_break_flag = 0;
    if (sh.traps[TRAP_INT].action) {
        run_trap(TRAP_INT);
        return sh.flow != FLOW_NONE;
    }
    sh_flush_all();
    if (sh.interactive && sh.subshell == 0 && sh.sourcedepth == 0) {
        sh_puts(2, "^C\n");
        sh.flow = FLOW_INTR;
    } else if (sh.interactive) {
        sh.flow = FLOW_INTR;
    } else {
        sh_puts(2, "\n");
        sh.flow = FLOW_EXIT;
        sh.exitstatus = 130;
    }
    sh.laststatus = 130;
    return 1;
}

int run_trap(int sig)
{
    char *act;
    int save_status = sh.laststatus, save_flow = sh.flow;
    if (!sh.traps[sig].action || sh.in_trap) return 0;
    if (!*sh.traps[sig].action) return 0;   /* ignored */
    act = xstrdup(sh.traps[sig].action);
    sh.in_trap++;
    sh.flow = FLOW_NONE;
    exec_string(act, "trap");
    sh.in_trap--;
    xfree(act);
    if (sh.flow == FLOW_NONE) sh.flow = save_flow;
    sh.laststatus = save_status;
    return 1;
}

void run_exit_trap(void)
{
    if (sh.traps[TRAP_EXIT].action) {
        char *act = sh.traps[TRAP_EXIT].action;
        sh.traps[TRAP_EXIT].action = NULL;
        sh.flow = FLOW_NONE;
        exec_string(act, "trap");
        xfree(act);
    }
}

/* an expansion failed: ${x?} exits a non-interactive shell, other
 * errors abandon the current top-level command */
void expansion_failed(int e)
{
    if (sh.flow != FLOW_NONE) return;
    if (e == 2 && !sh.interactive) {
        sh.flow = FLOW_EXIT;
        sh.exitstatus = 1;
    } else {
        sh.flow = FLOW_ABORT;
        sh.exitstatus = 1;
    }
    sh.laststatus = 1;
}

static void errexit_check(int status)
{
    if (status == 0 || sh.errexit_suppressed || sh.flow != FLOW_NONE) return;
    /* the ERR trap is not inherited by functions unless set -E */
    if (sh.traps[TRAP_ERR].action && !sh.in_trap && (sh.funcdepth == 0 || sh.opt_errtrace))
        run_trap(TRAP_ERR);
    if (sh.opt_errexit) {
        sh.flow = FLOW_EXIT;
        sh.exitstatus = status;
    }
}

static void xtrace_word(SBuf *b, const char *a)
{
    const char *p;
    int q = !*a;
    for (p = a; *p; p++)
        if (!(isalnum((unsigned char)*p) || strchr("_./:@%^+=,-", *p))) q = 1;
    if (q) {
        char *qs = shell_quote(a);
        sb_adds(b, qs);
        xfree(qs);
    } else {
        sb_adds(b, a);
    }
}

/* write one "set -x" line: PS4, prefix text, then words (quoted) */
void xtrace_line(const char *prefix, char **argv, int argc, const char *suffix)
{
    SBuf b;
    char *ps4;
    int i;
    const char *p4 = var_get("PS4");
    sb_init(&b);
    ps4 = expand_str(p4 ? p4 : "+ ", 0);
    sb_adds(&b, ps4 ? ps4 : "+ ");
    xfree(ps4);
    if (prefix) sb_adds(&b, prefix);
    for (i = 0; i < argc; i++) {
        if (i || (prefix && *prefix)) sb_addc(&b, ' ');
        xtrace_word(&b, argv[i]);
    }
    if (suffix) sb_adds(&b, suffix);
    sb_addc(&b, '\n');
    sh_write(2, b.s, b.len);
    shf_flush(sh_fd(2));
    sb_free(&b);
}

static void xtrace(char **argv, int argc, Word *assigns)
{
    for (; assigns; assigns = assigns->next) {
        const char *eq = strchr(assigns->text, '=');
        if (eq && eq[1] == '(') {
            xtrace_line(assigns->text, NULL, 0, NULL);
        } else {
            char *v = expand_str(eq ? eq + 1 : "", X_ASSIGN);
            SBuf b;
            sb_init(&b);
            sb_addn(&b, assigns->text, eq ? (int)(eq - assigns->text) + 1 : 0);
            xtrace_word(&b, v ? v : "");
            if (b.len > 1 && b.s[b.len - 1] == '\'' && b.s[b.len - 2] == '\'' && v && !*v)
                b.len -= 2;          /* x= rather than x='' */
            xtrace_line(sb_str(&b), NULL, 0, NULL);
            sb_free(&b);
            xfree(v);
        }
    }
    if (argc) xtrace_line(NULL, argv, argc, NULL);
}

/* ------------------------------------------------------------------ */
/* redirections                                                         */

static ShFile *special_file(const char *path, int mode)
{
    if (strcmp(path, "/dev/null") == 0) return shf_null();
    if (strcmp(path, "/dev/stdin") == 0 || strcmp(path, "/dev/fd/0") == 0) return shf_ref(sh_fd(0));
    if (strcmp(path, "/dev/stdout") == 0 || strcmp(path, "/dev/fd/1") == 0) return shf_ref(sh_fd(1));
    if (strcmp(path, "/dev/stderr") == 0 || strcmp(path, "/dev/fd/2") == 0) return shf_ref(sh_fd(2));
    if (strncmp(path, "/dev/fd/", 8) == 0 && isdigit((unsigned char)path[8]) && !path[9])
        return shf_ref(sh_fd(path[8] - '0'));
    (void)mode;
    return NULL;
}

static ShFile *open_redir_file(const char *path, int mode)
{
    ShFile *f = special_file(path, mode);
    if (f) return f;
    if (strcmp(path, "/dev/tty") == 0) {
#ifdef AMIGA
        path = "*";
#endif
    }
    f = shf_open(path, mode);
    if (!f) sh_error("%s: %s", path, os_errstr());
    return f;
}

static char *redir_target(Redir *r)
{
    StrList l;
    char *t;
    sl_init(&l);
    if (expand_word(r->target, &l, 0) || expand_error) {
        sl_free(&l);
        return NULL;
    }
    if (l.n != 1) {
        sh_error("%s: ambiguous redirect", r->target);
        sl_free(&l);
        return NULL;
    }
    t = l.v[0];
    l.v[0] = NULL;
    sl_free(&l);
    return t;
}

int apply_redirs(Redir *r, FdSave *save)
{
    fd_save(save);
    for (; r; r = r->next) {
        int fd = r->fd;
        char *t;
        ShFile *f;
        if (r->varfd) {
            /* {name}>... : close uses the number stored in name, otherwise
             * a free descriptor is chosen (from the top: 9..3) */
            if ((r->op == R_DUPIN || r->op == R_DUPOUT) && strcmp(r->target, "-") == 0) {
                const char *v = var_get(r->varfd);
                ShInt n;
                if (!v || !str_toint(v, &n) || n < 0 || n >= SH_NFD) {
                    sh_error("%s: ambiguous redirect", r->varfd);
                    return 1;
                }
                fd = (int)n;
            } else {
                for (fd = SH_NFD - 1; fd >= 3 && sh_fd(fd); fd--)
                    ;
                if (fd < 3) {
                    sh_error("%s: cannot allocate file descriptor", r->varfd);
                    return 1;
                }
                if (var_set_int(r->varfd, fd)) return 1;
            }
        }
        switch (r->op) {
        case R_HEREDOC: {
            char *body;
            if (fd < 0) fd = 0;
            if (r->quoted) {
                body = xstrdup(r->heredoc ? r->heredoc : "");
            } else {
                body = expand_str(r->heredoc ? r->heredoc : "", X_HEREDOC);
                if (!body) return 1;
            }
            f = shf_mem();
            shf_write(f, body, (long)strlen(body));
            xfree(body);
            fd_assign(fd, f);
            break;
        }
        case R_HERESTR: {
            char *s = expand_str(r->target, 0);
            if (!s) return 1;
            if (fd < 0) fd = 0;
            f = shf_mem();
            shf_write(f, s, (long)strlen(s));
            shf_write(f, "\n", 1);
            xfree(s);
            fd_assign(fd, f);
            break;
        }
        case R_DUPIN:
        case R_DUPOUT: {
            if (fd < 0) fd = r->op == R_DUPIN ? 0 : 1;
            t = redir_target(r);
            if (!t) return 1;
            if (strcmp(t, "-") == 0) {
                fd_assign(fd, NULL);
            } else if (isdigit((unsigned char)t[0]) && (!t[1] || (t[1] == '-' && !t[2]))) {
                int src = t[0] - '0';
                ShFile *sf = sh_fd(src);
                if (!sf) {
                    sh_error("%c: Bad file descriptor", t[0]);
                    xfree(t);
                    return 1;
                }
                fd_assign(fd, shf_ref(sf));
                if (t[1] == '-' && src != fd) fd_assign(src, NULL);
            } else if (r->op == R_DUPOUT && fd == 1) {
                /* >& file : stdout and stderr to file */
                f = open_redir_file(t, OS_WR);
                if (!f) {
                    xfree(t);
                    return 1;
                }
                fd_assign(1, f);
                fd_assign(2, shf_ref(f));
            } else {
                sh_error("%s: ambiguous redirect", t);
                xfree(t);
                return 1;
            }
            xfree(t);
            break;
        }
        default: {
            int mode;
            t = redir_target(r);
            if (!t) return 1;
            switch (r->op) {
            case R_IN: mode = OS_RD; if (fd < 0) fd = 0; break;
            case R_APPEND: mode = OS_APP; if (fd < 0) fd = 1; break;
            case R_RDWR: mode = OS_RW; if (fd < 0) fd = 0; break;
            case R_OUTERR: mode = OS_WR; fd = 1; break;
            case R_APPERR: mode = OS_APP; fd = 1; break;
            default: mode = OS_WR; if (fd < 0) fd = 1; break;
            }
            if (r->op == R_OUT && sh.opt_noclobber && !special_file(t, mode) && sh_exists(t) && !sh_is_dir(t)) {
                OsDirEnt e;
                if (sh_stat(t, &e) == 0 && e.type == OS_T_FILE) {
                    sh_error("%s: cannot overwrite existing file", t);
                    xfree(t);
                    return 1;
                }
            }
            f = open_redir_file(t, mode);
            xfree(t);
            if (!f) return 1;
            fd_assign(fd, f);
            if (r->op == R_OUTERR || r->op == R_APPERR) fd_assign(2, shf_ref(f));
        }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* command lookup                                                       */

typedef struct HashEnt { char *name, *path; int hits; struct HashEnt *next; } HashEnt;
static HashEnt *hashtab;

void hash_clear(void)
{
    while (hashtab) {
        HashEnt *h = hashtab;
        hashtab = h->next;
        xfree(h->name);
        xfree(h->path);
        xfree(h);
    }
}

void hash_list(int fd)
{
    HashEnt *h;
    if (!hashtab) {
        sh_puts(fd, "hash: hash table empty\n");
        return;
    }
    sh_puts(fd, "hits\tcommand\n");
    for (h = hashtab; h; h = h->next) sh_printf(fd, "%4d\t%s\n", h->hits, h->path);
}

static void path_dirs(StrList *out)
{
    const char *path = var_get("PATH");
    const char *s, *e;
    char sep = ':';
    if (!path) path = os_default_path();
#ifdef AMIGA
    if (strchr(path, ';')) sep = ';';
#endif
    for (s = path;; s = e + 1) {
        e = strchr(s, sep);
        if (!e) e = s + strlen(s);
        if (e == s) sl_adddup(out, ".");
        else sl_add(out, xstrndup(s, e - s));
        if (!*e) break;
    }
}

/* all executables called name in PATH (for type -a); no hashing */
int find_command_all(const char *name, StrList *out)
{
    StrList dirs;
    int i, n0 = out->n;
    if (!*name) return 0;
    if (path_has_dir(name)) {
        char *np = path_native(name);
        if (os_is_executable(np)) sl_add(out, np);
        else xfree(np);
        return out->n - n0;
    }
#ifdef AMIGA
    if (os_is_resident(name)) sl_adddup(out, name);
#endif
    sl_init(&dirs);
    path_dirs(&dirs);
    for (i = 0; i < dirs.n; i++) {
        char *cand = path_join(dirs.v[i], name);
        char *np = path_native(cand);
        int k, dup = 0;
        xfree(cand);
        for (k = n0; k < out->n; k++)
            if (strcmp(out->v[k], np) == 0) dup = 1;
        if (!dup && os_is_executable(np)) sl_add(out, np);
        else xfree(np);
    }
    sl_free(&dirs);
    return out->n - n0;
}

char *find_command(const char *name)
{
    HashEnt *h;
    StrList dirs;
    int i;
    char *found = NULL;
    if (!*name) return NULL;
    if (path_has_dir(name)) {
        char *np = path_native(name);
        if (os_is_executable(np)) return np;
        xfree(np);
        return NULL;
    }
    for (h = hashtab; h; h = h->next) {
        if (strcmp(h->name, name) == 0) {
            if (os_is_resident(h->path) || os_is_executable(h->path)) {
                h->hits++;
                return xstrdup(h->path);
            }
            break;
        }
    }
#ifdef AMIGA
    if (os_is_resident(name)) {
        found = xstrdup(name);
    }
#endif
    sl_init(&dirs);
    if (!found) path_dirs(&dirs);
    for (i = 0; i < dirs.n && !found; i++) {
        char *cand = path_join(dirs.v[i], name);
        char *np = path_native(cand);
        xfree(cand);
        if (os_is_executable(np)) found = np;
        else xfree(np);
    }
    sl_free(&dirs);
    if (found) {
        for (h = hashtab; h; h = h->next) {
            if (strcmp(h->name, name) == 0) {
                xfree(h->path);
                h->path = xstrdup(found);
                h->hits = 1;
                return found;
            }
        }
        h = xmalloc(sizeof(HashEnt));
        h->name = xstrdup(name);
        h->path = xstrdup(found);
        h->hits = 1;
        h->next = hashtab;
        hashtab = h;
    }
    return found;
}

static const Builtin *bi_find(const Builtin *tab, const char *name)
{
    for (; tab->name; tab++)
        if (strcmp(tab->name, name) == 0) return tab;
    return NULL;
}

static StrList disabled;

const Builtin *builtin_lookup(const char *name)
{
    const Builtin *b = bi_find(shell_builtins, name);
    int i;
    if (!b) b = bi_find(util_builtins, name);
    if (!b) return NULL;
    for (i = 0; i < disabled.n; i++)
        if (strcmp(disabled.v[i], name) == 0) return NULL;
    return b;
}

void builtin_set_enabled(const char *name, int on)
{
    int i;
    for (i = 0; i < disabled.n; i++) {
        if (strcmp(disabled.v[i], name) == 0) {
            if (on) {
                xfree(disabled.v[i]);
                disabled.v[i] = disabled.v[--disabled.n];
                disabled.v[disabled.n] = NULL;
            }
            return;
        }
    }
    if (!on) sl_adddup(&disabled, name);
}

void builtin_names(StrList *out, const char *prefix)
{
    const Builtin *t;
    size_t pl = prefix ? strlen(prefix) : 0;
    for (t = shell_builtins; t->name; t++)
        if (!pl || strncmp(t->name, prefix, pl) == 0)
            if (builtin_lookup(t->name)) sl_adddup(out, t->name);
    for (t = util_builtins; t->name; t++)
        if (!pl || strncmp(t->name, prefix, pl) == 0)
            if (builtin_lookup(t->name)) sl_adddup(out, t->name);
}

/* ------------------------------------------------------------------ */
/* subshell emulation                                                   */

typedef struct SubState {
    Shell saved;
    int mark, fmark;
    Params params;
    char *pwd;
    StrList dirs;
} SubState;

static void sub_enter(SubState *st)
{
    int i;
    st->saved = sh;
    st->mark = var_journal_mark();
    st->fmark = func_journal_mark();
    params_copy(&st->params, &sh_params);
    st->pwd = xstrdup(sh.pwd ? sh.pwd : "");
    sl_init(&st->dirs);
    for (i = 0; i < sh.dirstack.n; i++) sl_adddup(&st->dirs, sh.dirstack.v[i]);
    sh.subshell++;
    /* traps are not inherited by subshells (except ignored ones) */
    for (i = 0; i < NSIGS; i++) {
        if (sh.traps[i].action && *sh.traps[i].action) {
            sh.ptraps[i].action = sh.traps[i].action;
            sh.traps[i].action = NULL;
        }
    }
}

static int sub_leave(SubState *st, int status)
{
    int i;
    char *curpwd = sh.pwd;
    if (sh.flow == FLOW_EXIT || sh.flow == FLOW_ABORT) status = sh.exitstatus;
    if (sh.flow == FLOW_INTR) {
        st->saved.flow = FLOW_INTR;
    }
    /* run EXIT trap set inside the subshell */
    if (sh.traps[TRAP_EXIT].action && sh.traps[TRAP_EXIT].action != st->saved.traps[TRAP_EXIT].action) {
        sh.flow = FLOW_NONE;
        exec_string(sh.traps[TRAP_EXIT].action, "trap");
    }
    var_rollback(st->mark);
    func_rollback(st->fmark);
    params_free(&sh_params);
    sh_params = st->params;
    if (curpwd && strcmp(curpwd, st->pwd) != 0) {
        sh_chdir(st->pwd, 1);
        curpwd = sh.pwd;
    }
    /* restore dir stack */
    for (i = 0; i < sh.dirstack.n; i++) xfree(sh.dirstack.v[i]);
    xfree(sh.dirstack.v);
    {
        int flow = st->saved.flow;
        sh = st->saved;
        sh.flow = flow;
    }
    sh.dirstack = st->dirs;
    sh.pwd = curpwd;
    xfree(st->pwd);
    sh.laststatus = status;
    return status;
}

int exec_subshell_node(Node *n)
{
    SubState st;
    int status;
    sub_enter(&st);
    status = exec_node(n);
    return sub_leave(&st, status);
}

int exec_subshell_string(const char *src)
{
    SubState st;
    int status;
    sub_enter(&st);
    status = exec_string(src, NULL);
    return sub_leave(&st, status);
}

/* ------------------------------------------------------------------ */
/* functions                                                            */

/* push/pop at the front of a call-stack array (BASH_SOURCE) */
static void stack_push(const char *name, const char *val)
{
    Var *v = var_lookup(name);
    StrList old;
    int i;
    sl_init(&old);
    if (v && (v->flags & V_ARRAY)) var_arr_values(v, &old, 0);
    v = var_make_array(name, 0);
    var_arr_clear(v);
    var_arr_set(name, "0", val);
    for (i = 0; i < old.n; i++) {
        char *k = str_itoa(i + 1);
        var_arr_set(name, k, old.v[i]);
        xfree(k);
    }
    sl_free(&old);
}

static void stack_pop(const char *name)
{
    Var *v = var_lookup(name);
    StrList old;
    int i;
    if (!v) return;
    sl_init(&old);
    var_arr_values(v, &old, 0);
    var_arr_clear(v);
    for (i = 1; i < old.n; i++) {
        char *k = str_itoa(i - 1);
        var_arr_set(name, k, old.v[i]);
        xfree(k);
    }
    if (old.n <= 1) var_unset(name);
    sl_free(&old);
}

/* "local -": shell options restored when the function returns */
static const int opt_offs[] = {
    SHOFF(opt_errexit), SHOFF(opt_nounset), SHOFF(opt_xtrace), SHOFF(opt_verbose),
    SHOFF(opt_noglob), SHOFF(opt_noclobber), SHOFF(opt_allexport), SHOFF(opt_pipefail),
    SHOFF(opt_noexec), SHOFF(opt_braceexpand), SHOFF(opt_posix), SHOFF(opt_functrace)
};
#define NOPTS ((int)(sizeof(opt_offs) / sizeof(opt_offs[0])))
typedef struct OptSnap { int used; int v[NOPTS]; } OptSnap;
static OptSnap *cur_optsnap;

int func_local_options(void)
{
    int i;
    if (!cur_optsnap) return 1;
    if (!cur_optsnap->used)
        for (i = 0; i < NOPTS; i++) cur_optsnap->v[i] = *SHOPT(opt_offs[i]);
    cur_optsnap->used = 1;
    return 0;
}

/* call stack for "caller": line of each call and its script */
static int *call_lines;
static const char **call_files;
static int call_cap;

int caller_frame(int n, int *line, const char **file)
{
    int d = sh.funcdepth - 1 - n;
    if (n < 0 || d < 0 || d >= call_cap) return 0;
    *line = call_lines[d];
    *file = call_files[d];
    return 1;
}

int run_function(Func *f, int argc, char **argv)
{
    OptSnap snap, *save_snap;
    Params saved;
    Arena *save_arena = exec_arena;
    int status, i, save_lineno;
    Var *fn;
    char *k;
    if (sh.funcdepth > 1000 || !os_stack_ok()) {
        sh_error("%s: maximum function nesting level exceeded (%d)", argv[0], sh.funcdepth);
        return 1;
    }
    saved = sh_params;
    sh_params.v = NULL;
    sh_params.n = 0;
    params_set(argv + 1, argc - 1);
    /* FUNCNAME stack */
    fn = var_lookup("FUNCNAME");
    if (!fn || !(fn->flags & V_ARRAY)) fn = var_make_array("FUNCNAME", 0);
    {
        StrList old;
        sl_init(&old);
        if (sh.funcdepth > 0) var_arr_values(fn, &old, 0);
        else if (sh.script && !sh.interactive) sl_adddup(&old, "main");
        var_arr_clear(fn);
        var_arr_set("FUNCNAME", "0", argv[0]);
        for (i = 0; i < old.n; i++) {
            k = str_itoa(i + 1);
            var_arr_set("FUNCNAME", k, old.v[i]);
            xfree(k);
        }
        sl_free(&old);
    }
    var_push_scope();
    f->refs++;
    if (sh.funcdepth >= call_cap) {
        call_cap = call_cap ? call_cap * 2 : 16;
        call_lines = xrealloc(call_lines, call_cap * sizeof(int));
        call_files = xrealloc(call_files, call_cap * sizeof(char *));
    }
    call_lines[sh.funcdepth] = sh.lineno;
    call_files[sh.funcdepth] = sh.script ? sh.script : "main";
    stack_push("BASH_SOURCE", sh.script ? sh.script : "main");
    sh.funcdepth++;
    exec_arena = f->arena;
    save_lineno = sh.lineno;
    snap.used = 0;
    save_snap = cur_optsnap;
    cur_optsnap = &snap;
    status = exec_node(f->body);
    cur_optsnap = save_snap;
    if (snap.used) {
        for (i = 0; i < NOPTS; i++) *SHOPT(opt_offs[i]) = snap.v[i];
    }
    sh.lineno = save_lineno;        /* $LINENO is the caller's line again */
    exec_arena = save_arena;
    sh.funcdepth--;
    f->refs--;
    if (sh.flow == FLOW_RETURN) {
        sh.flow = FLOW_NONE;
        status = sh.exitstatus;
    }
    if (sh.traps[TRAP_RETURN].action && !sh.in_trap) run_trap(TRAP_RETURN);
    var_pop_scope();
    stack_pop("BASH_SOURCE");
    /* pop FUNCNAME */
    fn = var_lookup("FUNCNAME");
    if (fn) {
        StrList old;
        sl_init(&old);
        var_arr_values(fn, &old, 0);
        var_arr_clear(fn);
        for (i = 1; i < old.n; i++) {
            k = str_itoa(i - 1);
            var_arr_set("FUNCNAME", k, old.v[i]);
            xfree(k);
        }
        if (old.n <= 1 || sh.funcdepth == 0) var_unset("FUNCNAME");
        sl_free(&old);
    }
    params_free(&sh_params);
    sh_params = saved;
    return status;
}

/* ------------------------------------------------------------------ */
/* external commands                                                    */

/* is this file a bash script (#! naming bash/sh, or plain text)? */
static int script_kind(const char *native, char *interp, int isz)
{
    OsHandle h = os_open(native, OS_RD);
    char buf[128];
    long n;
    int i;
    interp[0] = 0;
    if (!h) return 0;
    n = os_read(h, buf, sizeof(buf) - 1);
    os_close(h);
    if (n < 2) return 0;
    buf[n] = 0;
    if (buf[0] == '#' && buf[1] == '!') {
        char *p = buf + 2, *e;
        while (*p == ' ' || *p == '\t') p++;
        e = p;
        while (*e && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') e++;
        i = (int)(e - p);
        if (i >= isz) i = isz - 1;
        memcpy(interp, p, i);
        interp[i] = 0;
        {
            const char *base = path_basename(interp);
            if (strcmp(base, "bash") == 0 || strcmp(base, "sh") == 0 || strcmp(base, "amibash") == 0) return 1;
            if (strcmp(base, "env") == 0) {
                /* #!/usr/bin/env bash */
                char *q = e;
                while (*q == ' ' || *q == '\t') q++;
                if (strncmp(q, "bash", 4) == 0 || strncmp(q, "sh", 2) == 0) return 1;
            }
        }
        return 2;
    }
    return 0;
}

/* run a bash script file as a child "process" (subshell emulation) */
static int run_script_child(const char *path, int argc, char **argv)
{
    SubState st;
    int status;
    char *save_script = sh.script;
    int save_line = sh.lineno, save_inter = sh.interactive;
    char *save_name = sh.name;
    Func *f;
    sub_enter(&st);
    sh.interactive = 0;
    sh.script = (char *)path;
    sh.name = (char *)path;
    sh.sourcedepth = 0;            /* a new top level (restored by sub_leave) */
    status = exec_file(path, argv + 1, argc - 1, 0);
    if (sh.flow == FLOW_EXIT) status = sh.exitstatus;
    (void)f;
    sh.flow = FLOW_NONE;
    status = sub_leave(&st, status);
    sh.script = save_script;
    sh.lineno = save_line;
    sh.interactive = save_inter;
    sh.name = save_name;
    return status;
}

/* "bash [-ceux] [-c cmd | script] args": run as an emulated child
 * process instead of loading a second copy of the shell */
int run_child_shell(int argc, char **argv)
{
    SubState st;
    int i, status = 0;
    const char *cmd = NULL;
    char *save_script = sh.script, *save_name = sh.name;
    int save_line = sh.lineno, save_inter = sh.interactive;
    int inter = 0, force_stdin = 0;
    sub_enter(&st);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *p;
        if (strcmp(a, "--") == 0 || strcmp(a, "-") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "--norc") == 0 || strcmp(a, "--noprofile") == 0 || strcmp(a, "--login") == 0)
            continue;
        if (strcmp(a, "--version") == 0) {
            sh_printf(1, "AmiBash %s (%s build, %s), GNU bash %s compatible\n", AMIBASH_SHORTVER, AMIBASH_BUILD, AMIBASH_DATE, AMIBASH_VERSION);
            return sub_leave(&st, 0);
        }
        if ((a[0] != '-' && a[0] != '+') || !a[1]) break;
        for (p = a + 1; *p; p++) {
            int on = a[0] == '-';
            switch (*p) {
            case 'c': cmd = i + 1 < argc ? argv[++i] : NULL; break;
            case 'e': sh.opt_errexit = on; break;
            case 'u': sh.opt_nounset = on; break;
            case 'x': sh.opt_xtrace = on; break;
            case 'v': sh.opt_verbose = on; break;
            case 'n': sh.opt_noexec = on; break;
            case 'f': sh.opt_noglob = on; break;
            case 'i': inter = 1; break;
            case 's': force_stdin = 1; break;
            case 'l': break;
            default:
                sh_error("-%c: invalid option", *p);
                return sub_leave(&st, 2);
            }
        }
        if (cmd) {
            i++;
            break;
        }
    }
    sh.flow = FLOW_NONE;
    if (cmd) {
        sh.interactive = inter;
        if (i < argc) {
            sh.name = argv[i];
            params_set(argv + i + 1, argc - i - 1);
        } else {
            params_set(NULL, 0);
        }
        status = exec_string(cmd, "-c");
    } else if (i < argc && !force_stdin) {
        sh.interactive = 0;
        sh.name = argv[i];
        sh.script = argv[i];
        if (!sh_exists(argv[i])) {
            sh_error("%s: No such file or directory", argv[i]);
            status = 127;
        } else {
            status = exec_file(argv[i], argv + i + 1, argc - i - 1, 0);
        }
    } else {
        ShFile *in = sh_fd(0);
        params_set(argv + i, argc - i);
        sh.interactive = inter || (in && in->kind == SHF_OS && in->tty);
        status = sh_repl(in, sh.interactive);
    }
    if (sh.flow == FLOW_EXIT) status = sh.exitstatus;
    sh.flow = FLOW_NONE;
    status = sub_leave(&st, status);
    sh.script = save_script;
    sh.name = save_name;
    sh.lineno = save_line;
    sh.interactive = save_inter;
    return status;
}

int exec_external(int argc, char **argv, const char *path)
{
    OsHandle in, out, err;
    ShFile *f0 = sh_fd(0), *f1 = sh_fd(1), *f2 = sh_fd(2);
    int rc;
    char interp[100];
    int kind;
    (void)argc;
    sh_flush_all();
    kind = path_has_dir(path) ? script_kind(path, interp, sizeof(interp)) : 0;
    if (kind == 1) return run_script_child(path, argc, argv);
    in = f0 ? shf_os_handle(f0, 0) : os_open(os_nulldev(), OS_RD);
    out = f1 ? shf_os_handle(f1, 1) : os_open(os_nulldev(), OS_WR);
    if (f2 && f2 == f1) err = out;
    else err = f2 ? shf_os_handle(f2, 1) : os_open(os_nulldev(), OS_WR);
    rc = os_run(path, argv, in, out, err);
    if (f0) shf_os_done(f0, 0);
    else os_close(in);
    if (f1) shf_os_done(f1, 1);
    else os_close(out);
    if (f2 && f2 != f1) shf_os_done(f2, 1);
    else if (!f2) os_close(err);
    if (rc < 0) {
        sh_error("%s: %s", argv[0], os_errstr());
        return 126;
    }
    return rc;
}

/* run argv: function, builtin or external (no assignments/redirs) */
int exec_argv(int argc, char **argv)
{
    Func *f;
    const Builtin *b;
    char *path;
    int st;
    if (argc == 0) return 0;
    if (!path_has_dir(argv[0])) {
        f = func_lookup(argv[0]);
        if (f) return run_function(f, argc, argv);
        b = builtin_lookup(argv[0]);
        if (b) {
            int save = sh_util_running;
            sh_util_running = (b->flags & BI_UTIL) != 0;
            st = b->fn(argc, argv);
            sh_util_running = save;
            return st;
        }
    }
    path = find_command(argv[0]);
    if (!path) {
        if (path_has_dir(argv[0])) {
            char *np = path_native(argv[0]);
            OsDirEnt e;
            if (os_stat(np, &e) == 0) {
                if (e.type == OS_T_DIR) sh_error("%s: Is a directory", argv[0]);
                else {
                    char interp[100];
                    /* not executable, but a bash script? run it anyway like sh does for scripts with r bit */
                    if (script_kind(np, interp, sizeof(interp)) == 1) {
                        st = run_script_child(np, argc, argv);
                        xfree(np);
                        return st;
                    }
                    sh_error("%s: Permission denied", argv[0]);
                }
                xfree(np);
                return 126;
            }
            xfree(np);
            sh_error("%s: No such file or directory", argv[0]);
            return 127;
        }
        sh_error("%s: command not found", argv[0]);
        return 127;
    }
    st = exec_external(argc, argv, path);
    xfree(path);
    return st;
}

/* ------------------------------------------------------------------ */
/* assignments                                                          */

/* parse "name[sub]+=value" ; returns malloc'd name, sets *sub, *append, *val */
static char *split_assign(const char *w, char **sub, int *append, const char **val)
{
    const char *p = w;
    char *name;
    *sub = NULL;
    *append = 0;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    name = xstrndup(w, p - w);
    if (*p == '[') {
        const char *q = p + 1;
        int depth = 1;
        while (*q && depth) {
            if (*q == '[') depth++;
            else if (*q == ']') depth--;
            if (depth) q++;
        }
        *sub = xstrndup(p + 1, q - p - 1);
        p = q + 1;
    }
    if (*p == '+') {
        *append = 1;
        p++;
    }
    *val = p + 1;
    return name;
}

/* split the inside of ( ... ) into raw words */
static void compound_words(const char *s, StrList *out)
{
    while (*s) {
        const char *e;
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        if (*s == '#') {
            while (*s && *s != '\n') s++;
            continue;
        }
        e = scan_word_end(s);
        if (!e || e == s) e = s + 1;
        sl_add(out, xstrndup(s, e - s));
        s = e;
    }
}

int assign_array(const char *name, const char *inner, int append, int assoc_hint)
{
    StrList raw, keys, vals;
    SBuf kinds;               /* per entry: 'k' [k]=v, 'a' [k]+=v, 'v' value */
    Var *v = var_lookup(name);
    int i, assoc, r = 0;
    ShInt next = 0;
    if (v && (v->flags & V_READONLY)) {
        sh_error("%s: readonly variable", name);
        return 1;
    }
    assoc = (v && (v->flags & V_ASSOC)) || assoc_hint;
    /* phase 1: expand everything (the old value may be referenced) */
    sl_init(&raw);
    sl_init(&keys);
    sl_init(&vals);
    sb_init(&kinds);
    compound_words(inner, &raw);
    for (i = 0; i < raw.n && !r; i++) {
        const char *w = raw.v[i];
        if (w[0] == '[') {
            /* [key]=value */
            const char *q = w + 1;
            int depth = 1;
            while (*q && depth) {
                if (*q == '[') depth++;
                else if (*q == ']') depth--;
                if (depth) q++;
            }
            if (*q == ']' && (q[1] == '=' || (q[1] == '+' && q[2] == '='))) {
                char *ks = xstrndup(w + 1, q - w - 1);
                int app = q[1] == '+';
                char *val = expand_str(q + (app ? 3 : 2), X_ASSIGN);
                char *key;
                if (assoc) {
                    key = expand_str(ks, 0);
                } else {
                    ShInt idx;
                    key = NULL;
                    if (arith_expand_eval(ks, &idx)) r = 1;
                    else key = str_itoa(idx);
                }
                xfree(ks);
                if (!key || !val) {
                    xfree(key);
                    xfree(val);
                    r = 1;
                    break;
                }
                sl_add(&keys, key);
                sl_add(&vals, val);
                sb_addc(&kinds, app ? 'a' : 'k');
                continue;
            }
        }
        {
            StrList ev;
            int k;
            sl_init(&ev);
            if (expand_word(w, &ev, 0)) {
                sl_free(&ev);
                r = 1;
                break;
            }
            for (k = 0; k < ev.n; k++) {
                sl_adddup(&keys, "");
                sl_adddup(&vals, ev.v[k]);
                sb_addc(&kinds, 'v');
            }
            sl_free(&ev);
        }
    }
    if (r) goto out;
    /* phase 2: store */
    if (!append || !v || !(v->flags & (V_ARRAY | V_ASSOC))) {
        v = var_make_array(name, assoc);
        if (!append) var_arr_clear(v);
    }
    v->flags &= ~V_UNSET;           /* x=() is set, even if empty */
    if (!assoc && v->alen) next = v->arr[v->alen - 1].idx + 1;
    for (i = 0; i < keys.n; i++) {
        int kind = kinds.s[i];
        if (kind == 'k' || kind == 'a') {
            const char *key = keys.v[i];
            if (kind == 'a') {
                const char *old = var_arr_get(var_lookup(name), key);
                char *nv = str_cat(old ? old : "", vals.v[i]);
                if (var_arr_set(name, key, nv)) r = 1;
                xfree(nv);
            } else if (var_arr_set(name, key, vals.v[i])) {
                r = 1;
            }
            if (!assoc) {
                ShInt idx = 0;
                str_toint(key, &idx);
                next = idx + 1;
            }
        } else if (assoc) {
            /* bash 5.1+: key value pairs */
            if (i + 1 < keys.n && kinds.s[i + 1] == 'v') {
                if (var_arr_set(name, vals.v[i], vals.v[i + 1])) r = 1;
                i++;
            } else if (var_arr_set(name, vals.v[i], "")) {
                r = 1;
            }
        } else {
            char *key = str_itoa(next++);
            if (var_arr_set(name, key, vals.v[i])) r = 1;
            xfree(key);
        }
    }
out:
    sl_free(&raw);
    sl_free(&keys);
    sl_free(&vals);
    sb_free(&kinds);
    return r;
}

/* perform one assignment word; flags are extra var flags (export) */
int do_assignment(const char *w, int flags)
{
    char *sub, *name;
    int append, r = 0;
    const char *val;
    name = split_assign(w, &sub, &append, &val);
    if (val[0] == '(' && !sub) {
        size_t l = strlen(val);
        if (l >= 2 && val[l - 1] == ')') {
            char *inner = xstrndup(val + 1, l - 2);
            r = assign_array(name, inner, append, 0);
            xfree(inner);
            if (flags) {
                Var *v = var_lookup(name);
                if (v) v->flags |= flags;
            }
            xfree(name);
            return r;
        }
    }
    {
        char *v = expand_str(val, X_ASSIGN);
        if (!v) {
            xfree(name);
            xfree(sub);
            return 1;
        }
        if (sub) {
            Var *var = var_lookup(name);
            char *key;
            if (var && (var->flags & V_ASSOC)) {
                key = expand_str(sub, 0);
            } else {
                ShInt idx;
                if (arith_expand_eval(sub, &idx)) {
                    xfree(v);
                    xfree(name);
                    xfree(sub);
                    return 1;
                }
                if (idx < 0 && var && var->alen) idx += var->arr[var->alen - 1].idx + 1;
                key = str_itoa(idx);
            }
            if (append) {
                const char *old = var ? var_arr_get(var, key) : NULL;
                char *nv = str_cat(old ? old : "", v);
                r = var_arr_set(name, key, nv);
                xfree(nv);
            } else {
                r = var_arr_set(name, key, v);
            }
            xfree(key);
        } else if (append) {
            r = var_append(name, v);
        } else {
            r = var_set(name, v, flags);
        }
        if (flags && !r) {
            Var *var = var_lookup(name);
            if (var) {
                var->flags |= flags;
                if (flags & V_EXPORT) os_setenv(name, var_get(name) ? var_get(name) : "");
            }
        }
        xfree(v);
    }
    xfree(name);
    xfree(sub);
    return r;
}

typedef struct TmpVar { char *name; char *val; int flags; int existed; } TmpVar;

/* expand command words; arguments of declaration builtins that look
 * like assignments are expanded like assignments (no split/glob) */
static int is_decl_builtin(const char *w)
{
    return !strcmp(w, "local") || !strcmp(w, "declare") || !strcmp(w, "typeset") ||
           !strcmp(w, "export") || !strcmp(w, "readonly");
}

static int raw_is_assignment(const char *w)
{
    const char *p = w;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return 0;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    if (*p == '[') {
        while (*p && *p != ']') p++;
        if (*p) p++;
    }
    if (*p == '+') p++;
    return *p == '=';
}

static int expand_cmd_words(Word *w, StrList *out)
{
    int decl = 0;
    if (!w) return 0;
    if (expand_word(w->text, out, 0)) return 1;
    if (out->n > 0 && is_decl_builtin(out->v[0]) && !func_lookup(out->v[0])) decl = 1;
    for (w = w->next; w; w = w->next) {
        int fl = 0;
        if (decl && raw_is_assignment(w->text)) {
            const char *eq = strchr(w->text, '=');
            /* keep array literals for the builtin to parse */
            if (eq && eq[1] == '(') {
                sl_adddup(out, w->text);
                continue;
            }
            fl = X_ASSIGN | X_NOSPLIT | X_NOGLOB;
        }
        if (expand_word(w->text, out, fl)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* simple commands                                                      */

static int exec_simple(Node *n)
{
    StrList args;
    Word *w;
    FdSave save;
    int status = 0, i;
    int nassign = 0;
    TmpVar *tmp = NULL;
    int psmark = procsub_mark();
    sl_init(&args);
    expand_error = 0;
    cmdsubst_status = -1;
    if (expand_cmd_words(n->u.simple.words, &args) || expand_error) {
        int e = expand_error;
        sl_free(&args);
        procsub_cleanup_to(psmark);
        expansion_failed(e);
        return 1;
    }
    for (w = n->u.simple.assigns; w; w = w->next) nassign++;

    if (args.n == 0) {
        /* assignments only (persist), redirections opened and closed;
         * the status is that of the last command substitution */
        sh.laststatus = cmdsubst_status >= 0 ? cmdsubst_status : 0;
        status = sh.laststatus;
        for (w = n->u.simple.assigns; w; w = w->next) {
            expand_error = 0;
            if (do_assignment(w->text, 0)) {
                status = 1;
                expansion_failed(expand_error ? expand_error : 1);
                break;
            }
            status = sh.laststatus;     /* status of last $(...) */
        }
        if (sh.opt_xtrace && n->u.simple.assigns) xtrace(NULL, 0, n->u.simple.assigns);
        if (n->redirs) {
            if (apply_redirs(n->redirs, &save)) status = 1;
            fd_restore(&save);
        }
        sl_free(&args);
        procsub_cleanup_to(psmark);
        return status;
    }

    if (sh.opt_xtrace) xtrace(args.v, args.n, n->u.simple.assigns);

    /* temporary assignments */
    if (nassign) {
        tmp = xcalloc(nassign * sizeof(TmpVar));
        i = 0;
        for (w = n->u.simple.assigns; w; w = w->next, i++) {
            char *sub;
            int append;
            const char *val;
            Var *v;
            tmp[i].name = split_assign(w->text, &sub, &append, &val);
            xfree(sub);
            v = var_lookup(tmp[i].name);
            tmp[i].existed = v != NULL && !(v->flags & V_UNSET);
            if (tmp[i].existed) {
                const char *cv = var_get(tmp[i].name);
                tmp[i].val = cv ? xstrdup(cv) : NULL;
                tmp[i].flags = v->flags;
            }
            expand_error = 0;
            if (do_assignment(w->text, V_EXPORT) && expand_error) {
                status = 1;
                expansion_failed(expand_error);
                goto done_tmp;
            }
        }
    }

    sh.lineno = n->lineno > 0 ? n->lineno : sh.lineno;
    if (apply_redirs(n->redirs, &save)) {
        fd_restore(&save);
        status = 1;
        goto done_tmp;
    }
    {
        /* $_ is the last argument of the previous command */
        status = exec_argv(args.n, args.v);
    }
    sh_flush_all();
    if (keep_redirs) {
        /* "exec >file": redirections stay in effect */
        int k;
        keep_redirs = 0;
        for (k = 0; k < SH_NFD; k++) shf_close(save.saved[k]);
    } else {
        fd_restore(&save);
    }
    if (args.n) var_set("_", args.v[args.n - 1], 0);

done_tmp:
    if (tmp) {
        for (i = 0; i < nassign; i++) {
            if (tmp[i].existed) {
                Var *v = var_lookup(tmp[i].name);
                if (v && (v->flags & V_READONLY)) {
                    /* leave it */
                } else {
                    if (v) v->flags &= ~V_EXPORT;
                    var_set(tmp[i].name, tmp[i].val ? tmp[i].val : "", tmp[i].flags & ~V_READONLY);
                    v = var_lookup(tmp[i].name);
                    if (v) {
                        v->flags = tmp[i].flags;
                        if (!(v->flags & V_EXPORT)) os_unsetenv(tmp[i].name);
                    }
                }
            } else {
                var_unset(tmp[i].name);
                os_unsetenv(tmp[i].name);
            }
            xfree(tmp[i].name);
            xfree(tmp[i].val);
        }
        xfree(tmp);
    }
    sl_free(&args);
    procsub_cleanup_to(psmark);
    return status;
}

/* ------------------------------------------------------------------ */
/* compound commands                                                    */

static int exec_pipeline(Node *n)
{
    int i, status = 0;
    int nst = n->u.pipe.n;
    int *st;
    ShFile *prev = NULL;
    long t0 = 0;
    if (n->u.pipe.timed) t0 = os_time_ms();
    if (nst == 0) {
        status = 0;
        goto timed;
    }
    st = xmalloc(nst * sizeof(int));
    if (nst == 1) {
        sh.errexit_suppressed += n->u.pipe.bang;
        status = exec_node(n->u.pipe.cmds[0]);
        sh.errexit_suppressed -= n->u.pipe.bang;
        st[0] = status;
    } else {
        for (i = 0; i < nst; i++) {
            FdSave save;
            ShFile *mem = NULL;
            int last = i == nst - 1;
            fd_save(&save);
            if (prev) {
                prev->mpos = 0;
                fd_assign(0, prev);
                prev = NULL;
            }
            if (!last) {
                mem = shf_mem();
                fd_assign(1, shf_ref(mem));
            }
            if (last && sh.sh_lastpipe) st[i] = exec_node(n->u.pipe.cmds[i]);
            else st[i] = exec_subshell_node(n->u.pipe.cmds[i]);
            sh_flush_all();
            fd_restore(&save);
            prev = mem;
            if (sh.flow == FLOW_INTR || sh.flow == FLOW_EXIT) {
                if (prev) shf_close(prev);
                prev = NULL;
                for (i++; i < nst; i++) st[i] = 130;
                break;
            }
        }
        if (prev) shf_close(prev);
        status = st[nst - 1];
        if (sh.opt_pipefail) {
            for (i = nst - 1; i >= 0; i--)
                if (st[i]) {
                    status = st[i];
                    break;
                }
        }
    }
    set_pipestatus(st, nst);
    xfree(st);
    if (n->u.pipe.bang) status = !status;
timed:
    if (n->u.pipe.timed) {
        long ms = os_time_ms() - t0;
        sh_printf(2, "\nreal\t%ldm%ld.%03lds\nuser\t0m0.000s\nsys\t0m0.000s\n",
                  ms / 60000, (ms / 1000) % 60, ms % 1000);
    }
    return status;
}

static int loop_flow(void)
{
    /* returns 1 if the loop must stop */
    if (sh.flow == FLOW_BREAK) {
        if (--sh.flowcount <= 0) sh.flow = FLOW_NONE;
        return 1;
    }
    if (sh.flow == FLOW_CONTINUE) {
        if (--sh.flowcount <= 0) {
            sh.flow = FLOW_NONE;
            return 0;
        }
        return 1;
    }
    return sh.flow != FLOW_NONE;
}

static int exec_for(Node *n)
{
    StrList items;
    int i, status = 0;
    sl_init(&items);
    if (n->u.forn.has_in) {
        expand_error = 0;
        if (expand_words(n->u.forn.words, &items, 0)) {
            sl_free(&items);
            expansion_failed(expand_error);
            return 1;
        }
    } else {
        for (i = 0; i < sh_params.n; i++) sl_adddup(&items, sh_params.v[i]);
    }
    sh.loopdepth++;
    for (i = 0; i < items.n; i++) {
        if (check_interrupt()) break;
        if (sh.opt_xtrace) {
            char *pre = str_printf("for %s in", n->u.forn.var);
            xtrace_line(pre, items.v, items.n, NULL);
            xfree(pre);
        }
        if (var_set(n->u.forn.var, items.v[i], 0)) {
            status = 1;
            break;
        }
        status = exec_list_node(n->u.forn.body);
        if (loop_flow()) break;
    }
    sh.loopdepth--;
    sl_free(&items);
    return status;
}

static int exec_select(Node *n)
{
    StrList items;
    int i, status = 0;
    SBuf line;
    sl_init(&items);
    sb_init(&line);
    if (n->u.forn.has_in) {
        if (expand_words(n->u.forn.words, &items, 0)) {
            sl_free(&items);
            return 1;
        }
    } else {
        for (i = 0; i < sh_params.n; i++) sl_adddup(&items, sh_params.v[i]);
    }
    sh.loopdepth++;
    for (;;) {
        const char *ps3;
        int nl;
        ShInt choice;
        for (i = 0; i < items.n; i++) sh_printf(2, "%d) %s\n", i + 1, items.v[i]);
        ps3 = var_get("PS3");
        sh_puts(2, ps3 ? ps3 : "#? ");
        sh_flush_all();
        shf_flush(sh_fd(2));
        if (!sh_getline(0, &line, &nl)) {
            status = 1;
            break;
        }
        var_set("REPLY", sb_str(&line), 0);
        if (!line.len) continue;
        if (str_toint(sb_str(&line), &choice) && choice >= 1 && choice <= items.n)
            var_set(n->u.forn.var, items.v[choice - 1], 0);
        else
            var_set(n->u.forn.var, "", 0);
        status = exec_list_node(n->u.forn.body);
        if (loop_flow()) break;
        if (check_interrupt()) break;
    }
    sh.loopdepth--;
    sl_free(&items);
    sb_free(&line);
    return status;
}

static int exec_arithfor(Node *n)
{
    ShInt v;
    int status = 0;
    if (*n->u.afor.init && arith_expand_eval(n->u.afor.init, &v)) return 1;
    sh.loopdepth++;
    for (;;) {
        if (check_interrupt()) break;
        if (*n->u.afor.cond) {
            if (arith_expand_eval(n->u.afor.cond, &v)) {
                status = 1;
                break;
            }
            if (!v) break;
        }
        status = exec_list_node(n->u.afor.body);
        if (loop_flow()) break;
        if (*n->u.afor.step && arith_expand_eval(n->u.afor.step, &v)) {
            status = 1;
            break;
        }
    }
    sh.loopdepth--;
    return status;
}

static int exec_while(Node *n, int until)
{
    int status = 0, c;
    sh.loopdepth++;
    for (;;) {
        if (check_interrupt()) break;
        sh.errexit_suppressed++;
        c = exec_list_node(n->u.loop.cond);
        sh.errexit_suppressed--;
        if (sh.flow != FLOW_NONE) {
            if (loop_flow()) break;
            continue;
        }
        if (until ? c == 0 : c != 0) break;
        status = exec_list_node(n->u.loop.body);
        if (loop_flow()) break;
    }
    sh.loopdepth--;
    return status;
}

static int exec_case(Node *n)
{
    char *word;
    CaseItem *ci;
    int status = 0, fall = 0;
    if (sh.opt_xtrace) {
        char *pre = str_printf("case %s in", n->u.casen.word);
        xtrace_line(pre, NULL, 0, NULL);
        xfree(pre);
    }
    word = expand_str(n->u.casen.word, 0);
    if (!word) {
        expansion_failed(1);
        return 1;
    }
    for (ci = n->u.casen.items; ci; ci = ci->next) {
        int matched = fall;
        Word *p;
        if (!matched) {
            for (p = ci->pats; p; p = p->next) {
                char *pat = expand_str(p->text, X_PATTERN);
                int m;
                if (!pat) {
                    xfree(word);
                    return 1;
                }
                m = pat_match(pat, word, sh.sh_nocasematch ? PM_NOCASE : 0);
                xfree(pat);
                if (m) {
                    matched = 1;
                    break;
                }
            }
        }
        if (matched) {
            status = ci->body ? exec_list_node(ci->body) : 0;
            if (sh.flow != FLOW_NONE) break;
            if (ci->term == 1) {
                fall = 1;
                continue;
            }
            fall = 0;
            if (ci->term == 2) continue;
            break;
        }
    }
    xfree(word);
    return status;
}

static int exec_compound_redir(Node *n, int (*fn)(Node *))
{
    FdSave save;
    int status, psmark;
    if (!n->redirs) return fn(n);
    psmark = procsub_mark();
    if (apply_redirs(n->redirs, &save)) {
        fd_restore(&save);
        procsub_cleanup_to(psmark);
        return 1;
    }
    status = fn(n);
    sh_flush_all();
    fd_restore(&save);
    procsub_cleanup_to(psmark);
    return status;
}

static int do_node(Node *n);

/* statuses of finished "background" jobs (they ran synchronously) */
#define NBGSTAT 16
static struct { long id; int status; } bgstat[NBGSTAT];
static int nbgstat;

static void bg_record(long id, int status)
{
    bgstat[nbgstat % NBGSTAT].id = id;
    bgstat[nbgstat % NBGSTAT].status = status;
    nbgstat++;
}

/* status of background job id; -1 if unknown */
int bg_status(long id)
{
    int i;
    for (i = 0; i < NBGSTAT; i++)
        if (bgstat[i].id == id && id) return bgstat[i].status;
    return -1;
}

static int exec_bg(Node *n)
{
    Node *b = n->u.one.body;
    /* simple external command: really run it in the background */
    if (b->type == N_SIMPLE && !b->redirs && !b->u.simple.assigns) {
        StrList args;
        sl_init(&args);
        if (!expand_words(b->u.simple.words, &args, 0) && args.n &&
            !func_lookup(args.v[0]) && !builtin_lookup(args.v[0])) {
            char *path = find_command(args.v[0]);
            if (path) {
                long id;
                sh_flush_all();
                id = os_run_bg(path, args.v);
                xfree(path);
                sl_free(&args);
                if (id < 0) {
                    sh_error("%s: %s", "background", os_errstr());
                    return 1;
                }
                sh.lastbg = id;
                if (sh.interactive) sh_printf(2, "[1] %ld\n", id);
                return 0;
            }
        }
        sl_free(&args);
    }
    /* otherwise run synchronously in a subshell (no concurrency); the
     * status is remembered for "wait $!" */
    {
        static long fake_id;
        int st;
        if (!fake_id) fake_id = sh.pid + 1;
        sh.lastbg = fake_id++;
        st = exec_subshell_node(b);
        bg_record(sh.lastbg, st);
    }
    return 0;
}

static int do_node(Node *n)
{
    int status = 0;
    switch (n->type) {
    case N_SIMPLE:
        status = exec_simple(n);
        break;
    case N_PIPE:
        status = exec_pipeline(n);
        break;
    case N_AND:
    case N_OR:
        sh.errexit_suppressed++;
        status = exec_node(n->u.bin.l);
        sh.errexit_suppressed--;
        if (sh.flow != FLOW_NONE) break;
        if (n->type == N_AND ? status == 0 : status != 0) status = exec_node(n->u.bin.r);
        break;
    case N_SEQ:
        status = exec_node(n->u.bin.l);
        if (sh.flow != FLOW_NONE) break;
        if (check_interrupt()) break;
        status = exec_node(n->u.bin.r);
        break;
    case N_BG:
        status = exec_bg(n);
        break;
    case N_NOT:
        sh.errexit_suppressed++;
        status = !exec_node(n->u.one.body);
        sh.errexit_suppressed--;
        break;
    case N_SUBSHELL:
        status = exec_subshell_node(n->u.one.body);
        break;
    case N_GROUP:
        status = exec_list_node(n->u.one.body);
        break;
    case N_IF: {
        int c;
        sh.errexit_suppressed++;
        c = exec_list_node(n->u.ifn.cond);
        sh.errexit_suppressed--;
        if (sh.flow != FLOW_NONE) break;
        if (c == 0) status = exec_list_node(n->u.ifn.then);
        else if (n->u.ifn.els) status = exec_list_node(n->u.ifn.els);
        else status = 0;
        break;
    }
    case N_WHILE:
    case N_UNTIL:
        status = exec_while(n, n->type == N_UNTIL);
        break;
    case N_FOR:
        status = exec_for(n);
        break;
    case N_SELECT:
        status = exec_select(n);
        break;
    case N_ARITHFOR:
        status = exec_arithfor(n);
        break;
    case N_CASE:
        status = exec_case(n);
        break;
    case N_FUNCDEF:
        if (!exec_arena) {
            sh_error("internal error: no arena for function");
            return 1;
        }
        func_define(n->u.func.name, n->u.func.body, exec_arena);
        status = 0;
        break;
    case N_ARITH: {
        ShInt v;
        if (sh.opt_xtrace) sh_printf(2, "%s(( %s ))\n", var_get("PS4") ? var_get("PS4") : "+ ", n->u.arith.expr);
        arith_errprefix = "((";
        if (arith_expand_eval(n->u.arith.expr, &v)) status = 1;
        else status = v == 0;
        arith_errprefix = NULL;
        break;
    }
    case N_COND:
        status = cond_eval(n->u.cond.expr);
        break;
    }
    return status;
}

static int exec_list_node(Node *n)
{
    if (!n) return 0;
    return exec_node(n);
}

int exec_node(Node *n)
{
    int status;
    if (!n) return 0;
    if (sh.flow != FLOW_NONE) return sh.laststatus;
    if (!os_stack_ok() || exec_depth > 2000) {
        sh_error("maximum nesting level exceeded");
        sh.flow = FLOW_EXIT;
        sh.exitstatus = 2;
        return 2;
    }
    exec_depth++;
    if (n->lineno > 0) sh.lineno = n->lineno;
    if (sh.opt_noexec && !sh.interactive) {
        exec_depth--;
        return 0;
    }
    switch (n->type) {
    case N_SIMPLE:
    case N_PIPE:
    case N_AND:
    case N_OR:
    case N_SEQ:
    case N_BG:
    case N_NOT:
    case N_FUNCDEF:
        status = do_node(n);
        break;
    default:
        status = exec_compound_redir(n, do_node);
    }
    exec_depth--;
    sh.laststatus = status;
    if (n->type == N_SIMPLE || n->type == N_SUBSHELL || n->type == N_ARITH || n->type == N_COND)
        set_pipestatus(&status, 1);         /* a one-command pipeline */
    if (n->type == N_SIMPLE || n->type == N_PIPE || n->type == N_SUBSHELL ||
        n->type == N_ARITH || n->type == N_COND) {
        if (n->type != N_PIPE || !n->u.pipe.bang) errexit_check(status);
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* strings and files                                                    */

/* print a parse error like bash: the message, then the offending line */
void report_syntax_error(Parser *p, const char *ctx)
{
    int ln = 0;
    const char *line = parser_errline(p, &ln);
    sh.lineno = ln > 0 ? ln : parser_lineno(p);
    if (ctx) {
        sh_error_ctx(ctx, "%s", parser_errmsg(p));
        if (*line) sh_error_ctx(ctx, "`%s'", line);
    } else {
        sh_error("%s", parser_errmsg(p));
        if (*line) sh_error("`%s'", line);
    }
}

int exec_string(const char *src, const char *name)
{
    Arena *a = arena_new();
    Arena *save_arena = exec_arena;
    /* eval, traps and $(...) count lines from the current line */
    Parser *p = parser_new(src, a, sh.lineno > 0 ? sh.lineno : 1);
    int status = 0;
    parser_set_final(p);
    int soft = name && (strcmp(name, "eval") == 0 || strcmp(name, "trap") == 0);
    exec_arena = a;
    for (;;) {
        Node *n;
        int r = parser_next(p, &n);
        if (r == PARSE_EOF) break;
        if (r != PARSE_OK) {
            /* a syntax error in eval/trap text only fails that command */
            report_syntax_error(p, soft ? name : NULL);
            status = 2;
            if (!sh.interactive && !soft) {
                sh.flow = FLOW_EXIT;
                sh.exitstatus = 2;
            }
            break;
        }
        if (n) {
            status = exec_node(n);
            if (sh.flow != FLOW_NONE) break;
            if (check_interrupt()) break;
        }
    }
    parser_free(p);
    exec_arena = save_arena;
    arena_free(a);
    return status;
}

static char *read_file(const char *path)
{
    ShFile *f = shf_open(path, OS_RD);
    SBuf b;
    char buf[1024];
    long n;
    if (!f) return NULL;
    sb_init(&b);
    sb_addn(&b, "", 0);
    while ((n = shf_read(f, buf, sizeof(buf))) > 0) sb_addn(&b, buf, (int)n);
    shf_close(f);
    return sb_take(&b);
}

int exec_file(const char *path, char **argv, int argc, int keep_params)
{
    char *src = read_file(path);
    Arena *a, *save_arena = exec_arena;
    Parser *p;
    Params saved;
    int status = 0;
    char *save_script = sh.script;
    int save_line = sh.lineno;
    if (!src) {
        sh_error("%s: %s", path, os_errstr());
        return 127;
    }
    if (!keep_params) {
        saved = sh_params;
        sh_params.v = NULL;
        sh_params.n = 0;
        params_set(argv, argc);
    }
    a = arena_new();
    exec_arena = a;
    {
        /* skip #! line */
        p = parser_new(src, a, 1);
        parser_set_final(p);
    }
    xfree(src);
    sh.script = (char *)path;
    sh.sourcedepth++;
    stack_push("BASH_SOURCE", path);
    for (;;) {
        Node *n;
        int r = parser_next(p, &n);
        if (r == PARSE_EOF) break;
        if (r != PARSE_OK) {
            sh.lineno = parser_lineno(p);
            report_syntax_error(p, NULL);
            status = 2;
            /* the script itself exits; a sourced file just fails */
            if (!sh.interactive && sh.sourcedepth == 1) {
                sh.flow = FLOW_EXIT;
                sh.exitstatus = 2;
            }
            break;
        }
        if (n) {
            status = exec_node(n);
            if (sh.flow == FLOW_ABORT && sh.sourcedepth == 1) {
                /* expansion error: continue with the next command */
                sh.flow = FLOW_NONE;
                status = sh.laststatus = 1;
            }
            if (sh.flow != FLOW_NONE) break;
        }
    }
    sh.sourcedepth--;
    stack_pop("BASH_SOURCE");
    if (sh.flow == FLOW_RETURN) {
        sh.flow = FLOW_NONE;
        status = sh.exitstatus;
    }
    parser_free(p);
    exec_arena = save_arena;
    arena_free(a);
    sh.script = save_script;
    sh.lineno = save_line;
    if (!keep_params) {
        params_free(&sh_params);
        sh_params = saved;
    }
    return status;
}
