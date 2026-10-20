/*
 * io.c - shell file descriptor table.
 *
 * Shell fds 0..9 map to ShFile objects.  A ShFile is either a real OS
 * file handle (buffered), a memory buffer (used for pipelines and command
 * substitution, since we cannot fork), or the null device.  When an
 * external program needs a real handle for a memory file, the data is
 * spilled to a temporary file.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sh.h"

#define WBUFSZ 1024
#define RBUFSZ 512

ShFile *sh_fds[SH_NFD];
static long tmpcount;

static ShFile *shf_new(int kind)
{
    ShFile *f = xcalloc(sizeof(ShFile));
    f->kind = kind;
    f->refs = 1;
    return f;
}

ShFile *shf_os(OsHandle h, int owned)
{
    ShFile *f = shf_new(SHF_OS);
    f->h = h;
    f->owned = owned;
    f->tty = os_isatty(h);
    return f;
}

ShFile *shf_mem(void)
{
    return shf_new(SHF_MEM);
}

ShFile *shf_null(void)
{
    return shf_new(SHF_NULL);
}

ShFile *shf_open(const char *path, int mode)
{
    char *np = path_native(path);
    OsHandle h;
    ShFile *f;
    if (path_is_vroot(np)) {
        xfree(np);
        return NULL;
    }
    h = os_open(np, mode);
    xfree(np);
    if (!h) return NULL;
    f = shf_os(h, 1);
    if (mode == OS_APP) os_seek(h, 0, 2);
    return f;
}

ShFile *shf_ref(ShFile *f)
{
    if (f) f->refs++;
    return f;
}

void shf_flush(ShFile *f)
{
    if (f && f->kind == SHF_OS && f->wlen) {
        os_write(f->h, f->wbuf, f->wlen);
        f->wlen = 0;
    }
}

void shf_close(ShFile *f)
{
    if (!f || --f->refs > 0) return;
    if (f->kind == SHF_OS) {
        shf_flush(f);
        if (f->owned) os_close(f->h);
    }
    if (f->tmpname) {
        os_remove(f->tmpname);
        xfree(f->tmpname);
    }
    xfree(f->wbuf);
    xfree(f->rbuf);
    xfree(f->mem);
    xfree(f);
}

static void mem_append(ShFile *f, const void *buf, long n)
{
    if (f->mlen + n + 1 > f->mcap) {
        long nc = f->mcap ? f->mcap * 2 : 256;
        while (nc < f->mlen + n + 1) nc *= 2;
        f->mem = xrealloc(f->mem, nc);
        f->mcap = nc;
    }
    memcpy(f->mem + f->mlen, buf, n);
    f->mlen += n;
    f->mem[f->mlen] = 0;
}

long shf_write(ShFile *f, const void *buf, long n)
{
    if (!f) return -1;
    if (n <= 0) return 0;
    switch (f->kind) {
    case SHF_NULL:
        return n;
    case SHF_MEM:
        mem_append(f, buf, n);
        return n;
    case SHF_OS:
        if (!f->wbuf) f->wbuf = xmalloc(WBUFSZ);
        if (f->wlen + n > WBUFSZ) {
            shf_flush(f);
            if (n >= WBUFSZ) {
                long r = os_write(f->h, buf, n);
                return r;
            }
        }
        memcpy(f->wbuf + f->wlen, buf, n);
        f->wlen += (int)n;
        /* line buffered on consoles so loops show output promptly */
        if (f->tty && memchr(buf, '\n', n)) shf_flush(f);
        return n;
    }
    return -1;
}

long shf_read(ShFile *f, void *buf, long n)
{
    long got = 0;
    if (!f) return -1;
    switch (f->kind) {
    case SHF_NULL:
        return 0;
    case SHF_MEM:
        if (f->mpos >= f->mlen) return 0;
        if (n > f->mlen - f->mpos) n = f->mlen - f->mpos;
        memcpy(buf, f->mem + f->mpos, n);
        f->mpos += n;
        return n;
    case SHF_OS:
        if (f->rlen > f->rpos) {
            got = f->rlen - f->rpos;
            if (got > n) got = n;
            memcpy(buf, f->rbuf + f->rpos, got);
            f->rpos += (int)got;
            return got;
        }
        if (f->tty) sh_flush_all();
        got = os_read(f->h, buf, n);
        return got < 0 ? 0 : got;
    }
    return -1;
}

