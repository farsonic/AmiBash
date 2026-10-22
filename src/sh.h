/*
 * sh.h - AmiBash core declarations.
 *
 * AmiBash is a bash-compatible shell for AmigaOS.  It is a from-scratch
 * implementation (bash itself depends on fork(), which AmigaOS lacks);
 * subshells, pipelines and command substitution are emulated in-process.
 */
#ifndef AMIBASH_SH_H
#define AMIBASH_SH_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include "os.h"

#define AMIBASH_VERSION "5.2.0(1)-amiga"
#define AMIBASH_SHORTVER "0.1"
#define AMIBASH_DATE "22.10.2026"
#define AMIBASH_STR2(x) #x
#define AMIBASH_STR(x) AMIBASH_STR2(x)
#ifdef AMIBASH_CPU
#define AMIBASH_BUILD AMIBASH_STR(AMIBASH_CPU)
#endif
#ifndef AMIBASH_BUILD
#ifdef AMIGA
#define AMIBASH_BUILD "68000"
#else
#define AMIBASH_BUILD "host"
#endif
#endif

typedef long long ShInt;              /* arithmetic type (bash: intmax_t) */

/* ------------------------------------------------------------------ */
/* util.c : memory, strings, lists                                     */
/* ------------------------------------------------------------------ */
void *xmalloc(size_t n);
void *xcalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void  xfree(void *p);

typedef struct SBuf { char *s; int len, cap; } SBuf;
void  sb_init(SBuf *b);
void  sb_addc(SBuf *b, int c);
void  sb_adds(SBuf *b, const char *s);
void  sb_addn(SBuf *b, const char *s, int n);
void  sb_printf(SBuf *b, const char *fmt, ...);
void  sb_vprintf(SBuf *b, const char *fmt, va_list ap);
char *sb_take(SBuf *b);            /* malloc'd NUL-terminated; b reset */
void  sb_clear(SBuf *b);           /* len = 0, keep memory             */
void  sb_free(SBuf *b);
#define sb_str(b) ((b)->s ? (b)->s : "")

typedef struct StrList { char **v; int n, cap; } StrList;
void  sl_init(StrList *l);
void  sl_add(StrList *l, char *s);     /* takes ownership of s           */
void  sl_adddup(StrList *l, const char *s);
void  sl_free(StrList *l);             /* frees strings and vector       */
void  sl_sort(StrList *l, int nocase);
char **sl_argv(StrList *l);            /* NULL-terminated view (v)       */

int   str_casecmp(const char *a, const char *b);
int   str_ncasecmp(const char *a, const char *b, size_t n);
int   str_startswith(const char *s, const char *p);
int   str_endswith(const char *s, const char *p);
int   str_isname(const char *s);       /* valid shell identifier         */
int   str_isnum(const char *s);        /* optional sign + digits         */
char *str_cat(const char *a, const char *b); /* malloc'd a+b              */
char *str_printf(const char *fmt, ...);
char *str_itoa(ShInt v);               /* malloc'd                       */
int   str_toint(const char *s, ShInt *out); /* 1 ok, 0 not a number     */

/* time helpers (timeutil.c); independent of the C library's notion of
 * time zones.  Amiga clock is local time, so os_tz_offset() is 0 there. */
typedef struct ShTm { int year, mon, mday, hour, min, sec, wday, yday; } ShTm;
void   sh_localtime(long t, ShTm *tm);      /* epoch -> local broken-down */
long   sh_mktime(const ShTm *tm);           /* local broken-down -> epoch */
/* strftime subset: %a %A %b %B %c %C %d %D %e %F %H %I %j %k %l %m %M
 * %n %p %r %R %s %S %t %T %u %w %x %X %y %Y %z %Z %% ; returns length */
size_t sh_strftime(char *buf, size_t n, const char *fmt, const ShTm *tm);

/* simple arena for parse trees */
typedef struct Arena Arena;
Arena *arena_new(void);
void  *arena_alloc(Arena *a, size_t n);
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);
void   arena_free(Arena *a);              /* drops one reference      */
void   arena_ref(Arena *a);

/* getopt for builtins: returns option char, '?' on error (message
 * printed), or -1 at end.  Stops at first non-option or "--". */
