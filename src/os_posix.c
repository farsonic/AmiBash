/*
 * os_posix.c - POSIX implementation of the OS layer (host test build).
 */
#ifndef AMIGA
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <utime.h>
#include <pwd.h>
#include "os.h"
#include "sh.h"

extern char **environ;

static int last_errno;
static volatile sig_atomic_t got_int;
static struct termios saved_tio;
static int raw_on;

#define H2FD(h) ((int)(long)(h) - 1)
#define FD2H(fd) ((OsHandle)(long)((fd) + 1))

static void on_int(int s)
{
    (void)s;
    got_int = 1;
}

void os_init(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_int;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
}

void os_cleanup(void)
{
    if (raw_on) os_rawmode(os_stdin(), 0);
}

OsHandle os_open(const char *path, int mode)
{
    int fl, fd;
    switch (mode) {
    case OS_RD: fl = O_RDONLY; break;
    case OS_WR: fl = O_WRONLY | O_CREAT | O_TRUNC; break;
    case OS_APP: fl = O_WRONLY | O_CREAT | O_APPEND; break;
    default: fl = O_RDWR | O_CREAT; break;
    }
    fd = open(path, fl, 0666);
    if (fd < 0) {
        last_errno = errno;
        return NULL;
    }
    if (mode == OS_RD) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            close(fd);
            last_errno = EISDIR;
            return NULL;
        }
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return FD2H(fd);
}

void os_close(OsHandle h)
{
    if (h) close(H2FD(h));
}

long os_read(OsHandle h, void *buf, long n)
{
    long r;
    do {
        r = read(H2FD(h), buf, n);
    } while (r < 0 && errno == EINTR && !got_int);
    if (r < 0) last_errno = errno;
    return r;
}

long os_write(OsHandle h, const void *buf, long n)
{
    long done = 0;
    while (done < n) {
        long r = write(H2FD(h), (const char *)buf + done, n - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            last_errno = errno;
            return done ? done : -1;
        }
        done += r;
    }
    return done;
}

long os_seek(OsHandle h, long off, int whence)
{
    off_t r = lseek(H2FD(h), off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    if (r < 0) last_errno = errno;
    return (long)r;
}

OsHandle os_stdin(void) { return FD2H(0); }
OsHandle os_stdout(void) { return FD2H(1); }
OsHandle os_stderr(void) { return FD2H(2); }

int os_isatty(OsHandle h)
{
    return isatty(H2FD(h));
}

int os_rawmode(OsHandle h, int on)
{
    int fd = H2FD(h);
    if (on) {
        struct termios t;
        if (tcgetattr(fd, &saved_tio) < 0) return -1;
        t = saved_tio;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(IXON | ICRNL);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSADRAIN, &t);
        raw_on = 1;
    } else if (raw_on) {
        tcsetattr(fd, TCSADRAIN, &saved_tio);
        raw_on = 0;
    }
    return 0;
}

int os_waitchar(OsHandle h, long usec)
{
    fd_set s;
    struct timeval tv;
    FD_ZERO(&s);
    FD_SET(H2FD(h), &s);
    tv.tv_sec = usec / 1000000;
    tv.tv_usec = usec % 1000000;
    return select(H2FD(h) + 1, &s, NULL, NULL, &tv) > 0;
}

void os_flush(OsHandle h)
{
    (void)h;
}

const char *os_errstr(void)
{
    return strerror(last_errno);
}

int os_errno_notfound(void)
{
    return last_errno == ENOENT;
}

static void fill_ent(OsDirEnt *e, const char *name, struct stat *st)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->type = S_ISDIR(st->st_mode) ? OS_T_DIR : S_ISLNK(st->st_mode) ? OS_T_LINK : OS_T_FILE;
    e->size = (double)st->st_size;
    e->mtime = (long)st->st_mtime;
    e->prot = OS_P_DELETE;
    if (st->st_mode & S_IRUSR) e->prot |= OS_P_READ;
    if (st->st_mode & S_IWUSR) e->prot |= OS_P_WRITE;
    if (st->st_mode & S_IXUSR) e->prot |= OS_P_EXEC;
}

