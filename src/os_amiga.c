/*
 * os_amiga.c - AmigaOS (2.04+ / 3.x, AROS) implementation of the OS
 * layer, using dos.library directly.  No ixemul or POSIX emulation.
 *
 * External programs are run with RunCommand() on this process (like the
 * AmigaDOS Shell does), so Ctrl-C, the console and the current directory
 * behave exactly as Amiga users expect.  Scripts with the S bit are
 * handed to the AmigaDOS shell via SystemTags().
 */
#ifdef AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <dos/datetime.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/intuition.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/icon.h>
#include <diskfont/diskfont.h>
#include <devices/conunit.h>
#include <exec/io.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "os.h"
#include "sh.h"

static LONG last_err;
static BPTR orig_cwd;
static int cwd_changed;
static char tmpdir[16] = "T:";
static long bgcount;
static APTR old_winptr;

#define B2H(b) ((OsHandle)(b))
#define H2B(h) ((BPTR)(h))

static struct Process *me(void)
{
    return (struct Process *)FindTask(NULL);
}

void os_init(void)
{
    BPTR l;
    /* no "Please insert volume" requesters for the shell's own lookups */
    old_winptr = me()->pr_WindowPtr;
    me()->pr_WindowPtr = (APTR)-1;
    l = Lock("T:", SHARED_LOCK);
    if (l) UnLock(l);
    else strcpy(tmpdir, "RAM:");
    {
        /* a tab of the AmiBash Terminal inherits the terminal's error
         * stream (Kickstart 3.1 ignores NP_Error): use the tab instead */
        struct Process *p = me();
        struct FileHandle *in = (struct FileHandle *)BADDR(Input());
        struct FileHandle *ces = (struct FileHandle *)BADDR(p->pr_CES);
        char b[4];
        if (ces && in && in->fh_Type == p->pr_ConsoleTask && ces->fh_Type != p->pr_ConsoleTask &&
            GetVar((STRPTR)"AMIBASH_TERM", (STRPTR)b, sizeof(b), GVF_LOCAL_ONLY) >= 0)
            p->pr_CES = 0;
    }
}

void os_cleanup(void)
{
    /* our_font stays open: the console window may still be using it */
    me()->pr_WindowPtr = old_winptr;
    if (cwd_changed) {
        BPTR mine = CurrentDir(orig_cwd);
        UnLock(mine);
        cwd_changed = 0;
    }
    if (IsInteractive(Input())) SetMode(Input(), 0);
}

void os_exit(int status)
{
#ifdef AMIBASH_PURE
    void pure_exit(int);
    pure_exit(status);
#else
    exit(status);
#endif
}

/* ------------------------------------------------------------------ */
/* files                                                                */

OsHandle os_open(const char *path, int mode)
{
    BPTR fh;
    LONG m = mode == OS_RD ? MODE_OLDFILE : mode == OS_WR ? MODE_NEWFILE : MODE_READWRITE;
    fh = Open((STRPTR)path, m);
    if (!fh) {
        last_err = IoErr();
        return NULL;
    }
    if (mode == OS_APP) Seek(fh, 0, OFFSET_END);
    return B2H(fh);
}

void os_close(OsHandle h)
{
    if (h) Close(H2B(h));
}

/* keys typed while we waited for a console report are kept here */
static char pushback[64];
static int npush;
static BPTR raw_fh;        /* console currently in raw mode */

long os_read(OsHandle h, void *buf, long n)
{
    LONG r;
    if (npush > 0 && H2B(h) == Input()) {
        if (n > npush) n = npush;
        memcpy(buf, pushback, n);
        memmove(pushback, pushback + n, npush - n);
        npush -= (int)n;
        return n;
    }
    if (raw_fh && H2B(h) == raw_fh) {
        /* raw console: AROS may return 0 instead of blocking */
        while (!WaitForChar(raw_fh, 1000000L))
            ;
    }
    r = Read(H2B(h), buf, n);
    if (r < 0) last_err = IoErr();
    return r;
}

long os_write(OsHandle h, const void *buf, long n)
{
    LONG r = Write(H2B(h), (APTR)buf, n);
    if (r < 0) last_err = IoErr();
    return r;
}

long os_seek(OsHandle h, long off, int whence)
{
    LONG mode = whence == 0 ? OFFSET_BEGINNING : whence == 1 ? OFFSET_CURRENT : OFFSET_END;
    if (Seek(H2B(h), off, mode) < 0) {
        last_err = IoErr();
        return -1;
    }
    return Seek(H2B(h), 0, OFFSET_CURRENT);
}

OsHandle os_stdin(void) { return B2H(Input()); }
OsHandle os_stdout(void) { return B2H(Output()); }

OsHandle os_stderr(void)
{
    BPTR e = me()->pr_CES;
    return B2H(e ? e : Output());
}

int os_isatty(OsHandle h)
{
    return h && IsInteractive(H2B(h)) ? 1 : 0;
}

int os_rawmode(OsHandle h, int on)
{
    LONG ok = SetMode(H2B(h), on ? 1 : 0);
    raw_fh = on && ok ? H2B(h) : 0;
    return ok ? 0 : -1;
}

int os_waitchar(OsHandle h, long usec)
{
    if (npush > 0 && H2B(h) == Input()) return 1;
    return WaitForChar(H2B(h), usec) ? 1 : 0;
}

void os_flush(OsHandle h)
{
    (void)h;
}

const char *os_errstr(void)
{
    static char buf[100];
    if (!last_err) return "Unknown error";
    if (last_err == ERROR_OBJECT_NOT_FOUND) return "No such file or directory";
    if (last_err == ERROR_DIR_NOT_FOUND) return "No such file or directory";
    if (last_err == ERROR_OBJECT_WRONG_TYPE) return "Not a directory";
    if (last_err == ERROR_WRITE_PROTECTED || last_err == ERROR_DELETE_PROTECTED ||
        last_err == ERROR_READ_PROTECTED || last_err == ERROR_DISK_WRITE_PROTECTED)
        return "Permission denied";
    if (last_err == ERROR_OBJECT_EXISTS) return "File exists";
    if (last_err == ERROR_DIRECTORY_NOT_EMPTY) return "Directory not empty";
    if (last_err == ERROR_OBJECT_IN_USE) return "Object in use";
    if (last_err == ERROR_DISK_FULL) return "No space left on device";
    Fault(last_err, NULL, (STRPTR)NEARP(buf), sizeof(buf));
    return NEARP(buf);
}