typedef struct GetOpt { int ind; int sp; char *arg; int opt; const char *name; } GetOpt;
void sh_getopt_init(GetOpt *g, const char *cmdname);
int  sh_getopt(GetOpt *g, int argc, char **argv, const char *optstring);

/* ------------------------------------------------------------------ */
/* io.c : shell file descriptors                                       */
/* ------------------------------------------------------------------ */
#define SH_NFD 10

typedef struct ShFile ShFile;
#define SHF_OS   1            /* backed by an OsHandle                 */
#define SHF_MEM  2            /* memory buffer (pipes, $(...))         */
#define SHF_NULL 3            /* bit bucket                             */

struct ShFile {
    int kind;
    int refs;
    OsHandle h;               /* SHF_OS                                 */
    int owned;                /* close h when refs drop to zero          */
    int tty;                  /* interactive console                    */
    char *mem; long mlen, mcap, mpos;   /* SHF_MEM data / read position  */
    char *wbuf; int wlen;     /* write buffer (SHF_OS)                  */
    char *rbuf; int rlen, rpos; /* read buffer (SHF_OS, non-tty)        */
    int eof;
    char *tmpname;            /* temp file to delete on close           */
};

ShFile *shf_open(const char *path, int mode); /* shell path; NULL+msg in os_errstr */
ShFile *shf_os(OsHandle h, int owned);
ShFile *shf_mem(void);
ShFile *shf_null(void);
ShFile *shf_ref(ShFile *f);
void    shf_close(ShFile *f);              /* drop one reference       */
long    shf_read(ShFile *f, void *buf, long n);
long    shf_write(ShFile *f, const void *buf, long n);
int     shf_getc(ShFile *f);               /* -1 at EOF                */
/* read a line (without newline) into line; returns 0 at EOF with no
 * data, 1 if a line was read; *nl set to 1 if it ended with newline */
int     shf_getline(ShFile *f, SBuf *line, int *nl);
void    shf_flush(ShFile *f);
/* make sure f has a real OS handle (for external commands): memory
 * files are spilled to a temporary file.  for_write: the handle will be
 * written to; after the command call shf_os_done(). */
OsHandle shf_os_handle(ShFile *f, int for_write);
void     shf_os_done(ShFile *f, int for_write);

extern ShFile *sh_fds[SH_NFD];
ShFile *sh_fd(int fd);                    /* NULL if closed           */
long  sh_write(int fd, const void *buf, long n);
void  sh_puts(int fd, const char *s);
void  sh_putc(int fd, int c);
void  sh_printf(int fd, const char *fmt, ...);
long  sh_read(int fd, void *buf, long n);
int   sh_getline(int fd, SBuf *line, int *nl);
void  sh_flush_all(void);
/* "bash: msg\n" on fd 2 (prefixed with script name/line when needed) */
void  sh_error(const char *fmt, ...);
/* "bash: cmd: msg\n" */
void  sh_cmd_error(const char *cmd, const char *fmt, ...);
void  sh_error_ctx(const char *ctx, const char *fmt, ...);
extern int sh_util_running;   /* utility builtin: messages unprefixed  */

/* save/restore of the fd table around redirections */
typedef struct FdSave { ShFile *saved[SH_NFD]; } FdSave;
void  fd_save(FdSave *s);
void  fd_restore(FdSave *s);
void  fd_assign(int fd, ShFile *f);           /* takes the reference      */
void  io_init(void);

/* ------------------------------------------------------------------ */
/* path.c : Amiga <-> Unix style path handling                         */
/* ------------------------------------------------------------------ */
/* Convert a path as typed by the user to a native path (malloc'd).
 * Amiga:  "DH0:x"   -> "DH0:x"        (native, unchanged)
 *         "/dh0/x"  -> "dh0:x"        (unix style absolute)
 *         "/"       -> "/"            (virtual root: volumes & assigns)
 *         "../x"    -> resolved against $PWD
 *         "x/y"     -> "x/y"          (relative, unchanged)
 * POSIX:  identity. */
