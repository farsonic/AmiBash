/*
 * os.h - operating system abstraction for AmiBash.
 *
 * Two implementations exist:
 *   os_amiga.c  - AmigaOS 2.x/3.x dos.library (the real target)
 *   os_posix.c  - Linux/POSIX host build, used for fast logic testing
 *                 against GNU bash.
 *
 * All paths passed to os_* functions are *native* paths (on the Amiga
 * already translated by path.c, e.g. "DH0:Work/foo").
 */
#ifndef AMIBASH_OS_H
#define AMIBASH_OS_H

typedef void *OsHandle;          /* BPTR on Amiga, fd+1 on POSIX     */

/* open modes */
#define OS_RD   0                /* read only, must exist            */
#define OS_WR   1                /* write, create/truncate           */
#define OS_APP  2                /* write, create, append            */
#define OS_RW   3                /* read/write, create, no truncate  */

OsHandle os_open(const char *path, int mode);
void     os_close(OsHandle h);
long     os_read(OsHandle h, void *buf, long n);
long     os_write(OsHandle h, const void *buf, long n);
long     os_seek(OsHandle h, long off, int whence); /* 0=set 1=cur 2=end; returns new pos or -1 */
OsHandle os_stdin(void);
OsHandle os_stdout(void);
OsHandle os_stderr(void);
int      os_isatty(OsHandle h);
int      os_rawmode(OsHandle h, int on);
/* wait up to usec for input; 1 = char available */
int      os_waitchar(OsHandle h, long usec);
void     os_flush(OsHandle h);

/* error string of the last failed os_* call */
const char *os_errstr(void);
int      os_errno_notfound(void);   /* last error was "object not found" */

/* directory entries */
#define OS_T_FILE 0
#define OS_T_DIR  1
#define OS_T_LINK 2
#define OS_T_VOL  3                 /* virtual root entry: volume/assign */

typedef struct OsDirEnt {
    char name[108];
    int  type;
    double size;                    /* double: >2GB safe, softfloat ok   */
    long mtime;                     /* seconds since 1970-01-01 UTC      */
    unsigned long prot;             /* see OS_P_* below                  */
    char comment[80];
} OsDirEnt;

/* protection bits, expressed positively (set = allowed) */
#define OS_P_READ   0x01
#define OS_P_WRITE  0x02
#define OS_P_EXEC   0x04
#define OS_P_DELETE 0x08
#define OS_P_SCRIPT 0x10
#define OS_P_PURE   0x20
#define OS_P_ARCH   0x40
#define OS_P_HIDDEN 0x80

void *os_opendir(const char *path);         /* NULL on error           */
int   os_readdir(void *d, OsDirEnt *e);     /* 1 = got entry, 0 = end  */
void  os_closedir(void *d);
int   os_stat(const char *path, OsDirEnt *e); /* 0 = ok                */
int   os_chdir(const char *path);           /* 0 = ok                  */
char *os_getcwd(char *buf, int n);          /* real current dir name   */
int   os_mkdir(const char *path);
int   os_remove(const char *path);          /* file or empty dir       */
int   os_rename(const char *from, const char *to);
int   os_setprot(const char *path, unsigned long prot);
int   os_setdate(const char *path, long t);
int   os_setcomment(const char *path, const char *c);
/* fully qualified name of an existing object (NameFromLock), malloc'd */
char *os_fullpath(const char *path);

/* processes */
/* Run an external program.  path is the resolved program, argv[0] is
 * the name as typed.  in/out/err are handles (never NULL).  Returns the
 * exit code, or -1 if it could not be started (os_errstr explains). */
int  os_run(const char *path, char **argv, OsHandle in, OsHandle out,
            OsHandle err);
/* start in background; returns a job/process id or -1 */
long os_run_bg(const char *path, char **argv);
/* is this file something os_run can execute? (exec bit / script bit) */
int  os_is_executable(const char *path);
/* resident command lookup (Amiga resident list); returns 1 if found  */
int  os_is_resident(const char *name);
/* enumerate resident commands, cb returns 0 to continue */
void os_list_resident(int (*cb)(const char *name, void *ud), void *ud);

/* environment (Amiga: local shell vars and ENV:) */
char *os_getenv(const char *name);          /* malloc'd or NULL        */
void  os_setenv(const char *name, const char *val);
void  os_unsetenv(const char *name);
/* enumerate environment at startup: cb(name, value) */
void  os_environ(void (*cb)(const char *n, const char *v, void *ud), void *ud);

/* misc */
int   os_check_break(void);                 /* Ctrl-C pressed? clears  */
extern int os_break_flag;                   /* set by os_check_break   */
void  os_sleep_ms(long ms);
long  os_time(void);                        /* unix epoch seconds      */
long  os_time_ms(void);                     /* monotonic-ish ms        */
long  os_tz_offset(void);                   /* local - UTC, seconds    */
long  os_pid(void);
const char *os_tmpdir(void);
const char *os_nulldev(void);         /* "NIL:" or "/dev/null" */
const char *os_hostname(void);
const char *os_username(void);
const char *os_default_home(void);
const char *os_default_path(void);
int   os_console_size(OsHandle h, int *rows, int *cols);
/* enumerate volumes/assigns/devices (virtual root), cb(name, type) */
void  os_list_volumes(int (*cb)(const char *name, int isvol, void *ud), void *ud);
/* free bytes on volume of path (or -1) */
double os_freespace(const char *path, double *total);
unsigned long os_random_seed(void);

/* console window appearance (Amiga console; -1 = not supported) */
int   os_console_getfont(char *name, int nsz, int *size);
int   os_console_setfont(const char *name, int size);   /* 0 ok, else msg in os_errstr */
int   os_console_pens(void);                            /* number of colours */
int   os_console_getpen(int pen, unsigned long *rgb);   /* rgb = 0xRRGGBB */
int   os_console_setpen(int pen, unsigned long rgb);

void  os_init(void);
void  os_cleanup(void);

#endif
