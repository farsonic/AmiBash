/*
 * utils.h - private helpers shared by the built-in utilities
 * (utils_fs.c, utils_text.c, utils_misc.c, timeutil.c).
 */
#ifndef AMIBASH_UTILS_H
#define AMIBASH_UTILS_H

#include "sh.h"

/* ---- GNU style option parser (permutes operands) ----
 * Short options: "ab:c::" (':' = required argument, '::' = optional,
 * attached only).  Long options: NULL terminated array, a trailing '='
 * means the option takes an argument ("--x=v" or "--x v"), a trailing
 * '?' means an optional argument ("--x" or "--x=v").
 * ug_next() returns the option char, UG_LONG+index for long options,
 * UG_NUM for "-123" when allow_num is set (arg = digits), '?' after
 * printing an error, or -1 at the end.  Operands are collected in ops. */
#define UG_LONG 0x100
#define UG_NUM  0x200
typedef struct UGetopt {
    int argc; char **argv;
    int i, sp;
    const char *opts;
    const char *const *lopts;
    const char *cmd;
    char *arg;
    int allow_num;          /* -N returns UG_NUM                         */
    int stop_nonopt;        /* POSIX: stop at first operand              */
    int done;
    char **ops; int nops;   /* operands (point into argv)                */
} UGetopt;
void ug_init(UGetopt *g, int argc, char **argv, const char *opts, const char *const *lopts);
int  ug_next(UGetopt *g);
void ug_free(UGetopt *g);

/* misc */
int   u_usage(const char *cmd);                 /* prints usage, returns 2 */
int   u_atoi(const char *s, long *out);          /* 1 ok                    */
char *u_size_str(double sz, int human);          /* malloc'd                */
char *u_ultoa(unsigned long v);                  /* static buffer           */
int   u_break(void);                              /* poll Ctrl-C (sticky)    */
void  u_break_reset(void);
ShFile *u_open_in(const char *cmd, const char *path); /* "-" = stdin; error printed */
void  u_close_in(ShFile *f);
int   u_copy_file(const char *cmd, const char *from, const char *to, int preserve);
int   u_remove_tree(const char *cmd, const char *path, int verbose);
int   u_copy_tree(const char *cmd, const char *from, const char *to, int preserve, int verbose);
int   u_out_is_tty(int *cols);
/* stable merge sort of pointers */
typedef int (*UCmp)(const void *a, const void *b, void *ud);
void  u_msort(void **v, int n, UCmp cmp, void *ud);
int   u_parse_date(const char *s, int utc, long *out);   /* 0 ok */

/* timeutil.c extras */
void   sh_gmtime(long t, ShTm *tm);
size_t sh_strftime_tz(char *buf, size_t n, const char *fmt, const ShTm *tm, long off, int utc);
long   sh_timegm(const ShTm *tm);

/* utility entry points */
#define UDECL(x) int util_##x(int argc, char **argv)
UDECL(ls); UDECL(cp); UDECL(mv); UDECL(rm); UDECL(mkdir); UDECL(rmdir);
UDECL(touch); UDECL(chmod); UDECL(find); UDECL(du); UDECL(df); UDECL(ln);
UDECL(realpath); UDECL(stat);
UDECL(cat); UDECL(head); UDECL(tail); UDECL(wc); UDECL(grep); UDECL(sort);
UDECL(uniq); UDECL(cut); UDECL(tr); UDECL(sed); UDECL(tee); UDECL(seq);
UDECL(rev); UDECL(nl); UDECL(paste); UDECL(tac);
UDECL(basename); UDECL(dirname); UDECL(sleep); UDECL(date); UDECL(env);
UDECL(printenv); UDECL(clear); UDECL(xargs); UDECL(uname); UDECL(whoami);
UDECL(hostname); UDECL(acopy); UDECL(apaste);

#endif