char *path_native(const char *p);
/* normalise a path to an absolute shell path (for PWD), malloc'd */
char *path_absolute(const char *p);
int   path_is_vroot(const char *native);
char *path_amiga_normalize(const char *p, const char *cwd); /* testable */   /* "/" on Amiga             */
char *path_join(const char *dir, const char *name); /* malloc'd        */
const char *path_basename(const char *p); /* pointer into p            */
char *path_dirname(const char *p);        /* malloc'd                  */
int   path_has_dir(const char *p);        /* contains '/' or ':'       */
int   path_is_absolute(const char *p);
/* translated wrappers */
int   sh_stat(const char *path, OsDirEnt *e);
void *sh_opendir(const char *path);
int   sh_mkdir(const char *path);
int   sh_remove(const char *path);
int   sh_rename(const char *from, const char *to);
int   sh_is_dir(const char *path);
int   sh_exists(const char *path);

/* ------------------------------------------------------------------ */
/* pattern.c : glob patterns                                           */
/* ------------------------------------------------------------------ */
#define PM_NOCASE 1
#define PM_DOTGLOB 2
/* match str against shell pattern pat; backslash quotes next char */
int  pat_match(const char *pat, const char *str, int flags);
/* length of the shortest/longest prefix (or suffix) of str matching pat,
 * -1 if none.  Used by ${x#p} ${x##p} ${x%p} ${x%%p} */
int  pat_prefix(const char *pat, const char *str, int longest, int flags);
int  pat_suffix(const char *pat, const char *str, int longest, int flags);
int  pat_has_glob(const char *pat);        /* unescaped * ? [         */
char *pat_unescape(const char *pat);       /* remove backslashes       */
char *pat_escape(const char *s);           /* escape glob chars        */
/* pathname expansion; appends sorted matches to out, returns count */
int  glob_expand(const char *pat, StrList *out, int flags);

/* ------------------------------------------------------------------ */
/* regex.c : POSIX extended (and basic) regular expressions            */
/* ------------------------------------------------------------------ */
typedef struct Regex Regex;
#define RE_ICASE   1
#define RE_BASIC   2          /* BRE syntax (grep/sed default)          */
#define RE_NEWLINE 4
#define RE_NOSUB   8
#define RE_NOTBOL  1          /* exec flag                              */
typedef struct ReMatch { int so, eo; } ReMatch;   /* -1 if unset       */
Regex *re_compile(const char *pat, int flags, char **err); /* err malloc'd */
/* returns 1 on match; m[0..nm-1] filled */
int    re_exec(Regex *re, const char *s, int nm, ReMatch *m, int eflags);
int    re_nsub(Regex *re);
void   re_free(Regex *re);

/* ------------------------------------------------------------------ */
/* parse.c : syntax tree                                               */
/* ------------------------------------------------------------------ */
typedef struct ShNode Node;
typedef struct Redir Redir;
typedef struct Word Word;

struct Word {                  /* unexpanded word (raw source text)     */
    char *text;
    Word *next;
};

enum {
    R_IN, R_OUT, R_APPEND, R_CLOBBER, R_RDWR, R_DUPIN, R_DUPOUT,
    R_HEREDOC, R_HERESTR, R_OUTERR, R_APPERR
};

struct Redir {
    int op;
    int fd;                    /* -1 = default for op                   */
    char *varfd;               /* {name}> style, unused                 */
    char *target;              /* raw word / heredoc delimiter          */
    char *heredoc;             /* heredoc body                          */
    int quoted;                /* heredoc delimiter was quoted          */
    int striptabs;             /* <<-                                   */
    Redir *next;
};

enum {
    N_SIMPLE, N_PIPE, N_AND, N_OR, N_SEQ, N_BG, N_NOT,
    N_SUBSHELL, N_GROUP, N_IF, N_WHILE, N_UNTIL, N_FOR, N_ARITHFOR,
    N_CASE, N_FUNCDEF, N_ARITH, N_COND, N_SELECT
};

/* [[ ]] expression tree */
typedef struct CondNode CondNode;
enum { C_AND, C_OR, C_NOT, C_UNARY, C_BINARY, C_WORD };
struct CondNode {
    int type;
    char *op;                  /* operator for unary/binary             */
    char *w1, *w2;             /* raw words                             */
    CondNode *l, *r;
};