int os_errno_notfound(void)
{
    return last_err == ERROR_OBJECT_NOT_FOUND || last_err == ERROR_DIR_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* directories                                                          */

static long ds_to_epoch(const struct DateStamp *ds)
{
    return (ds->ds_Days + 2922L) * 86400L + ds->ds_Minute * 60L + ds->ds_Tick / TICKS_PER_SECOND;
}

static void epoch_to_ds(long t, struct DateStamp *ds)
{
    long s = t - 2922L * 86400L;
    if (s < 0) s = 0;
    ds->ds_Days = s / 86400L;
    s %= 86400L;
    ds->ds_Minute = s / 60;
    ds->ds_Tick = (s % 60) * TICKS_PER_SECOND;
}

static unsigned long prot_from_fib(LONG p)
{
    unsigned long r = 0;
    if (!(p & FIBF_READ)) r |= OS_P_READ;
    if (!(p & FIBF_WRITE)) r |= OS_P_WRITE;
    if (!(p & FIBF_EXECUTE)) r |= OS_P_EXEC;
    if (!(p & FIBF_DELETE)) r |= OS_P_DELETE;
    if (p & FIBF_SCRIPT) r |= OS_P_SCRIPT;
    if (p & FIBF_PURE) r |= OS_P_PURE;
    if (p & FIBF_ARCHIVE) r |= OS_P_ARCH;
    if (p & (1L << 7)) r |= OS_P_HIDDEN;
    return r;
}

static LONG prot_to_fib(unsigned long r)
{
    LONG p = 0;
    if (!(r & OS_P_READ)) p |= FIBF_READ;
    if (!(r & OS_P_WRITE)) p |= FIBF_WRITE;
    if (!(r & OS_P_EXEC)) p |= FIBF_EXECUTE;
    if (!(r & OS_P_DELETE)) p |= FIBF_DELETE;
    if (r & OS_P_SCRIPT) p |= FIBF_SCRIPT;
    if (r & OS_P_PURE) p |= FIBF_PURE;
    if (r & OS_P_ARCH) p |= FIBF_ARCHIVE;
    if (r & OS_P_HIDDEN) p |= (1L << 7);
    return p;
}

static void fill_ent(OsDirEnt *e, struct FileInfoBlock *fib)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->name, (char *)fib->fib_FileName, sizeof(e->name) - 1);
    if (fib->fib_DirEntryType == ST_SOFTLINK) e->type = OS_T_LINK;
    else if (fib->fib_DirEntryType > 0) e->type = OS_T_DIR;
    else e->type = OS_T_FILE;
    e->size = (double)(unsigned long)fib->fib_Size;
    e->mtime = ds_to_epoch(&fib->fib_Date);
    e->prot = prot_from_fib(fib->fib_Protection);
    strncpy(e->comment, (char *)fib->fib_Comment, sizeof(e->comment) - 1);
}

typedef struct ADir {
    BPTR lock;
    struct FileInfoBlock *fib;
    StrList vols;           /* virtual root */
    int vidx;
    int vroot;
} ADir;

static void bstr_copy(char *dst, BSTR b, int max)
{
    UBYTE *s = (UBYTE *)BADDR(b);
    int n = s[0];
    if (n >= max) n = max - 1;
    memcpy(dst, s + 1, n);
    dst[n] = 0;
}

void os_list_volumes(int (*cb)(const char *name, int isvol, void *ud), void *ud)
{
    struct DosList *dl;
    StrList names;
    StrList types;
    int i;
    char nm[108];
    sl_init(&names);
    sl_init(&types);
    dl = LockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES)) != NULL) {
        bstr_copy(nm, dl->dol_Name, sizeof(nm));
        if (dl->dol_Type == DLT_DEVICE) {
            /* only devices with a running file system */
            if (!dl->dol_Task) continue;
            sl_adddup(&types, "d");
        } else if (dl->dol_Type == DLT_VOLUME) {
            sl_adddup(&types, "v");
        } else {
            sl_adddup(&types, "a");
        }
        sl_adddup(&names, nm);
    }
    UnLockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES | LDF_READ);
    for (i = 0; i < names.n; i++) {
        if (types.v[i][0] == 'd') {
            char *dn = str_cat(names.v[i], ":");
            int fs = IsFileSystem((STRPTR)dn);
            xfree(dn);
            if (!fs) continue;
        }
        if (cb(names.v[i], types.v[i][0] == 'v', ud)) break;
    }
    sl_free(&names);
    sl_free(&types);
}

static int vol_collect(const char *name, int isvol, void *ud)
{
    StrList *l = ud;
    int i;
    (void)isvol;
    for (i = 0; i < l->n; i++)
        if (str_casecmp(l->v[i], name) == 0) return 0;
    sl_adddup(l, name);
    return 0;
}

void *os_opendir(const char *path)
{
    ADir *d;
    if (path[0] == '/' && path[1] == 0) {
        d = xcalloc(sizeof(ADir));
        d->vroot = 1;
        sl_init(&d->vols);
        os_list_volumes(vol_collect, &d->vols);
        sl_sort(&d->vols, 1);
        return d;
    }
    {
        BPTR l = Lock((STRPTR)(*path ? path : ""), SHARED_LOCK);
        struct FileInfoBlock *fib;
        if (!l) {
            last_err = IoErr();
            return NULL;
        }
        fib = AllocDosObject(DOS_FIB, NULL);
        if (!fib || !Examine(l, fib) || fib->fib_DirEntryType <= 0 || fib->fib_DirEntryType == ST_SOFTLINK) {
            last_err = fib ? ERROR_OBJECT_WRONG_TYPE : ERROR_NO_FREE_STORE;
            if (fib) FreeDosObject(DOS_FIB, fib);
            UnLock(l);
            return NULL;
        }
        d = xcalloc(sizeof(ADir));
        d->lock = l;
        d->fib = fib;
        return d;
    }
}

int os_readdir(void *dp, OsDirEnt *e)
{
    ADir *d = dp;
    if (d->vroot) {
        if (d->vidx >= d->vols.n) return 0;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, d->vols.v[d->vidx++], sizeof(e->name) - 1);
        e->type = OS_T_VOL;
        e->prot = OS_P_READ | OS_P_WRITE | OS_P_EXEC;
        return 1;
    }
    if (!ExNext(d->lock, d->fib)) {
        last_err = IoErr();
        return 0;
    }
    fill_ent(e, d->fib);
    return 1;
}

void os_closedir(void *dp)
{
    ADir *d = dp;
    if (!d) return;
    if (d->vroot) sl_free(&d->vols);
    if (d->fib) FreeDosObject(DOS_FIB, d->fib);
    if (d->lock) UnLock(d->lock);
    xfree(d);
}

int os_stat(const char *path, OsDirEnt *e)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK);
    struct FileInfoBlock *fib;
    int r = -1;
    if (!l) {
        last_err = IoErr();
        return -1;
    }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(l, fib)) {
        fill_ent(e, fib);
        r = 0;
    } else {
        last_err = IoErr();
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(l);
    return r;
}

int os_chdir(const char *path)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK), old;
    struct FileInfoBlock *fib;
    int isdir = 0;
    if (!l) {
        last_err = IoErr();
        return -1;
    }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(l, fib)) isdir = fib->fib_DirEntryType > 0;
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (!isdir) {
        UnLock(l);
        last_err = ERROR_OBJECT_WRONG_TYPE;
        return -1;
    }
    old = CurrentDir(l);
    if (!cwd_changed) {
        orig_cwd = old;      /* belongs to whoever started us */
        cwd_changed = 1;
    } else {
        UnLock(old);
    }
    return 0;
}

