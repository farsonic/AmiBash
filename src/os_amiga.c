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
#include <proto/dos.h>
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
}

void os_cleanup(void)
{
    me()->pr_WindowPtr = old_winptr;
    if (cwd_changed) {
        BPTR mine = CurrentDir(orig_cwd);
        UnLock(mine);
        cwd_changed = 0;
    }
    if (IsInteractive(Input())) SetMode(Input(), 0);
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
    raw_fh = on ? H2B(h) : 0;
    return SetMode(H2B(h), on ? 1 : 0) ? 0 : -1;
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
    Fault(last_err, NULL, (STRPTR)buf, sizeof(buf));
    return buf;
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
    if (!NameFromLock(l, (STRPTR)buf, sizeof(buf))) {
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
        if (now && NameFromLock(now, (STRPTR)buf, sizeof(buf))) sh_set_pwd(buf);
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
    LONG n = GetVar((STRPTR)name, (STRPTR)buf, sizeof(buf), 0);
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
        if (GetVar((STRPTR)globals[i], (STRPTR)buf, sizeof(buf), GVF_GLOBAL_ONLY) >= 0) {
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
    return tmpdir;
}

const char *os_nulldev(void)
{
    return "NIL:";
}

const char *os_hostname(void)
{
    static char buf[64];
    if (!buf[0]) {
        if (GetVar((STRPTR)"HOSTNAME", (STRPTR)buf, sizeof(buf), 0) < 0) strcpy(buf, "amiga");
    }
    return buf;
}

const char *os_username(void)
{
    static char buf[64];
    if (!buf[0]) {
        if (GetVar((STRPTR)"USER", (STRPTR)buf, sizeof(buf), 0) < 0) strcpy(buf, "amiga");
    }
    return buf;
}

const char *os_default_home(void)
{
    static char buf[256];
    if (!buf[0]) {
        if (GetVar((STRPTR)"HOME", (STRPTR)buf, sizeof(buf), 0) < 0) strcpy(buf, "SYS:");
    }
    return buf;
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
    if (cli) {
        BPTR *pn = (BPTR *)BADDR(cli->cli_CommandDir);
        int guard = 0;
        while (pn && guard++ < 64) {
            BPTR lock = pn[1];
            if (lock && NameFromLock(lock, (STRPTR)buf, sizeof(buf))) {
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