typedef struct CaseItem CaseItem;
struct CaseItem {
    Word *pats;
    Node *body;
    int term;                  /* 0 = ;;  1 = ;&  2 = ;;&               */
    CaseItem *next;
};

struct ShNode {
    int type;
    int lineno;
    Redir *redirs;
    union {
        struct { Word *assigns; Word *words; } simple;
        struct { Node **cmds; int n; int bang; int timed; } pipe;
        struct { Node *l, *r; } bin;          /* AND OR SEQ             */
        struct { Node *body; } one;           /* BG NOT SUBSHELL GROUP  */
        struct { Node *cond, *then, *els; } ifn;
        struct { Node *cond, *body; } loop;   /* WHILE UNTIL            */
        struct { char *var; Word *words; int has_in; Node *body; } forn;
        struct { char *init, *cond, *step; Node *body; } afor;
        struct { char *word; CaseItem *items; } casen;
        struct { char *name; Node *body; char *src; } func;
        struct { char *expr; } arith;
        struct { CondNode *expr; } cond;
    } u;
};

typedef struct Parser Parser;
#define PARSE_OK         0
#define PARSE_EOF        1
#define PARSE_INCOMPLETE 2
#define PARSE_ERROR      3
Parser *parser_new(const char *src, Arena *arena, int lineno);
/* parse the next complete command; *out may be NULL for blank lines */
int     parser_next(Parser *p, Node **out);
int     parser_lineno(Parser *p);
void    parser_set_final(Parser *p);   /* whole source given (file/eval) */
const char *parser_errmsg(Parser *p);
const char *parser_errline(Parser *p, int *lineno);
/* report a parse error like bash ("ctx: line N: msg" + "`line'") */
void    report_syntax_error(Parser *p, const char *ctx);
const char *parser_pos(Parser *p);     /* remaining input              */
void    parser_free(Parser *p);
/* parse a whole string (for eval / $(...) / functions); NULL on error */
Node   *parse_string(const char *src, Arena *a, int *status);
/* find end of $( ... ) starting after "$(" ; returns pointer at ')' */
const char *scan_cmdsub_end(const char *s);
/* raw scanners: s points just after the opening char; return pointer
 * after the closing char, NULL if unterminated */
const char *scan_skip_squote(const char *s);
const char *scan_skip_dquote(const char *s);
const char *scan_skip_dollar(const char *s);   /* s at '$' */
const char *scan_skip_backtick(const char *s);
const char *scan_skip_brace(const char *s);    /* after "${" */
const char *scan_skip_ansi(const char *s);     /* after "$'" */
const char *scan_skip_parens(const char *s);   /* after '(' */
const char *scan_word_end(const char *s);
void    parser_skip_line(Parser *p);
/* reconstruct source text for a node (type -a / declare -f) */
void    node_to_text(Node *n, SBuf *b, int indent);

/* ------------------------------------------------------------------ */
/* vars.c : variables                                                  */
/* ------------------------------------------------------------------ */
#define V_EXPORT   0x0001
#define V_READONLY 0x0002
#define V_INTEGER  0x0004
#define V_ARRAY    0x0008
#define V_ASSOC    0x0010
#define V_LOWER    0x0020
#define V_UPPER    0x0040
#define V_LOCAL    0x0080
#define V_UNSET    0x0100      /* declared but no value ("local x")     */
#define V_SPECIAL  0x0200      /* dynamic value (RANDOM, SECONDS...)    */
#define V_NAMEREF  0x0400

typedef struct ArrEl { ShInt idx; char *key; char *val; } ArrEl;

typedef struct Var {
    char *name;
    char *val;                 /* scalar value                          */
    int flags;
    ArrEl *arr; int alen, acap;/* indexed (sorted by idx) or assoc     */
    struct Var *next;          /* hash chain                            */
    int scope;                 /* function scope level it belongs to    */
    int jgen;                  /* subshell journal generation           */
} Var;

