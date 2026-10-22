/*
 * tasktime.c - CPU time per task, memory and system figures for AmigaOS.
 * See tasktime.h.
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>
#include <stdio.h>

#include "tasktime.h"

extern struct ExecBase *SysBase;
struct Device *TimerBase;               /* ReadEClock(), also in the hooks */

void tt_launch_stub(void);              /* tthooks.s */
void tt_switch_stub(void);

/*
 * One slot per task we timed.  The hooks look a task up by its address;
 * the main program adds and removes slots only in Forbid(), when no task
 * switch and so no hook can happen.
 */
#define NSLOT 512
#define TOMB  ((struct Task *)1)

typedef struct Slot {
    struct Task *task;
    APTR name;                          /* its name pointer: a new task in old memory has another */
    UBYTE oldflags;                     /* its own TF_LAUNCH / TF_SWITCH  */
    UBYTE running;                      /* launched, not switched out yet */
    UBYTE seen;
    UBYTE pad;
    void (*oldlaunch)(void);
    void (*oldswitch)(void);
    ULONG in;                           /* E-clock (low word) at launch   */
    ULONG acc_hi, acc_lo;               /* E-clock ticks it ran           */
    ULONG prev_hi, prev_lo;             /* acc at the previous sample     */
} Slot;

static Slot slots[NSLOT];
static struct MsgPort *tport;
static struct timerequest *treq;
static struct EClockVal last;
static ULONG efreq;
static ULONG lastdisp;
static unsigned long long uptime;     /* E-clock ticks since tt_open() */
static int opened;

static ULONG hash(struct Task *t)
{
    return ((ULONG)t >> 4) % NSLOT;
}

static Slot *find(struct Task *t)
{
    ULONG h = hash(t), i;
    for (i = 0; i < NSLOT; i++) {
        Slot *s = &slots[(h + i) % NSLOT];
        if (s->task == t) return s;
        if (s->task == NULL) return NULL;
    }
    return NULL;
}

static Slot *insert(struct Task *t)
{
    ULONG h = hash(t), i;
    for (i = 0; i < NSLOT; i++) {
        Slot *s = &slots[(h + i) % NSLOT];
        if (s->task == NULL || s->task == TOMB) {
            memset(s, 0, sizeof(*s));
            s->task = t;
            return s;
        }
    }
    return NULL;
}

/* ---- the hooks (called by exec while it switches tasks) ---------------- */

ULONG tt_launch(void)
{
    Slot *s = find(SysBase->ThisTask);
    struct EClockVal e;
    if (!s) return 0;
    ReadEClock(&e);
    s->in = e.ev_lo;
    s->running = 1;
    return (s->oldflags & TF_LAUNCH) ? (ULONG)s->oldlaunch : 0;
}

ULONG tt_switch(void)
{
    Slot *s = find(SysBase->ThisTask);
    struct EClockVal e;
    ULONG d;
    if (!s) return 0;
    if (s->running) {
        ReadEClock(&e);
        d = e.ev_lo - s->in;            /* a slice is far shorter than 2^32 ticks */
        s->acc_lo += d;
        if (s->acc_lo < d) s->acc_hi++;
        s->running = 0;
    }
    return (s->oldflags & TF_SWITCH) ? (ULONG)s->oldswitch : 0;
}

/* ---- giving tasks the hooks and taking them back -------------------- */

static void hook(struct Task *t, Slot *s)
{
    if (t->tc_Launch != (void (*)())tt_launch_stub) {
        s->oldflags = t->tc_Flags & (TF_LAUNCH | TF_SWITCH);
        s->oldlaunch = (void (*)(void))t->tc_Launch;
        s->oldswitch = (void (*)(void))t->tc_Switch;
    }
    s->name = t->tc_Node.ln_Name;
    t->tc_Launch = (void (*)())tt_launch_stub;
    t->tc_Switch = (void (*)())tt_switch_stub;
    t->tc_Flags |= TF_LAUNCH | TF_SWITCH;
}

static void unhook(struct Task *t, Slot *s)
{
    if (t->tc_Launch != (void (*)())tt_launch_stub) return;
    t->tc_Launch = (void (*)())s->oldlaunch;
    t->tc_Switch = (void (*)())s->oldswitch;
    t->tc_Flags = (t->tc_Flags & ~(TF_LAUNCH | TF_SWITCH)) | s->oldflags;
}

/* call fn for every task: the running one, the ready and the waiting ones */
static void each_task(void (*fn)(struct Task *t, UBYTE state, void *ud), void *ud)
{
    struct Node *n;
    fn(SysBase->ThisTask, TS_RUN, ud);
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        fn((struct Task *)n, TS_READY, ud);
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        fn((struct Task *)n, TS_WAIT, ud);
}