char *os_getcwd(char *buf, int n)
{
    BPTR l = me()->pr_CurrentDir;
    if (!l) {
        strncpy(buf, "SYS:", n);
        return buf;
    }
    if (!NameFromLock(l, (STRPTR)buf, n)) return NULL;
    return buf;
}

int os_mkdir(const char *path)
{
    BPTR l = CreateDir((STRPTR)path);
    if (!l) {
        last_err = IoErr();
        return -1;
    }
    UnLock(l);
    return 0;
}

int os_remove(const char *path)
{
    if (!DeleteFile((STRPTR)path)) {
        last_err = IoErr();
        return -1;
    }
    return 0;
}

int os_rename(const char *from, const char *to)
{
    if (!Rename((STRPTR)from, (STRPTR)to)) {
        last_err = IoErr();
        return -1;
    }
    return 0;
}

int os_setprot(const char *path, unsigned long prot)
{
    if (!SetProtection((STRPTR)path, prot_to_fib(prot))) {
        last_err = IoErr();
        return -1;
    }
    return 0;
}

int os_setdate(const char *path, long t)
{
    struct DateStamp ds;
    epoch_to_ds(t, &ds);
    if (!SetFileDate((STRPTR)path, &ds)) {
        last_err = IoErr();
        return -1;
    }
    return 0;
}

int os_setcomment(const char *path, const char *c)
{
    if (!SetComment((STRPTR)path, (STRPTR)c)) {
        last_err = IoErr();
        return -1;
    }
    return 0;
}

char *os_fullpath(const char *path)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK);
    char buf[512];
    if (!l) {
        last_err = IoErr();
        return NULL;
    }
    if (!NameFromLock(l, (STRPTR)NEARP(buf), sizeof(buf))) {
        last_err = IoErr();
        UnLock(l);
        return NULL;
    }
    UnLock(l);
    return xstrdup(buf);
}

/* ------------------------------------------------------------------ */
/* running programs                                                     */

/* quote one argument for ReadArgs() */
static void quote_arg(SBuf *b, const char *a, int for_shell)
{
    const char *p;
    int need = !*a;
    for (p = a; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '"' || *p == '*' || *p == ';' || *p == '\n' ||
            *p == 27 || (for_shell && (*p == '>' || *p == '<' || *p == '`' || *p == '$' || *p == '|')))
            need = 1;
    if (!need) {
        sb_adds(b, a);
        return;
    }
    sb_addc(b, '"');
    for (p = a; *p; p++) {
        if (*p == '"') sb_adds(b, "*\"");
        else if (*p == '*') sb_adds(b, "**");
        else if (*p == '\n') sb_adds(b, "*N");
        else if (*p == 27) sb_adds(b, "*E");
        else sb_addc(b, *p);
    }
    sb_addc(b, '"');
}

static char *build_args(char **argv, int for_shell)
{
    SBuf b;
    int i;
    sb_init(&b);
    sb_addn(&b, "", 0);
    for (i = 1; argv[i]; i++) {
        if (i > 1) sb_addc(&b, ' ');
        quote_arg(&b, argv[i], for_shell);
    }
    return sb_take(&b);
}

static LONG file_protection(const char *path, int *isfile)
{
    OsDirEnt e;
    *isfile = 0;
    if (os_stat(path, &e) != 0) return -1;
    *isfile = e.type == OS_T_FILE;
    return (LONG)e.prot;
}

/* after a command ran on our process it may have changed the current
 * directory (e.g. the Amiga "CD" command): keep $PWD in sync */
static BPTR cwd_before;

static void sync_cwd_after(void)
{
    BPTR now = me()->pr_CurrentDir;
    if (now != cwd_before) {
        char buf[512];
        if (now && NameFromLock(now, (STRPTR)NEARP(buf), sizeof(buf))) sh_set_pwd(buf);
    }
}

int os_run(const char *path, char **argv, OsHandle in, OsHandle out, OsHandle err)
{
    struct Process *pr = me();
    struct CommandLineInterface *cli = Cli();
    struct Segment *res = NULL;
    BPTR seg = 0;
    LONG rc;
    char *args;
    int isfile = 0;
    LONG prot;

    if (!strchr(path, ':') && !strchr(path, '/')) {
        Forbid();
        res = FindSegment((STRPTR)path, NULL, 0);
        if (!res) res = FindSegment((STRPTR)path, NULL, 1);
        if (res && res->seg_UC == CMD_DISABLED) res = NULL;
        if (res && res->seg_UC >= 0) res->seg_UC++;
        Permit();
        if (res) seg = res->seg_Seg;
    }
    if (!seg) {
        prot = file_protection(path, &isfile);
        if (prot >= 0 && (prot & OS_P_SCRIPT)) {
            /* AmigaDOS script: let the native shell Execute it */
            SBuf line;
            char *a = build_args(argv, 1);
            sb_init(&line);
            sb_adds(&line, "Execute ");
            quote_arg(&line, path, 1);
            if (*a) {
                sb_addc(&line, ' ');
                sb_adds(&line, a);
            }
            xfree(a);
            cwd_before = pr->pr_CurrentDir;
            rc = SystemTags((STRPTR)line.s, SYS_Input, H2B(in), SYS_Output, H2B(out),
                            SYS_UserShell, TRUE, TAG_END);
            sb_free(&line);
            return rc;
        }
        seg = LoadSeg((STRPTR)path);
        if (!seg) {
            last_err = IoErr();
            if (!last_err) last_err = isfile ? ERROR_FILE_NOT_OBJECT : ERROR_OBJECT_NOT_FOUND;
            return -1;
        }
    }
    args = build_args(argv, 0);
    {
        size_t al = strlen(args);
        char *line = xmalloc(al + 2);
        BPTR oldin, oldout, olderr, oldmod = 0;
        char oldname[128];
        LONG stack = 8192;
        memcpy(line, args, al);
        line[al] = '\n';
        line[al + 1] = 0;
        if (cli) {
            stack = cli->cli_DefaultStack * 4;
            if (stack < 8192) stack = 8192;
            oldmod = cli->cli_Module;
            cli->cli_Module = seg;
        }
        oldname[0] = 0;
        GetProgramName((STRPTR)oldname, sizeof(oldname));
        SetProgramName((STRPTR)(res ? path : path_basename(path)));
        oldin = SelectInput(H2B(in));
        oldout = SelectOutput(H2B(out));
        olderr = pr->pr_CES;
        pr->pr_CES = H2B(err);
        cwd_before = pr->pr_CurrentDir;
        pr->pr_WindowPtr = old_winptr;
        rc = RunCommand(seg, stack, (STRPTR)line, (LONG)(al + 1));
        /* as the Shell does: write out what the command left buffered */
        Flush(Output());
        if (pr->pr_CES) Flush(pr->pr_CES);
        pr->pr_WindowPtr = (APTR)-1;
        if (cli) {
            cli->cli_ReturnCode = rc;
            cli->cli_Result2 = IoErr();
            cli->cli_Module = oldmod;
        }
        pr->pr_CES = olderr;
        SelectInput(oldin);
        SelectOutput(oldout);
        SetProgramName((STRPTR)oldname);
        xfree(line);
        sync_cwd_after();
    }
    xfree(args);
    if (res) {
        Forbid();
        if (res->seg_UC > 0) res->seg_UC--;
        Permit();
    } else {
        UnLoadSeg(seg);
    }
    if (rc == -1) {
        last_err = ERROR_NO_FREE_STORE;
        return -1;
    }
    return (int)rc;
}