int shf_getc(ShFile *f)
{
    unsigned char c;
    if (!f) return -1;
    if (f->kind == SHF_MEM) {
        if (f->mpos >= f->mlen) return -1;
        return (unsigned char)f->mem[f->mpos++];
    }
    if (f->kind == SHF_OS) {
        if (f->rpos >= f->rlen) {
            long r;
            if (f->tty) sh_flush_all();
            if (!f->rbuf) f->rbuf = xmalloc(RBUFSZ);
            r = os_read(f->h, f->rbuf, RBUFSZ);
            if (r <= 0) return -1;
            f->rlen = (int)r;
            f->rpos = 0;
        }
        return (unsigned char)f->rbuf[f->rpos++];
    }
    return shf_read(f, &c, 1) == 1 ? c : -1;
}

int shf_getline(ShFile *f, SBuf *line, int *nl)
{
    int c, any = 0;
    sb_clear(line);
    if (nl) *nl = 0;
    if (!line->s) sb_addn(line, "", 0);
    if (f && f->kind == SHF_MEM) {
        /* fast path */
        char *s, *e;
        if (f->mpos >= f->mlen) return 0;
        s = f->mem + f->mpos;
        e = memchr(s, '\n', f->mlen - f->mpos);
        if (e) {
            sb_addn(line, s, (int)(e - s));
            f->mpos += (e - s) + 1;
            if (nl) *nl = 1;
        } else {
            sb_addn(line, s, (int)(f->mlen - f->mpos));
            f->mpos = f->mlen;
        }
        return 1;
    }
    while ((c = shf_getc(f)) != -1) {
        any = 1;
        if (c == '\n') {
            if (nl) *nl = 1;
            return 1;
        }
        sb_addc(line, c);
    }
    return any;
}

/* Spill/obtain a real OS handle for an external command */
OsHandle shf_os_handle(ShFile *f, int for_write)
{
    char name[128];
    OsHandle h;
    if (!f) return os_open(os_nulldev(), for_write ? OS_WR : OS_RD);
    if (f->kind == SHF_OS) {
        if (for_write) {
            shf_flush(f);
        } else if (f->rlen > f->rpos) {
            /* give unread buffered input back (seekable files only) */
            if (os_seek(f->h, -(long)(f->rlen - f->rpos), 1) >= 0) f->rlen = f->rpos = 0;
        }
        return f->h;
    }
    if (f->kind == SHF_NULL) {
        h = os_open(os_nulldev(), for_write ? OS_WR : OS_RD);
        f->h = h;
        return h;
    }
    /* memory file: use a temporary file */
    sprintf(name, "%samibash.%ld.%ld", os_tmpdir(), os_pid(), ++tmpcount);
    if (for_write) {
        h = os_open(name, OS_WR);
    } else {
        h = os_open(name, OS_WR);
        if (h) {
            if (f->mlen > f->mpos) os_write(h, f->mem + f->mpos, f->mlen - f->mpos);
            os_close(h);
            h = os_open(name, OS_RD);
        }
    }
    if (!h) {
        sh_error("cannot create temp file %s: %s", name, os_errstr());
        h = os_open(os_nulldev(), for_write ? OS_WR : OS_RD);
        f->h = h;
        return h;
    }
    xfree(f->tmpname);
    f->tmpname = xstrdup(name);
    f->h = h;
    return h;
}

void shf_os_done(ShFile *f, int for_write)
{
    if (!f) return;
    if (f->kind == SHF_NULL) {
        if (f->h) os_close(f->h);
        f->h = NULL;
        return;
    }
    if (f->kind != SHF_MEM || !f->tmpname) return;
    if (f->h) os_close(f->h);
    f->h = NULL;
    if (for_write) {
        OsHandle h = os_open(f->tmpname, OS_RD);
        if (h) {
            char buf[1024];
            long n;
            while ((n = os_read(h, buf, sizeof(buf))) > 0) mem_append(f, buf, n);
            os_close(h);
        }
    } else {
        f->mpos = f->mlen;
    }
    os_remove(f->tmpname);
    xfree(f->tmpname);
    f->tmpname = NULL;
}

