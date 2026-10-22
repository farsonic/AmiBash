/*
 * tasktime.c - CPU time per task, memory and system figures for AmigaOS.
 * See tasktime.h.
 *
 * A vertical blank interrupt server notes, 50 or 60 times a second, the
 * task that is running (or that the CPU is idle) in a ring; tt_sample()
 * counts those notes.  Exec's own task switching is left alone: hooks on
 * tc_Launch and tc_Switch run inside the dispatcher, where calling
 * timer.device (or anything that does Disable()/Enable()) locks up
 * Kickstart 3.x.
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <dos/dosextens.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>
#include <stdio.h>

#include "tasktime.h"

extern struct ExecBase *SysBase;
struct Device *TimerBase;               /* ReadEClock() for the intervals */

void tt_vblank(void);                   /* tthooks.s: calls tt_tick() */

/* ---- the interrupt side ------------------------------------------------- */

#define RING 1024                       /* notes: > 10 s at 60 Hz */
#define IDLE ((struct Task *)0)

static struct Task *volatile ring[RING];
static volatile ULONG ring_head;        /* written only by the interrupt */
static struct Interrupt vbint;

void tt_tick(void)
{
    struct Task *t = SysBase->ThisTask;
    ULONG h = ring_head;
    /* while exec idles, ThisTask is the last task, no longer running */
    ring[h % RING] = t && t->tc_State == TS_RUN ? t : IDLE;
    ring_head = h + 1;
}

/* ---- tasks' totals -------------------------------------------------------- */

#define NSLOT 512
#define TOMB  ((struct Task *)1)

typedef struct Slot {
    struct Task *task;
    APTR name;                          /* a new task in old memory has another name pointer */
    ULONG ticks;                        /* notes in this interval        */
    ULONG total;                        /* notes since tt_open()         */
    UBYTE seen;
} Slot;

static Slot slots[NSLOT];
static ULONG ring_tail;
static struct MsgPort *tport;
static struct timerequest *treq;
static struct EClockVal last;
static ULONG efreq;
static ULONG lastdisp;
static unsigned long long uptime;       /* E-clock ticks since tt_open() */
static UWORD vbfreq;
static int opened, inted;

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
            s->name = t->tc_Node.ln_Name;
            return s;
        }
    }
    return NULL;
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
    vbfreq = SysBase->VBlankFrequency ? SysBase->VBlankFrequency : 50;
    uptime = 0;
    ring_head = ring_tail = 0;
    vbint.is_Node.ln_Type = NT_INTERRUPT;
    vbint.is_Node.ln_Pri = -60;
    vbint.is_Node.ln_Name = "tasktime";
    vbint.is_Data = NULL;
    vbint.is_Code = (void (*)())tt_vblank;
    AddIntServer(INTB_VERTB, &vbint);
    inted = 1;
    opened = 1;
    return 0;
}

void tt_close(void)
{
    if (!opened) return;
    if (inted) RemIntServer(INTB_VERTB, &vbint);
    inted = 0;
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
    c->sys->ntasks++;
    if (state == TS_READY) c->sys->nready++;
    if (state == TS_WAIT) c->sys->nwait++;
    if (s && s->name != t->tc_Node.ln_Name) {
        /* another task now lives where an old one was */
        s->task = TOMB;
        s = NULL;
    }
    if (!s) s = insert(t);
    if (s) s->seen = 1;
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
            /* notes for now; the CPU share is worked out after Permit() */
            i->cpu = s->ticks;
            i->cpu_ms = s->total * 1000 / vbfreq;
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
    ULONG elapsed, head, notes, idle = 0, disp, k;
    int i;

    memset(sys, 0, sizeof(*sys));
    c.ti = ti;
    c.max = max;
    c.n = 0;
    c.sys = sys;
    Forbid();
    ReadEClock(&now);
    /* count the notes since the last time */
    for (k = 0; k < NSLOT; k++) {
        slots[k].ticks = 0;
        slots[k].seen = 0;
    }
    head = ring_head;
    if (head - ring_tail > RING) ring_tail = head - RING;
    notes = head - ring_tail;
    for (; ring_tail != head; ring_tail++) {
        struct Task *t = ring[ring_tail % RING];
        Slot *s;
        if (t == IDLE) {
            idle++;
            continue;
        }
        if (!(s = find(t)) && t->tc_Node.ln_Type != 0) s = insert(t);
        if (s) {
            s->ticks++;
            s->total++;
        }
    }
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
    for (i = 0; i < c.n; i++) ti[i].cpu = tt_permille(ti[i].cpu, notes);
    sys->cpu = notes ? 1000 - tt_permille(idle, notes) : 0;
    sys->timed = notes > 0;
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