long os_run_bg(const char *path, char **argv)
{
    SBuf line;
    char *a = build_args(argv, 1);
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    BPTR out = Open((STRPTR)"*", MODE_NEWFILE);
    LONG r;
    sb_init(&line);
    quote_arg(&line, path, 1);
    if (*a) {
        sb_addc(&line, ' ');
        sb_adds(&line, a);
    }
    xfree(a);
    if (!out) out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    r = SystemTags((STRPTR)line.s, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                   SYS_UserShell, TRUE, TAG_END);
    sb_free(&line);
    if (r == -1) {
        Close(in);
        Close(out);
        last_err = IoErr();
        return -1;
    }
    return ++bgcount;
}

int os_run_native(const char *cmdline, OsHandle in, OsHandle out)
{
    LONG r;
    cwd_before = me()->pr_CurrentDir;
    r = SystemTags((STRPTR)cmdline, SYS_Input, H2B(in), SYS_Output, H2B(out),
                   SYS_UserShell, TRUE, TAG_END);
    sync_cwd_after();
    return (int)r;
}

int os_is_executable(const char *path)
{
    OsDirEnt e;
    if (os_stat(path, &e) != 0) return 0;
    if (e.type != OS_T_FILE) return 0;
    if (e.prot & OS_P_SCRIPT) return 1;
    if (!(e.prot & OS_P_EXEC)) return 0;
    {
        /* most Amiga files carry the e bit: check for a load file */
        BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
        ULONG magic = 0;
        if (!fh) return 0;
        if (Read(fh, &magic, 4) != 4) magic = 0;
        Close(fh);
        if (magic == 0x000003F3UL) return 1;           /* HUNK_HEADER */
        if ((magic >> 16) == (('#' << 8) | '!')) return 1;
        if (magic == 0x7F454C46UL) return 1;           /* ELF (AROS) */
        return 0;
    }
}

int os_is_resident(const char *name)
{
    struct Segment *s;
    Forbid();
    s = FindSegment((STRPTR)name, NULL, 0);
    if (!s) s = FindSegment((STRPTR)name, NULL, 1);
    if (s && s->seg_UC == CMD_DISABLED) s = NULL;
    Permit();
    return s != NULL;
}

void os_list_resident(int (*cb)(const char *name, void *ud), void *ud)
{
    struct DosInfo *di;
    struct Segment *s;
    StrList names;
    int i;
    char nm[64];
    sl_init(&names);
    Forbid();
    di = (struct DosInfo *)BADDR(DOSBase->dl_Root->rn_Info);
    for (s = (struct Segment *)BADDR(di->di_NetHand ? di->di_ResList : di->di_ResList); s;
         s = (struct Segment *)BADDR(s->seg_Next)) {
        int n = s->seg_Name[0];
        if (s->seg_UC == CMD_DISABLED) continue;
        if (n > 63) n = 63;
        memcpy(nm, s->seg_Name + 1, n);
        nm[n] = 0;
        if (names.n < 200) sl_adddup(&names, nm);
    }
    Permit();
    for (i = 0; i < names.n; i++)
        if (cb(names.v[i], ud)) break;
    sl_free(&names);
}

int os_break_cli(long n)
{
    struct Process *p;
    Forbid();
    p = FindCliProc((ULONG)n);
    if (p) Signal((struct Task *)p, SIGBREAKF_CTRL_C);
    Permit();
    return p ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* environment                                                          */

char *os_getenv(const char *name)
{
    char buf[1024];
    LONG n = GetVar((STRPTR)name, (STRPTR)NEARP(buf), sizeof(buf), 0);
    if (n < 0) return NULL;
    return xstrdup(buf);
}

void os_setenv(const char *name, const char *val)
{
    SetVar((STRPTR)name, (STRPTR)val, -1, GVF_LOCAL_ONLY | LV_VAR);
}

void os_unsetenv(const char *name)
{
    DeleteVar((STRPTR)name, GVF_LOCAL_ONLY | LV_VAR);
}

void os_environ(void (*cb)(const char *n, const char *v, void *ud), void *ud)
{
    static const char *const globals[] = {
        "HOME", "USER", "LOGNAME", "TERM", "EDITOR", "VISUAL", "PAGER", "LANG",
        "HOSTNAME", "TZ", "PS1", "HISTFILE", "HISTSIZE", "Language", NULL
    };
    struct LocalVar *lv;
    struct Process *pr = me();
    StrList n, v;
    int i;
    char buf[512];
    sl_init(&n);
    sl_init(&v);
    for (i = 0; globals[i]; i++) {
        if (GetVar((STRPTR)globals[i], (STRPTR)NEARP(buf), sizeof(buf), GVF_GLOBAL_ONLY) >= 0) {
            sl_adddup(&n, globals[i]);
            sl_adddup(&v, buf);
        }
    }
    for (lv = (struct LocalVar *)pr->pr_LocalVars.mlh_Head; lv->lv_Node.ln_Succ;
         lv = (struct LocalVar *)lv->lv_Node.ln_Succ) {
        if (lv->lv_Node.ln_Type != LV_VAR || !lv->lv_Node.ln_Name) continue;
        if (lv->lv_Len < sizeof(buf)) {
            memcpy(buf, lv->lv_Value, lv->lv_Len);
            buf[lv->lv_Len] = 0;
            sl_adddup(&n, lv->lv_Node.ln_Name);
            sl_adddup(&v, buf);
        }
    }
    for (i = 0; i < n.n; i++) cb(n.v[i], v.v[i], ud);
    sl_free(&n);
    sl_free(&v);
}

/* ------------------------------------------------------------------ */
/* misc                                                                 */

int os_break_flag;

int os_check_break(void)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
        os_break_flag = 1;   /* sticky until the shell consumes it */
        return 1;
    }
    return 0;
}

void os_sleep_ms(long ms)
{
    while (ms > 0) {
        long chunk = ms > 200 ? 200 : ms;
        long ticks = chunk / 20;
        if (ticks < 1) ticks = 1;
        Delay(ticks);
        ms -= chunk;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) break;
    }
}

long os_time(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds_to_epoch(&ds);
}

long os_time_ms(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ds.ds_Days % 1000L) * 86400000L + ds.ds_Minute * 60000L + ds.ds_Tick * (1000L / TICKS_PER_SECOND);
}