/* ---- fd table ---- */

ShFile *sh_fd(int fd)
{
    if (fd < 0 || fd >= SH_NFD) return NULL;
    return sh_fds[fd];
}

void fd_assign(int fd, ShFile *f)
{
    if (fd < 0 || fd >= SH_NFD) {
        shf_close(f);
        return;
    }
    if (sh_fds[fd]) shf_close(sh_fds[fd]);
    sh_fds[fd] = f;
}

void fd_save(FdSave *s)
{
    int i;
    for (i = 0; i < SH_NFD; i++) s->saved[i] = shf_ref(sh_fds[i]);
}

void fd_restore(FdSave *s)
{
    int i;
    for (i = 0; i < SH_NFD; i++) {
        if (sh_fds[i]) shf_close(sh_fds[i]);
        sh_fds[i] = s->saved[i];
        s->saved[i] = NULL;
    }
}

long sh_write(int fd, const void *buf, long n)
{
    ShFile *f = sh_fd(fd);
    if (!f) return -1;
    return shf_write(f, buf, n);
}

void sh_puts(int fd, const char *s)
{
    sh_write(fd, s, (long)strlen(s));
}

void sh_putc(int fd, int c)
{
    char ch = (char)c;
    sh_write(fd, &ch, 1);
}

void sh_printf(int fd, const char *fmt, ...)
{
    SBuf b;
    va_list ap;
    sb_init(&b);
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    sh_write(fd, b.s, b.len);
    sb_free(&b);
}

long sh_read(int fd, void *buf, long n)
{
    ShFile *f = sh_fd(fd);
    if (!f) return -1;
    return shf_read(f, buf, n);
}

int sh_getline(int fd, SBuf *line, int *nl)
{
    ShFile *f = sh_fd(fd);
    if (!f) return 0;
    return shf_getline(f, line, nl);
}

void sh_flush_all(void)
{
    int i;
    for (i = 0; i < SH_NFD; i++)
        if (sh_fds[i]) shf_flush(sh_fds[i]);
}

int sh_util_running;      /* a coreutils-style builtin is running */

static void err_prefix(SBuf *b)
{
    if (sh_util_running) {
        /* like an external program: "cat: x: No such file..." */
        return;
    }
    if (!sh.interactive && sh.script) {
        sb_printf(b, "%s: line %d: ", sh.script, sh.lineno);
    } else if (!sh.interactive && sh.lineno > 0) {
        sb_printf(b, "%s: line %d: ", sh.name ? sh.name : "bash", sh.lineno);
    } else {
        sb_adds(b, "bash: ");
    }
}

void sh_error(const char *fmt, ...)
{
    SBuf b;
    va_list ap;
    sb_init(&b);
    err_prefix(&b);
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    sb_addc(&b, '\n');
    sh_flush_all();
    sh_write(2, b.s, b.len);
    shf_flush(sh_fd(2));
    sb_free(&b);
}

/* "script: ctx: line N: msg" (eval syntax errors) */
void sh_error_ctx(const char *ctx, const char *fmt, ...)
{
    SBuf b;
    va_list ap;
    sb_init(&b);
    if (sh.script && !sh.interactive) sb_printf(&b, "%s: ", sh.script);
    else sb_printf(&b, "%s: ", sh.name ? sh.name : "bash");
    sb_printf(&b, "%s: line %d: ", ctx, sh.lineno);
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    sb_addc(&b, '\n');
    sh_flush_all();
    sh_write(2, b.s, b.len);
    shf_flush(sh_fd(2));
    sb_free(&b);
}

void sh_cmd_error(const char *cmd, const char *fmt, ...)
{
    SBuf b;
    va_list ap;
    sb_init(&b);
    err_prefix(&b);
    sb_printf(&b, "%s: ", cmd);
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    sb_addc(&b, '\n');
    sh_flush_all();
    sh_write(2, b.s, b.len);
    shf_flush(sh_fd(2));
    sb_free(&b);
}

void io_init(void)
{
    int i;
    for (i = 0; i < SH_NFD; i++) sh_fds[i] = NULL;
    sh_fds[0] = shf_os(os_stdin(), 0);
    sh_fds[1] = shf_os(os_stdout(), 0);
    sh_fds[2] = shf_os(os_stderr(), 0);
}