void  vars_init(void);
Var  *var_lookup(const char *name);
void  pipestatus_sync(void);                 /* exec.c */          /* follows namerefs      */
Var  *var_lookup_raw(const char *name);      /* the variable itself   */
const char *var_resolve(const char *name);   /* nameref target name   */
int   var_set_nameref(const char *name, const char *target);
const char *var_get(const char *name);      /* NULL if unset           */
/* set scalar (or element 0 of array); returns 0 ok, 1 readonly error */
int   var_set(const char *name, const char *val, int flags);
int   var_set_int(const char *name, ShInt v);
int   var_unset(const char *name);           /* 0 ok, 1 readonly       */
int   var_append(const char *name, const char *val);  /* x+=val       */
/* arrays */
int   var_arr_set(const char *name, const char *key, const char *val);
const char *var_arr_get(Var *v, const char *key);
int   var_arr_unset(const char *name, const char *key);
void  var_arr_clear(Var *v);
int   var_arr_count(Var *v);
/* list array values (or keys) into out (strdup'd) */
void  var_arr_values(Var *v, StrList *out, int keys);
Var  *var_make_array(const char *name, int assoc);
/* scope: functions */
void  var_push_scope(void);
void  var_pop_scope(void);
int   var_make_local(const char *name);
int   var_scope_level(void);
/* subshell journal: changes after mark are undone by var_rollback */
int   var_journal_mark(void);
void  var_rollback(int mark);
void  var_export_all(void);                  /* sync exported to OS env */
void  var_list(int fd, int flags_filter, int fmt); /* set/declare -p   */
void  var_print_decl(int fd, Var *v);
void  var_attr_string(Var *v, char *buf);    /* "aAinrxlu" subset     */
char *shell_quote(const char *s);              /* quote for re-input */
/* iterate all variables (sorted) */
void  var_names(StrList *out, const char *prefix);

/* positional parameters */
typedef struct Params { char **v; int n; } Params;
extern Params sh_params;
void  params_set(char **argv, int n);       /* copies                   */
void  params_free(Params *p);
void  params_copy(Params *dst, Params *src);

/* functions */
typedef struct Func { char *name; Node *body; Arena *arena; int refs; struct Func *next; } Func;
Func *func_lookup(const char *name);
void  func_define(const char *name, Node *body, Arena *arena);
int   func_unset(const char *name);
void  func_names(StrList *out, const char *prefix);
/* subshell journal for function and alias definitions */
int   func_journal_mark(void);
void  func_rollback(int mark);

/* aliases */
const char *alias_get(const char *name);
void  alias_set(const char *name, const char *val);
int   alias_unset(const char *name);
void  alias_names(StrList *out, const char *prefix);

/* ------------------------------------------------------------------ */
/* expand.c                                                             */
/* ------------------------------------------------------------------ */
#define X_NOSPLIT  0x01        /* no field splitting                    */
#define X_NOGLOB   0x02        /* no pathname expansion                 */
#define X_ASSIGN   0x04        /* assignment: tilde after = and :       */
#define X_PATTERN  0x08        /* result is a pattern: escape quoted    */
#define X_NOBRACE  0x10
#define X_HEREDOC  0x20        /* heredoc body: only $ ` \ processing   */
#define X_DQUOTE   0x40        /* as if inside double quotes            */
#define X_REGEX    0x80        /* [[ =~ ]]: quoted chars escaped for ERE */

/* expand words into fields; returns 0 ok, nonzero on expansion error */
int   expand_words(Word *w, StrList *out, int flags);
int   expand_word(const char *raw, StrList *out, int flags);
/* expand to a single string (no split/glob); NULL on error */
char *expand_str(const char *raw, int flags);
/* run a command string, capture stdout (trailing newlines removed) */
char *command_subst(const char *cmd);
extern int cmdsubst_status;   /* status of the last $(...), -1 = none  */
int   brace_expand(const char *raw, StrList *out);
char *tilde_expand_str(const char *s);       /* for ~user ~+ ~- etc     */
/* decode backslash escapes; echo_style: \c stops (sets *stop), \0nnn */
char *ansi_decode(const char *s, int n, int echo_style, int *stop);
extern int ansi_decode_len;   /* length of its result (may hold NULs)  */
void  procsub_cleanup(void);
int   procsub_mark(void);
void  procsub_cleanup_to(int mark);       /* files created after mark  */
/* prompt string expansion (\u \h \w ...) then parameter expansion */
char *prompt_expand(const char *ps);
extern int expand_error;   /* set by failing expansions                 */