long os_tz_offset(void)
{
    return 0;   /* the Amiga clock runs in local time */
}

long os_pid(void)
{
    struct Process *pr = me();
    if (pr->pr_TaskNum) return pr->pr_TaskNum;
    return ((long)pr >> 4) & 0xFFFF;
}

const char *os_tmpdir(void)
{
    return NEARP(tmpdir);
}

const char *os_nulldev(void)
{
    return "NIL:";
}

const char *os_hostname(void)
{
    static char buf[64];
    if (!buf[0]) {
        if (GetVar((STRPTR)"HOSTNAME", (STRPTR)NEARP(buf), sizeof(buf), 0) < 0) strcpy(buf, "amiga");
    }
    return NEARP(buf);
}

const char *os_username(void)
{
    static char buf[64];
    if (!buf[0]) {
        if (GetVar((STRPTR)"USER", (STRPTR)NEARP(buf), sizeof(buf), 0) < 0) strcpy(buf, "amiga");
    }
    return NEARP(buf);
}

const char *os_default_home(void)
{
    static char buf[256];
    if (!buf[0]) {
        if (GetVar((STRPTR)"HOME", (STRPTR)NEARP(buf), sizeof(buf), 0) < 0) strcpy(buf, "SYS:");
    }
    return NEARP(buf);
}

/* PATH built from the CLI's command path, written Unix style so that
 * ':' can separate entries: "/C:/Workbench/Utilities:..." */
const char *os_default_path(void)
{
    static char *path;
    struct CommandLineInterface *cli = Cli();
    SBuf b;
    char buf[256];
    if (path) return path;
    sb_init(&b);
    sb_adds(&b, "/C");
    if (cli && cli->cli_CommandDir) {
        BPTR *pn = (BPTR *)BADDR(cli->cli_CommandDir);
        int guard = 0;
        while (pn && guard++ < 64) {
            BPTR lock = pn[1];
            if (lock && NameFromLock(lock, (STRPTR)NEARP(buf), sizeof(buf))) {
                char *c = strchr(buf, ':');
                if (c) {
                    sb_addc(&b, ':');
                    sb_addc(&b, '/');
                    sb_addn(&b, buf, (int)(c - buf));
                    if (c[1]) {
                        sb_addc(&b, '/');
                        sb_adds(&b, c + 1);
                    }
                }
            }
            pn = (BPTR *)BADDR(pn[0]);
        }
    } else {
        sb_adds(&b, ":/SYS/Utilities:/SYS/System:/SYS/Rexxc:/S");
    }
    sb_adds(&b, ":.");
    path = sb_take(&b);
    return path;
}

/* ask the console for its size: CSI 0 q -> CSI 1;1;rows;cols r */
int os_console_size(OsHandle h, int *rows, int *cols)
{
    static int crow = 24, ccol = 80;
    BPTR in = Input();
    char buf[32];
    int n = 0, ok = 0;
    (void)h;
    *rows = crow;
    *cols = ccol;
    if (!IsInteractive(in) || !IsInteractive(Output())) return -1;
    SetMode(in, 1);
    Write(Output(), (APTR)"\2330 q", 4);
    /* reply: CSI 1;1;rows;cols r  - anything else is typed-ahead input */
    while (WaitForChar(in, 300000)) {
        char c;
        if (Read(in, &c, 1) != 1) break;
        if (n == 0 && (unsigned char)c != 0x9b && c != 27) {
            if (npush < (int)sizeof(pushback)) pushback[npush++] = c;
            continue;
        }
        if (n == 1 && c == '[' && buf[0] == 27) {
            buf[n++] = c;
            continue;
        }
        if (n >= 1 && n <= 2 && !(c >= '0' && c <= '9') && (n == 1 || buf[1] == '[')) {
            /* CSI followed by a non-digit: a key, not our report */
            int k;
            buf[n++] = c;
            for (k = 0; k < n && npush < (int)sizeof(pushback); k++) pushback[npush++] = buf[k];
            n = 0;
            continue;
        }
        if (n < (int)sizeof(buf) - 1) buf[n++] = c;
        if (c == 'r') {
            ok = 1;
            break;
        }
        if (n > 1 && (c == '~' || (isalpha((unsigned char)c) && c != 'r' && n > 2 && buf[n - 2] != ' '))) {
            /* some other key sequence: give it back */
            int k;
            for (k = 0; k < n && npush < (int)sizeof(pushback); k++) pushback[npush++] = buf[k];
            n = 0;
        }
    }
    buf[n] = 0;
    if (ok) {
        int a, b, r, c;
        char *p = buf;
        while (*p && !(*p >= '0' && *p <= '9')) p++;
        if (sscanf(p, "%d;%d;%d;%d", &a, &b, &r, &c) == 4 && c > 10 && r > 2) {
            crow = r;
            ccol = c;
        }
    }
    *rows = crow;
    *cols = ccol;
    return ok ? 0 : -1;
}

double os_freespace(const char *path, double *total)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK);
    struct InfoData *id;
    double fr = -1;
    if (total) *total = -1;
    if (!l) {
        last_err = IoErr();
        return -1;
    }
    id = AllocMem(sizeof(struct InfoData), MEMF_PUBLIC | MEMF_CLEAR);
    if (id && Info(l, id)) {
        double bpb = (double)id->id_BytesPerBlock;
        fr = (double)(id->id_NumBlocks - id->id_NumBlocksUsed) * bpb;
        if (total) *total = (double)id->id_NumBlocks * bpb;
    }
    if (id) FreeMem(id, sizeof(struct InfoData));
    UnLock(l);
    return fr;
}

unsigned long os_random_seed(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (unsigned long)(ds.ds_Tick * 7919UL + ds.ds_Minute * 104729UL + ds.ds_Days) ^ (unsigned long)FindTask(NULL);
}

int os_stack_ok(void)
{
    struct Task *t = FindTask(NULL);
    char here;
    return (char *)&here - (char *)t->tc_SPLower > 6000;
}
#endif

#ifdef AMIGA
/* ------------------------------------------------------------------ */
/* console window: font and colours                                     */

struct GfxBase *GfxBase;
struct Library *DiskfontBase;
static struct TextFont *our_font;   /* font we opened for the console */

static int open_gfx(void)
{
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 36);
    return GfxBase != NULL;
}

/* the Intuition window of our console and its console.device unit
 * (ACTION_DISK_INFO: id_VolumeNode = window, id_InUse = IORequest) */
static struct Window *console_window_unit(struct ConUnit **cu)
{
    struct MsgPort *ct = me()->pr_ConsoleTask;
    struct InfoData *id;
    struct Window *w = NULL;
    if (cu) *cu = NULL;
    if (!ct) return NULL;
    id = AllocVec(sizeof(struct InfoData), MEMF_PUBLIC | MEMF_CLEAR);
    if (!id) return NULL;
    if (DoPkt(ct, ACTION_DISK_INFO, MKBADDR(id), 0, 0, 0, 0)) {
        w = (struct Window *)id->id_VolumeNode;
        if (cu && id->id_InUse) {
            struct ConUnit *u = (struct ConUnit *)((struct IOStdReq *)id->id_InUse)->io_Unit;
            if (u && u->cu_Window == w) *cu = u;
        }
    }
    FreeVec(id);
    return w;
}

