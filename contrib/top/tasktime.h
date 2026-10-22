/*
 * tasktime.h - CPU time per task, memory and system figures for AmigaOS.
 *
 * Exec does not count the CPU time of tasks.  tt_open() gives every task a
 * launch and a switch hook (tc_Launch, tc_Switch) that time each slice
 * with the E-clock; earlier hooks of a task are still called.  tt_close()
 * takes them away again.  The time no task used is idle time.
 */
#ifndef TASKTIME_H
#define TASKTIME_H

#include <exec/types.h>
#include <exec/tasks.h>

#define TT_NAMELEN 40

typedef struct TaskInfo {
    struct Task *task;
    char name[TT_NAMELEN];       /* task name                           */
    char cmd[TT_NAMELEN];        /* the command a CLI process runs, or "" */
    BYTE pri;
    UBYTE type;                  /* NT_TASK or NT_PROCESS               */
    UBYTE state;                 /* TS_RUN, TS_READY, TS_WAIT           */
    UBYTE pad;
    LONG cli;                    /* CLI number, or 0                    */
    ULONG stack_size;
    ULONG stack_used;            /* at the moment, from its stack pointer */
    ULONG sigwait;               /* signals it waits for                */
    ULONG cpu;                   /* share of the CPU, 0..1000 (per mille), last interval */
    ULONG cpu_ms;                /* CPU time since tt_open(), milliseconds */
} TaskInfo;

typedef struct SysInfo {
    ULONG cpu;                   /* busy, 0..1000, last interval        */
    ULONG interval_ms;           /* length of the last interval         */
    ULONG chip_free, chip_total, chip_largest;
    ULONG fast_free, fast_total, fast_largest;
    int ntasks, nready, nwait;   /* tasks found (also beyond maxtasks)  */
    int timed;                   /* per-task times work                 */
    ULONG uptime_s;              /* since tt_open()                     */
    ULONG dispatches;            /* task switches in the last interval  */
} SysInfo;

typedef struct MachineInfo {
    char cpu[16];                /* "68030", "68040"...                 */
    char fpu[16];                /* "68882", "68040 FPU" or "none"      */
    char chipset[16];            /* "AGA", "ECS", "OCS"                 */
    char kickstart[24];          /* "40.68 (3.1)"                       */
    ULONG eclock;                /* E-clock frequency, Hz               */
    UWORD vblank;                /* 50 or 60                            */
} MachineInfo;

/* a / b in per mille (0..1000 when a <= b), without floating point */
ULONG tt_permille(ULONG a, ULONG b);

int  tt_open(void);              /* 0 ok                                */
void tt_close(void);
/* fill sys and up to max tasks (sorted as the caller likes later);
 * returns the number of TaskInfo filled */
int  tt_sample(SysInfo *sys, TaskInfo *tasks, int max);
void tt_machine(MachineInfo *m);

#endif