/* arith.c */
/* evaluate expression (already parameter-expanded by caller if wanted);
 * returns 0 ok, 1 error (message printed) */
int   arith_eval(const char *expr, ShInt *result);
/* expand $... in expr then evaluate */
int   arith_expand_eval(const char *expr, ShInt *result);
extern const char *arith_errprefix;   /* "((" / "let" in messages     */

/* ------------------------------------------------------------------ */
/* exec.c                                                               */
/* ------------------------------------------------------------------ */
/* control flow */
#define FLOW_NONE     0
#define FLOW_BREAK    1
#define FLOW_CONTINUE 2
#define FLOW_RETURN   3
#define FLOW_EXIT     4
#define FLOW_INTR     5        /* Ctrl-C: unwind to the prompt         */
#define FLOW_ABORT    6        /* expansion error: abandon the current
                                  top-level command (status 1)          */

typedef int (*BuiltinFn)(int argc, char **argv);
typedef struct Builtin {
    const char *name;
    BuiltinFn fn;
    int flags;
    const char *usage;         /* one-line usage for "help"             */
} Builtin;
#define BI_SPECIAL 1           /* POSIX special builtin                 */
#define BI_UTIL    2           /* coreutils-style utility (ls, cat...)  */
#define BI_DISABLED 4

extern const Builtin shell_builtins[];   /* builtins.c                  */
extern const Builtin util_builtins[];    /* utils*.c                    */
const Builtin *builtin_lookup(const char *name);
void  builtin_set_enabled(const char *name, int on);
void  builtin_names(StrList *out, const char *prefix);

int   exec_node(Node *n);
int   exec_string(const char *src, const char *name); /* eval/source    */
int   exec_file(const char *path, char **argv, int argc, int keep_params);
int   exec_subshell_string(const char *src);   /* (...) semantics       */
/* run argv as a command (builtin/function/external); no redirs        */
int   exec_argv(int argc, char **argv);
int   exec_external(int argc, char **argv, const char *path);
/* search PATH; malloc'd native path or NULL */
char *find_command(const char *name);
int   find_command_all(const char *name, StrList *out);  /* type -a */
void  hash_clear(void);
void  hash_list(int fd);
int   run_function(Func *f, int argc, char **argv);
int   apply_redirs(Redir *r, FdSave *save);
void  run_exit_trap(void);
void  expansion_failed(int e);       /* FLOW_EXIT or FLOW_ABORT     */
/* set -x output: PS4 prefix words(quoted) suffix */
void  xtrace_line(const char *prefix, char **argv, int argc, const char *suffix);
int   check_interrupt(void);       /* Ctrl-C handling; 1 = unwinding   */
int   bg_status(long id);          /* status of a finished bg job / -1 */
int   func_local_options(void);    /* "local -"; 1 if not in a function */
int   caller_frame(int n, int *line, const char **file);
int   run_trap(int sig);
int   exec_subshell_node(Node *n);
int   run_child_shell(int argc, char **argv);   /* "bash ..." in-process */
int   sh_repl(ShFile *in, int interactive);
#ifdef AMIGA
int   term_main(const char *selfpath);   /* term_amiga.c: tabbed terminal */
int   term_request(const char *cmd);     /* "new"/"next"/"prev" to it     */
#endif
extern Arena *exec_arena;          /* arena of the tree being run      */
int   os_stack_ok(void);

/* ------------------------------------------------------------------ */
/* test.c                                                               */
/* ------------------------------------------------------------------ */
int   test_eval(int argc, char **argv);         /* test / [ ; 0/1/2     */
extern const char *test_cmdname;              /* "[" or "test"        */
int   cond_eval(CondNode *c);                   /* [[ ]]                */
int   test_unary(const char *op, const char *arg);  /* -1 bad op        */
int   test_binary(const char *a, const char *op, const char *b); /* -1  */
int   is_test_unop(const char *op);
int   is_test_binop(const char *op);

/* ------------------------------------------------------------------ */
/* lineedit.c                                                           */
/* ------------------------------------------------------------------ */
/* read a line interactively with editing; returns malloc'd line without
 * newline, or NULL at EOF */