static struct Window *console_window(void)
{
    return console_window_unit(NULL);
}

int os_console_getfont(char *name, int nsz, int *size)
{
    struct Window *w = console_window();
    struct TextFont *f;
    if (!w || !w->RPort || !(f = w->RPort->Font)) return -1;
    strncpy(name, f->tf_Message.mn_Node.ln_Name ? f->tf_Message.mn_Node.ln_Name : "?", nsz - 1);
    name[nsz - 1] = 0;
    *size = f->tf_YSize;
    return 0;
}

/* Without diskfont.library (e.g. a bare ROM boot) load FONTS:name/size
 * ourselves: the file is a load module holding a DiskFontHeader right
 * after the next-segment pointer and a 4-byte return code. */
struct TextFont *os_load_font(const char *fpath, int size)
{
    char path[200];
    BPTR seg;
    struct DiskFontHeader *dfh;
    struct TextFont *tf;
    char *nm;
    const char *fname = (const char *)FilePart((STRPTR)fpath);
    struct TextAttr ta;
    size_t bl = strlen(fpath);
    if (bl < 6 || bl > 150) return NULL;
    bl -= 5;                            /* strip ".font" */
    ta.ta_Name = (STRPTR)fname;
    ta.ta_YSize = (UWORD)size;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    tf = OpenFont(&ta);                 /* loaded before? */
    if (tf && tf->tf_YSize == size) return tf;
    if (tf) CloseFont(tf);
    sprintf(path, "%s%.*s/%d", strchr(fpath, ':') ? "" : "FONTS:", (int)bl, fpath, size);
    seg = LoadSeg((STRPTR)path);
    if (!seg) {
        last_err = IoErr();
        return NULL;
    }
    dfh = (struct DiskFontHeader *)((char *)BADDR(seg) + 8);
    if (dfh->dfh_FileID != DFH_ID) {
        UnLoadSeg(seg);
        last_err = ERROR_OBJECT_WRONG_TYPE;
        return NULL;
    }
    tf = &dfh->dfh_TF;
    nm = AllocVec(strlen(fname) + 1, MEMF_PUBLIC);
    if (!nm) {
        UnLoadSeg(seg);
        return NULL;
    }
    strcpy(nm, fname);
    tf->tf_Message.mn_Node.ln_Name = (STRPTR)nm;
    tf->tf_Message.mn_Node.ln_Type = NT_FONT;
    AddFont(tf);                        /* stays loaded for the session */
    return OpenFont(&ta);
}

int os_console_setfont(const char *name, int size)
{
    struct Window *w = console_window();
    struct TextAttr ta;
    struct TextFont *f = NULL;
    char fname[64];
    last_err = 0;
    if (!w || !open_gfx()) {
        last_err = ERROR_OBJECT_WRONG_TYPE;
        return -1;
    }
    strncpy(fname, name, sizeof(fname) - 6);
    fname[sizeof(fname) - 6] = 0;
    if (!str_endswith(fname, ".font")) strcat(fname, ".font");
    ta.ta_Name = (STRPTR)fname;
    ta.ta_YSize = (UWORD)size;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    if (!DiskfontBase) DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    if (DiskfontBase) f = OpenDiskFont(&ta);
    if (!f) f = OpenFont(&ta);
    if (!f) f = os_load_font(fname, size);
    if (!f) {
        if (!last_err) last_err = ERROR_OBJECT_NOT_FOUND;
        return -1;
    }
    if (f->tf_Flags & FPF_PROPORTIONAL) {
        CloseFont(f);
        last_err = ERROR_BAD_TEMPLATE;   /* reported as "not a fixed-width font" */
        return -2;
    }
    SetFont(w->RPort, f);
    {
        /* tell console.device about the new character cell */
        struct ConUnit *cu;
        console_window_unit(&cu);
        if (cu) {
            int xo, yo;
            Forbid();
            xo = cu->cu_XROrigin;
            yo = cu->cu_YROrigin;
            cu->cu_Font = f;
            cu->cu_XRSize = f->tf_XSize;
            cu->cu_YRSize = f->tf_YSize;
            cu->cu_XMax = (w->Width - w->BorderRight - xo) / f->tf_XSize - 1;
            cu->cu_YMax = (w->Height - w->BorderBottom - yo) / f->tf_YSize - 1;
            if (cu->cu_XMax < 0) cu->cu_XMax = 0;
            if (cu->cu_YMax < 0) cu->cu_YMax = 0;
            cu->cu_XRExtant = xo + (cu->cu_XMax + 1) * f->tf_XSize - 1;
            cu->cu_YRExtant = yo + (cu->cu_YMax + 1) * f->tf_YSize - 1;
            if (cu->cu_XCP > cu->cu_XMax) cu->cu_XCP = cu->cu_XMax;
            if (cu->cu_YCP > cu->cu_YMax) cu->cu_YCP = cu->cu_YMax;
            Permit();
        }
    }
    if (our_font) CloseFont(our_font);
    our_font = f;
    /* home + clear: redraw everything in the new character cell */
    Write(Output(), (APTR)"\f", 1);
    return 0;
}

static struct Screen *console_screen(void)
{
    struct Window *w = console_window();
    return w ? w->WScreen : NULL;
}

int os_console_pens(void)
{
    struct Screen *s = console_screen();
    if (!s) return -1;
    return 1 << s->RastPort.BitMap->Depth;
}

int os_console_getpen(int pen, unsigned long *rgb)
{
    struct Screen *s = console_screen();
    ULONG c;
    if (!s || !open_gfx() || pen < 0 || pen >= os_console_pens()) return -1;
    c = GetRGB4(s->ViewPort.ColorMap, pen);
    /* 0x0RGB -> 0xRRGGBB */
    *rgb = ((c >> 8) & 15) * 0x110000UL + ((c >> 4) & 15) * 0x1100UL + (c & 15) * 0x11UL;
    return 0;
}

int os_console_setpen(int pen, unsigned long rgb)
{
    struct Screen *s = console_screen();
    if (!s || !open_gfx() || pen < 0 || pen >= os_console_pens()) return -1;
    if (GfxBase->LibNode.lib_Version >= 39)
        SetRGB32(&s->ViewPort, pen, ((rgb >> 16) & 255) * 0x01010101UL,
                 ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL);
    else
        SetRGB4(&s->ViewPort, pen, (rgb >> 20) & 15, (rgb >> 12) & 15, (rgb >> 4) & 15);
    return 0;
}
#endif

#ifdef AMIGA
/* ------------------------------------------------------------------ */
/* Bash windows                                                         */

extern struct WBStartup *_WBenchMsg;
struct IntuitionBase *IntuitionBase;