static void restore_one(struct Task *t, UBYTE state, void *ud)
{
    Slot *s = find(t);
    if (s) unhook(t, s);
}

int tt_open(void)
{
    if (opened) return 0;
    if (!(tport = CreateMsgPort())) return -1;
    if (!(treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq)))) {
        DeleteMsgPort(tport);
        return -1;
    }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, &treq->tr_node, 0)) {
        DeleteIORequest((struct IORequest *)treq);
        DeleteMsgPort(tport);
        return -1;
    }
    TimerBase = treq->tr_node.io_Device;
    memset(slots, 0, sizeof(slots));
    efreq = ReadEClock(&last);
    lastdisp = SysBase->DispCount;
    uptime = 0;
    opened = 1;
    return 0;
}

void tt_close(void)
{
    if (!opened) return;
    Forbid();
    each_task(restore_one, NULL);       /* only tasks that are still there */
    Permit();
    CloseDevice(&treq->tr_node);
    DeleteIORequest((struct IORequest *)treq);
    DeleteMsgPort(tport);
    opened = 0;
}

/* ---- sampling ------------------------------------------------------------ */

typedef struct Collect {
    TaskInfo *ti;
    int max, n;
    SysInfo *sys;
    ULONG now;
    ULONG busy;                         /* ticks all tasks ran            */
} Collect;

static void bcopy_bstr(char *dst, BSTR b, int len)
{
    UBYTE *p = BADDR(b);
    int n;
    *dst = 0;
    if (!p) return;
    n = *p;
    if (n >= len) n = len - 1;
    memcpy(dst, p + 1, n);
    dst[n] = 0;
}

static void collect_one(struct Task *t, UBYTE state, void *ud)
{
    Collect *c = ud;
    Slot *s = find(t);
    ULONG d = 0;
    c->sys->ntasks++;
    if (state == TS_READY) c->sys->nready++;
    if (state == TS_WAIT) c->sys->nwait++;
    if (s && s->name != t->tc_Node.ln_Name) {
        /* another task now lives where an old one was */
        s->task = TOMB;
        s = NULL;
    }
    if (!s) {
        s = insert(t);
        if (s) {
            hook(t, s);
            if (state == TS_RUN) {
                s->running = 1;
                s->in = c->now;
            }
        }
    } else if (t->tc_Launch != (void (*)())tt_launch_stub)
        hook(t, s);                     /* someone set its hooks since */
    if (s) {
        s->seen = 1;
        d = s->acc_lo - s->prev_lo;
        s->prev_hi = s->acc_hi;
        s->prev_lo = s->acc_lo;
        c->busy += d;
    }
    if (c->n < c->max) {
        TaskInfo *i = &c->ti[c->n++];
        const char *nm = t->tc_Node.ln_Name;
        memset(i, 0, sizeof(*i));
        i->task = t;
        strncpy(i->name, nm ? nm : "(no name)", TT_NAMELEN - 1);
        i->pri = t->tc_Node.ln_Pri;
        i->type = t->tc_Node.ln_Type;
        i->state = state;
        i->sigwait = t->tc_SigWait;
        i->stack_size = (ULONG)t->tc_SPUpper - (ULONG)t->tc_SPLower;
        if (state == TS_RUN) {
            /* ourselves: where our stack is now */
            ULONG here = (ULONG)&here;
            i->stack_used = (ULONG)t->tc_SPUpper - here;
        } else
            i->stack_used = (ULONG)t->tc_SPUpper - (ULONG)t->tc_SPReg;
        if (i->stack_used > i->stack_size) i->stack_used = i->stack_size;
        if (t->tc_Node.ln_Type == NT_PROCESS) {
            struct Process *pr = (struct Process *)t;
            struct CommandLineInterface *cli = BADDR(pr->pr_CLI);
            if (cli) {
                i->cli = pr->pr_TaskNum;
                if (cli->cli_Module) bcopy_bstr(i->cmd, cli->cli_CommandName, TT_NAMELEN);
            }
        }
        if (s) {
            /* ticks for now; the CPU share is worked out after Permit() */
            i->cpu = d;
            i->cpu_ms = (ULONG)((((unsigned long long)s->acc_hi << 32) | s->acc_lo) * 1000 / efreq);
        }
    }
}