char *le_readline(const char *prompt);
extern int le_cancelled;                      /* set when ^C ended the line */
void  hist_add(const char *line);
int   hist_count(void);
const char *hist_get(int i);                  /* 0 = oldest            */
int   hist_base(void);                        /* number of first entry */
void  hist_clear(void);
void  hist_delete(int i);
void  hist_load(const char *file);
void  hist_save(const char *file);
void  hist_set_max(int n);
/* history expansion (!!, !$, !n, ^a^b); returns malloc'd or NULL if
 * unchanged; *err set on failure */
char *hist_expand(const char *line, int *err, int *changed);
/* completion: fills candidates for word at pos */
void  complete_word(const char *line, int start, int end, StrList *out, int *isfile);

/* ------------------------------------------------------------------ */
/* global shell state                                                   */
/* ------------------------------------------------------------------ */
typedef struct Trap { char *action; } Trap;
#define NSIGS 32
#define TRAP_EXIT 0
#define TRAP_ERR  30
#define TRAP_DEBUG 31
#define TRAP_RETURN 29
#define TRAP_INT  2

typedef struct Shell {
    int interactive;
    int login;
    int laststatus;            /* $?                                   */
    int flow;                  /* FLOW_*                               */
    int flowcount;             /* levels for break/continue n          */
    int exitstatus;            /* status for FLOW_EXIT                 */
    int loopdepth;
    int funcdepth;
    int subshell;              /* nesting of emulated subshells        */
    int sourcedepth;
    int lineno;
    char *name;                /* $0                                   */
    char *script;              /* current script name for messages     */
    long lastbg;               /* $!                                   */
    long pid;
    long starttime;
    /* set -o options */
    int opt_errexit, opt_nounset, opt_xtrace, opt_verbose, opt_noglob,
        opt_noclobber, opt_allexport, opt_pipefail, opt_noexec,
        opt_monitor, opt_emacs, opt_vi, opt_hashall, opt_ignoreeof,
        opt_braceexpand, opt_histexpand, opt_history, opt_posix,
        opt_notify, opt_onecmd, opt_noprofile, opt_functrace, opt_errtrace;
    /* shopt */
    int sh_nocaseglob, sh_dotglob, sh_nullglob, sh_failglob, sh_extglob,
        sh_globstar, sh_expand_aliases, sh_nocasematch, sh_lastpipe,
        sh_xpg_echo, sh_cdspell, sh_checkwinsize, sh_histappend,
        sh_autocd, sh_cmdhist, sh_pathconv;
    int errexit_suppressed;    /* inside if/while condition, && || !   */
    int in_trap;
    Trap traps[NSIGS];
    int pipestatus[64]; int npipestatus;
    StrList dirstack;          /* pushd/popd                           */
    char *pwd;                 /* logical cwd (shell path)             */
    int (*getline_hook)(SBuf *line, const char *prompt);
    int umask;                 /* remembered file creation mask        */
    Trap ptraps[NSIGS];        /* in a subshell: the parent's traps,
                                  still shown by "trap -p"             */
} Shell;

extern Shell sh;

/* The resident ("pure") Amiga build addresses its data through a4, and
 * vbcc emits an absolute address whenever the address of a data object
 * lands in a data register or in memory.  NEARP() forces it through an
 * address register (lea x(a4),a0) instead.  Tables refer to Shell
 * fields by offset for the same reason. */
#if defined(AMIGA) && defined(AMIBASH_PURE)
static void *sh_nearp(__reg("a0") void *p) = "\tmove.l\ta0,d0";
#define NEARP(x) sh_nearp((void *)(x))
#else
#define NEARP(x) ((void *)(x))
#endif
#define SHOFF(f) ((int)offsetof(Shell, f))
#define SHOPT(off) ((int *)((char *)NEARP(&sh) + (off)))

void  sh_set_status(int st);
void  sh_exit(int status);        /* run EXIT trap, save history, exit   */
int   sh_chdir(const char *path, int quiet); /* updates PWD/OLDPWD      */
void  sh_set_pwd(const char *p);
const char *sh_basename_cmd(void);
char *shell_options_string(int setopts);  /* $SHELLOPTS / $BASHOPTS   */
int   sh_run_selftest(void);

#endif