static int open_intuition(void)
{
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 36);
    return IntuitionBase != NULL;
}

/* every interactive bash registers a public port so that windows can be
 * counted and cycled through */
typedef struct BashPort {
    struct MsgPort mp;
    struct Window *win;
    int num;
} BashPort;

static BashPort *myport;
static char oldtitle_buf[1];
static UBYTE *oldtitle;
static char mytitle[32];
static char selfpath[256];

static void find_self(void)
{
    char name[108];
    BPTR dir = 0;
    if (selfpath[0]) return;
    if (_WBenchMsg && _WBenchMsg->sm_NumArgs > 0) {
        dir = _WBenchMsg->sm_ArgList[0].wa_Lock;
        strncpy(name, (char *)_WBenchMsg->sm_ArgList[0].wa_Name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
    } else {
        dir = GetProgramDir();
        if (!GetProgramName((STRPTR)name, sizeof(name))) strcpy(name, "bash");
    }
    if (dir && NameFromLock(dir, (STRPTR)NEARP(selfpath), sizeof(selfpath))) {
        AddPart((STRPTR)NEARP(selfpath), FilePart((STRPTR)name), sizeof(selfpath));
    } else if (GetVar((STRPTR)"AMIBASH_SELF", (STRPTR)NEARP(selfpath), sizeof(selfpath), 0) <= 0) {
        /* resident: the terminal says where the program came from */
        strcpy(selfpath, "C:bash");
    }
}

const char *os_self_path(void)
{
    find_self();
    return NEARP(selfpath);
}

static int in_terminal(void)
{
    char b[8];
    return GetVar((STRPTR)"AMIBASH_TERM", (STRPTR)b, sizeof(b), 0) >= 0;
}

static int count_bash_windows(int *maxnum)
{
    struct Node *n;
    int cnt = 0;
    *maxnum = 0;
    Forbid();
    for (n = SysBase->PortList.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        if (n->ln_Name && strcmp(n->ln_Name, "AmiBash") == 0) {
            cnt++;
            if (((BashPort *)n)->num > *maxnum) *maxnum = ((BashPort *)n)->num;
        }
    }
    Permit();
    return cnt;
}

static char winspec_tt[160];     /* WINDOW tool type from our icon */
int os_new_window_only(void);

int os_new_shell_window(void)
{
    if (in_terminal()) {
        /* inside the AmiBash Terminal: open a tab instead of a window */
        int r = term_request("new");
        if (r != -2) return r;
    }
    return os_new_window_only();
}

int os_new_window_only(void)
{
    struct Screen *scr;
    char spec[160], cmd[300];
    int w = 640, h = 200, top = 11, maxnum, n;
    BPTR in;
    LONG r;
    find_self();
    count_bash_windows(&maxnum);
    n = maxnum;                         /* stagger new windows */
    if (open_intuition() && (scr = LockPubScreen(NULL)) != NULL) {
        top = scr->BarHeight + 1;
        w = scr->Width;
        h = (scr->Height - top) * 3 / 4;
        UnlockPubScreen(NULL, scr);
    }
    if (winspec_tt[0] && n == 0) strcpy(spec, winspec_tt);
    else sprintf(spec, "CON:%d/%d/%d/%d/Bash/CLOSE", (n % 6) * 16, top + (n % 6) * 12, w - 96, h);
    in = Open((STRPTR)spec, MODE_NEWFILE);
    if (!in) {
        last_err = IoErr();
        return -1;
    }
    sprintf(cmd, "\"%s\" -i", selfpath);
    r = SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, NULL, SYS_Asynch, TRUE,
                   SYS_UserShell, TRUE, NP_StackSize, 65536, TAG_END);
    if (r == -1) {
        last_err = IoErr();
        Close(in);
        return -1;
    }
    return 0;
}

/* "bash --term" typed in a shell: open the AmiBash Terminal (or a new
 * tab in the one already running) without tying up this shell */
int os_open_terminal(void)
{
    char cmd[300];
    BPTR in, out;
    LONG r = term_request("new");
    if (r != -2) return r == 0 ? 0 : -1;
    find_self();
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out) {
        if (in) Close(in);
        if (out) Close(out);
        return -1;
    }
    sprintf(cmd, "\"%s\" --term-run", selfpath);
    r = SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                   NP_StackSize, 16384, TAG_END);
    if (r == -1) {
        last_err = IoErr();
        Close(in);
        Close(out);
        return -1;
    }
    return 0;
}

int os_wb_launch(void)
{
    struct Library *IconBase;
    if (!_WBenchMsg) return 0;
    /* tool type WINDOW=CON:... in the icon overrides the window */
    IconBase = OpenLibrary((STRPTR)"icon.library", 36);
    if (IconBase && _WBenchMsg->sm_NumArgs > 0) {
        BPTR old = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
        struct DiskObject *d = GetDiskObject(_WBenchMsg->sm_ArgList[0].wa_Name);
        if (d) {
            STRPTR v = FindToolType(d->do_ToolTypes, (STRPTR)"WINDOW");
            if (v && strlen((char *)v) < sizeof(winspec_tt)) strcpy(winspec_tt, (char *)v);
            FreeDiskObject(d);
        }
        CurrentDir(old);
    }
    if (IconBase) CloseLibrary(IconBase);
    /* the tabbed AmiBash Terminal; plain console window as fallback
     * (or when the icon has the tool type CONSOLE) */
    if (!winspec_tt[0]) {
        find_self();
        if (term_main(NEARP(selfpath)) == 0) return 1;
    }
    /* started from Workbench: run in an own "Bash" window as a proper CLI
     * process; the window closes when that shell exits */
    os_new_shell_window();
    return 1;
}

int os_next_window_only(void);

void os_console_attach(void)
{
    struct Window *w = console_window();
    int maxnum;
    if (!w || !open_intuition() || myport || in_terminal()) return;
    count_bash_windows(&maxnum);
    myport = AllocVec(sizeof(BashPort), MEMF_PUBLIC | MEMF_CLEAR);
    if (!myport) return;
    myport->mp.mp_Node.ln_Name = (char *)"AmiBash";
    myport->mp.mp_Node.ln_Type = NT_MSGPORT;
    myport->mp.mp_Flags = PA_IGNORE;
    NewList(&myport->mp.mp_MsgList);
    myport->win = w;
    myport->num = maxnum + 1;
    AddPort(&myport->mp);
    oldtitle = w->Title;
    if (myport->num == 1) strcpy(mytitle, "Bash");
    else sprintf(mytitle, "Bash %d", myport->num);
    SetWindowTitles(w, (UBYTE *)mytitle, (UBYTE *)~0);
    (void)oldtitle_buf;
}

void os_console_detach(void)
{
    if (!myport) return;
    RemPort(&myport->mp);
    if (myport->win && open_intuition()) SetWindowTitles(myport->win, oldtitle, (UBYTE *)~0);
    FreeVec(myport);
    myport = NULL;
}

int os_next_shell_window(void)
{
    if (in_terminal() && term_request("next") != -2) return 0;
    return os_next_window_only();
}