typedef struct PDir { DIR *d; char *path; } PDir;

void *os_opendir(const char *path)
{
    PDir *p;
    DIR *d = opendir(*path ? path : ".");
    if (!d) {
        last_errno = errno;
        return NULL;
    }
    p = xmalloc(sizeof(PDir));
    p->d = d;
    p->path = xstrdup(*path ? path : ".");
    return p;
}

int os_readdir(void *dp, OsDirEnt *e)
{
    PDir *p = dp;
    struct dirent *de;
    while ((de = readdir(p->d)) != NULL) {
        struct stat st;
        char *full;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        full = path_join(p->path, de->d_name);
        if (stat(full, &st) != 0 && lstat(full, &st) != 0) memset(&st, 0, sizeof(st));
        xfree(full);
        fill_ent(e, de->d_name, &st);
        return 1;
    }
    return 0;
}

void os_closedir(void *dp)
{
    PDir *p = dp;
    if (!p) return;
    closedir(p->d);
    xfree(p->path);
    xfree(p);
}

int os_stat(const char *path, OsDirEnt *e)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        last_errno = errno;
        return -1;
    }
    fill_ent(e, path_basename(path), &st);
    return 0;
}

int os_chdir(const char *path)
{
    if (chdir(path) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

char *os_getcwd(char *buf, int n)
{
    return getcwd(buf, n);
}

int os_mkdir(const char *path)
{
    if (mkdir(path, 0777) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

int os_remove(const char *path)
{
    if (remove(path) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

int os_rename(const char *from, const char *to)
{
    if (rename(from, to) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

int os_setprot(const char *path, unsigned long prot)
{
    mode_t m = 0;
    if (prot & OS_P_READ) m |= 0444;
    if (prot & OS_P_WRITE) m |= 0200;
    if (prot & OS_P_EXEC) m |= 0111;
    if (chmod(path, m) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

int os_setdate(const char *path, long t)
{
    struct utimbuf u;
    u.actime = u.modtime = t;
    if (utime(path, &u) != 0) {
        last_errno = errno;
        return -1;
    }
    return 0;
}

int os_setcomment(const char *path, const char *c)
{
    (void)path;
    (void)c;
    return 0;
}

char *os_fullpath(const char *path)
{
    char *r = realpath(path, NULL);
    if (!r) {
        last_errno = errno;
        return NULL;
    }
    {
        char *x = xstrdup(r);
        free(r);
        return x;
    }
}

int os_run(const char *path, char **argv, OsHandle in, OsHandle out, OsHandle err)
{
    pid_t pid;
    int st;
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        last_errno = errno;
        return -1;
    }
    if (pid == 0) {
        int fi = H2FD(in), fo = H2FD(out), fe = H2FD(err);
        /* duplicate high first to avoid clobbering */
        int ti = dup(fi), to = dup(fo), te = dup(fe);
        dup2(ti, 0);
        dup2(to, 1);
        dup2(te, 2);
        close(ti);
        close(to);
        close(te);
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execv(path, argv);
        if (errno == ENOEXEC) {
            /* script without #!: run with /bin/sh like bash does */
            int n = 0;
            char **nv;
            while (argv[n]) n++;
            nv = malloc((n + 2) * sizeof(char *));
            nv[0] = "/bin/sh";
            nv[1] = (char *)path;
            memcpy(nv + 2, argv + 1, n * sizeof(char *));
            execv("/bin/sh", nv);
        }
        _exit(126);
    }
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    got_int = 0;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 1;
}

long os_run_bg(const char *path, char **argv)
{
    pid_t pid = fork();
    if (pid < 0) {
        last_errno = errno;
        return -1;
    }
    if (pid == 0) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) dup2(fd, 0);
        execv(path, argv);
        _exit(127);
    }
    return (long)pid;
}

int os_is_executable(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

int os_is_resident(const char *name)
{
    (void)name;
    return 0;
}

void os_list_resident(int (*cb)(const char *name, void *ud), void *ud)
{
    (void)cb;
    (void)ud;
}

char *os_getenv(const char *name)
{
    char *v = getenv(name);
    return v ? xstrdup(v) : NULL;
}

void os_setenv(const char *name, const char *val)
{
    setenv(name, val, 1);
}

void os_unsetenv(const char *name)
{
    unsetenv(name);
}

void os_environ(void (*cb)(const char *n, const char *v, void *ud), void *ud)
{
    char **e;
    for (e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (eq) {
            char *n = xstrndup(*e, eq - *e);
            cb(n, eq + 1, ud);
            xfree(n);
        }
    }
}

int os_break_flag;

int os_check_break(void)
{
    if (got_int) {
        got_int = 0;
        os_break_flag = 1;   /* sticky until the shell consumes it */
        return 1;
    }
    return 0;
}

void os_sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR && !got_int)
        ;
}

long os_time(void)
{
    return (long)time(NULL);
}

long os_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000);
}

long os_tz_offset(void)
{
    time_t t = time(NULL);
    struct tm lt = *localtime(&t);
    return (long)lt.tm_gmtoff;
}

long os_pid(void)
{
    return (long)getpid();
}

const char *os_tmpdir(void)
{
    return "/tmp/";
}

const char *os_nulldev(void)
{
    return "/dev/null";
}

const char *os_hostname(void)
{
    static char buf[64];
    if (gethostname(buf, sizeof(buf)) != 0) strcpy(buf, "localhost");
    return buf;
}

const char *os_username(void)
{
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "user";
}

const char *os_default_home(void)
{
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/";
}

const char *os_default_path(void)
{
    return "/usr/local/bin:/usr/bin:/bin";
}

int os_console_size(OsHandle h, int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(H2FD(h), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
    *rows = 24;
    *cols = 80;
    return -1;
}

void os_list_volumes(int (*cb)(const char *name, int isvol, void *ud), void *ud)
{
    (void)cb;
    (void)ud;
}

double os_freespace(const char *path, double *total)
{
    (void)path;
    if (total) *total = -1;
    return -1;
}

int os_console_getfont(char *name, int nsz, int *size)
{
    (void)name; (void)nsz; (void)size;
    return -1;
}

int os_console_setfont(const char *name, int size)
{
    (void)name; (void)size;
    last_errno = ENOTSUP;
    return -1;
}

int os_console_pens(void)
{
    return 8;
}

int os_console_getpen(int pen, unsigned long *rgb)
{
    (void)pen; (void)rgb;
    return -1;
}

int os_console_setpen(int pen, unsigned long rgb)
{
    /* xterm-compatible terminals: OSC 4 */
    char b[64];
    int n = snprintf(b, sizeof(b), "\033]4;%d;rgb:%02lx/%02lx/%02lx\033\\", pen,
                     (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    return write(1, b, n) == n ? 0 : -1;
}

int os_wb_launch(void) { return 0; }
int os_new_shell_window(void) { last_errno = ENOTSUP; return -1; }
int os_next_shell_window(void) { last_errno = ENOTSUP; return -1; }
void os_console_attach(void) { }
void os_console_detach(void) { }

int os_stack_ok(void)
{
    return 1;
}

int os_run_native(const char *cmdline, OsHandle in, OsHandle out)
{
    char *argv[4];
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = (char *)cmdline;
    argv[3] = NULL;
    return os_run("/bin/sh", argv, in, out, out);
}

unsigned long os_random_seed(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_usec ^ (tv.tv_sec << 8) ^ getpid());
}
#endif