static void mem(ULONG type, ULONG *freep, ULONG *totalp, ULONG *largestp)
{
    *freep = AvailMem(type);
    *largestp = AvailMem(type | MEMF_LARGEST);
    if (SysBase->LibNode.lib_Version >= 39)
        *totalp = AvailMem(type | MEMF_TOTAL);
    else {
        struct MemHeader *mh;
        ULONG tot = 0;
        Forbid();
        for (mh = (struct MemHeader *)SysBase->MemList.lh_Head; mh->mh_Node.ln_Succ;
             mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
            if (mh->mh_Attributes & type) tot += (ULONG)mh->mh_Upper - (ULONG)mh->mh_Lower;
        Permit();
        *totalp = tot;
    }
}

int tt_sample(SysInfo *sys, TaskInfo *ti, int max)
{
    Collect c;
    struct EClockVal now;
    ULONG elapsed, busy = 0, disp;
    int i;
    ULONG k;

    memset(sys, 0, sizeof(*sys));
    c.ti = ti;
    c.max = max;
    c.n = 0;
    c.sys = sys;
    c.busy = 0;
    Forbid();
    ReadEClock(&now);
    c.now = now.ev_lo;
    for (k = 0; k < NSLOT; k++) slots[k].seen = 0;
    each_task(collect_one, &c);
    /* slots of tasks that ended */
    for (k = 0; k < NSLOT; k++)
        if (slots[k].task && slots[k].task != TOMB && !slots[k].seen) slots[k].task = TOMB;
    disp = SysBase->DispCount;
    Permit();

    elapsed = now.ev_lo - last.ev_lo;
    if (now.ev_hi - last.ev_hi > 1) elapsed = 0xffffffff;
    last = now;
    if (!elapsed) elapsed = 1;
    sys->interval_ms = (ULONG)((unsigned long long)elapsed * 1000 / efreq);
    uptime += elapsed;
    sys->uptime_s = (ULONG)(uptime / efreq);
    sys->dispatches = disp - lastdisp;
    lastdisp = disp;
    for (i = 0; i < c.n; i++) {
        ULONG d = ti[i].cpu;
        ti[i].cpu = tt_permille(d, elapsed);
    }
    busy = c.busy;
    sys->cpu = tt_permille(busy, elapsed);
    sys->timed = 1;
    mem(MEMF_CHIP, &sys->chip_free, &sys->chip_total, &sys->chip_largest);
    mem(MEMF_FAST, &sys->fast_free, &sys->fast_total, &sys->fast_largest);
    return c.n;
}

ULONG tt_permille(ULONG a, ULONG b)
{
    if (!b) return 0;
    if (a >= b) return 1000;
    return (ULONG)((unsigned long long)a * 1000 / b);
}

/* ---- the machine ---------------------------------------------------------- */

void tt_machine(MachineInfo *m)
{
    UWORD a = SysBase->AttnFlags;
    struct GfxBase *gb;
    UWORD v = SysBase->LibNode.lib_Version, r = SysBase->LibNode.lib_Revision;
    const char *os;

    memset(m, 0, sizeof(*m));
    if (a & 0x80) strcpy(m->cpu, "68060");      /* AFF_68060 */
    else if (a & AFF_68040) strcpy(m->cpu, "68040");
    else if (a & AFF_68030) strcpy(m->cpu, "68030");
    else if (a & AFF_68020) strcpy(m->cpu, "68020");
    else if (a & AFF_68010) strcpy(m->cpu, "68010");
    else strcpy(m->cpu, "68000");
    if ((a & AFF_FPU40) && (a & (AFF_68040 | 0x80))) strcpy(m->fpu, a & 0x80 ? "68060 FPU" : "68040 FPU");
    else if (a & AFF_68882) strcpy(m->fpu, "68882");
    else if (a & AFF_68881) strcpy(m->fpu, "68881");
    else strcpy(m->fpu, "none");
    strcpy(m->chipset, "OCS");
    if ((gb = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 0))) {
        if (gb->ChipRevBits0 & GFXF_AA_ALICE) strcpy(m->chipset, "AGA");
        else if (gb->ChipRevBits0 & (GFXF_HR_AGNUS | GFXF_HR_DENISE)) strcpy(m->chipset, "ECS");
        CloseLibrary((struct Library *)gb);
    }
    os = v >= 50 ? "AROS" : v >= 47 ? "3.2" : v >= 46 ? "3.1.4" : v >= 44 ? "3.5/3.9" :
         v >= 40 ? "3.1" : v >= 39 ? "3.0" : v >= 37 ? "2.0x" : "1.x";
    sprintf(m->kickstart, "%u.%u (%s)", (unsigned)v, (unsigned)r, os);
    m->eclock = SysBase->ex_EClockFrequency;
    m->vblank = SysBase->VBlankFrequency;
}