int os_next_window_only(void)
{
    struct Node *n;
    struct Window *wins[32], *next = NULL;
    int nums[32], cnt = 0, i, best = -1;
    int mynum = myport ? myport->num : 0;
    if (!open_intuition()) return -1;
    Forbid();
    for (n = SysBase->PortList.lh_Head; n->ln_Succ && cnt < 32; n = n->ln_Succ) {
        if (n->ln_Name && strcmp(n->ln_Name, "AmiBash") == 0) {
            wins[cnt] = ((BashPort *)n)->win;
            nums[cnt] = ((BashPort *)n)->num;
            cnt++;
        }
    }
    /* next higher number, wrapping round to the lowest */
    for (i = 0; i < cnt; i++)
        if (nums[i] > mynum && (best < 0 || nums[i] < nums[best])) best = i;
    if (best < 0)
        for (i = 0; i < cnt; i++)
            if (best < 0 || nums[i] < nums[best]) best = i;
    if (best >= 0 && nums[best] != mynum) next = wins[best];
    if (next) {
        WindowToFront(next);
        ActivateWindow(next);
    }
    Permit();
    return next ? 0 : -1;
}
#endif

#ifdef AMIGA
/* ------------------------------------------------------------------ */
/* system information                                                   */

void os_sysinfo(OsSysInfo *si)
{
    static const struct { int v; const char *name; } rel[] = {
        {33, "1.2"}, {34, "1.3"}, {36, "2.0"}, {37, "2.04"}, {38, "2.1"}, {39, "3.0"},
        {40, "3.1"}, {44, "3.5"}, {45, "3.9"}, {46, "3.1.4"}, {47, "3.2"}, {0, NULL}
    };
    struct Library *aros = OpenLibrary((STRPTR)"aros.library", 0);
    UWORD af = SysBase->AttnFlags;
    int v = SysBase->LibNode.lib_Version, i;
    const char *name = NULL;
    memset(si, 0, sizeof(*si));
    strcpy(si->sysname, aros ? "AROS" : "AmigaOS");
    if (aros) CloseLibrary(aros);
    for (i = 0; rel[i].name; i++)
        if (rel[i].v == v) name = rel[i].name;
    if (aros || !name) sprintf(si->release, "%d.%d", v, (int)SysBase->SoftVer);
    else sprintf(si->release, "%s (%d.%d)", name, v, (int)SysBase->SoftVer);
    strcpy(si->machine, "m68k");
    if (af & (1 << 7)) strcpy(si->processor, "68060");
    else if (af & AFF_68040) strcpy(si->processor, "68040");
    else if (af & AFF_68030) strcpy(si->processor, "68030");
    else if (af & AFF_68020) strcpy(si->processor, "68020");
    else if (af & AFF_68010) strcpy(si->processor, "68010");
    else strcpy(si->processor, "68000");
    if (af & (1 << 6)) strcat(si->processor, "/FPU");
    else if (af & AFF_68882) strcat(si->processor, "/68882");
    else if (af & AFF_68881) strcat(si->processor, "/68881");
    strcpy(si->platform, "Amiga");
}
#endif

/* ------------------------------------------------------------------ */
/* clipboard: clipboard.device unit 0, text as IFF FORM FTXT / CHRS     */

#include <devices/clipboard.h>

static struct IOClipReq *clip_open(struct MsgPort **mp)
{
    struct IOClipReq *io;
    if (!(*mp = CreateMsgPort())) return NULL;
    io = (struct IOClipReq *)CreateIORequest(*mp, sizeof(struct IOClipReq));
    if (io && OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0) == 0) return io;
    if (io) DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(*mp);
    return NULL;
}

static void clip_close(struct IOClipReq *io, struct MsgPort *mp)
{
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(mp);
}

int os_clip_write(const char *s, long n)
{
    struct MsgPort *mp;
    struct IOClipReq *io = clip_open(&mp);
    ULONG pad = n & 1, total = 12 + 8 + n + pad;
    UBYTE *buf;
    int rc = -1;
    if (!io) return -1;
    buf = AllocVec(total, MEMF_ANY);
    if (buf) {
        ULONG *l = (ULONG *)buf;
        l[0] = 0x464F524DUL;                    /* FORM */
        l[1] = total - 8;
        l[2] = 0x46545854UL;                    /* FTXT */
        l[3] = 0x43485253UL;                    /* CHRS */
        l[4] = (ULONG)n;
        memcpy(buf + 20, s, n);
        if (pad) buf[20 + n] = 0;
        io->io_Command = CMD_WRITE;
        io->io_Data = (STRPTR)buf;
        io->io_Length = total;
        io->io_Offset = 0;
        io->io_ClipID = 0;
        if (DoIO((struct IORequest *)io) == 0) {
            io->io_Command = CMD_UPDATE;
            rc = DoIO((struct IORequest *)io) == 0 ? 0 : -1;
        }
        FreeVec(buf);
    }
    clip_close(io, mp);
    return rc;
}

/* read LEN bytes of the clip at the current offset; bytes read */
static ULONG clip_get(struct IOClipReq *io, void *buf, ULONG len)
{
    io->io_Command = CMD_READ;
    io->io_Data = (STRPTR)buf;
    io->io_Length = len;
    DoIO((struct IORequest *)io);
    return io->io_Actual;
}

char *os_clip_read(long *n)
{
    struct MsgPort *mp;
    struct IOClipReq *io = clip_open(&mp);
    ULONG hdr[3], size, pos, len = 0;
    UBYTE *form = NULL, dummy[64];
    char *out = NULL;
    *n = 0;
    if (!io) return NULL;
    io->io_Offset = 0;
    io->io_ClipID = 0;
    if (clip_get(io, hdr, 12) == 12 && hdr[0] == 0x464F524DUL && hdr[2] == 0x46545854UL && hdr[1] >= 4 &&
        hdr[1] < 0x1000000UL) {
        size = hdr[1] - 4;
        form = AllocVec(size + 1, MEMF_ANY);
        if (form && clip_get(io, form, size) == size) {
            out = AllocVec(size + 1, MEMF_ANY);
            for (pos = 0; out && pos + 8 <= size;) {
                ULONG id = ((ULONG *)(form + pos))[0], cl = ((ULONG *)(form + pos))[1];
                if (cl > size - pos - 8) break;
                if (id == 0x43485253UL) {           /* CHRS */
                    memcpy(out + len, form + pos + 8, cl);
                    len += cl;
                }
                pos += 8 + cl + (cl & 1);
            }
        }
    }
    /* read to the end, which tells the device we are done */
    while (clip_get(io, dummy, sizeof(dummy)) > 0) ;
    if (form) FreeVec(form);
    clip_close(io, mp);
    if (out && !len) {
        FreeVec(out);
        out = NULL;
    }
    if (out) out[len] = 0;
    *n = (long)len;
    return out;
}

void os_clip_free(char *p)
{
    if (p) FreeVec(p);
}
