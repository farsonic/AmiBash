/*
 * pure_amiga.c - start-up code of the resident ("pure") Amiga build.
 *
 * This build is linked with vbcc's minres.o instead of the C library
 * start-up.  Every run gets its own copy of the data and bss (addressed
 * through a4) while the code is shared, so one loaded copy of the
 * program can serve any number of shells: the terminal's tabs, or
 * "Resident C:bash PURE".
 *
 * minres.o only sets up a4 and calls main() with the argument string.
 * This file does the rest of what the C start-up normally does: it
 * opens dos.library, takes the Workbench message, splits the arguments,
 * makes sure there is a big enough stack, and frees everything when the
 * shell exits.
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <setjmp.h>
#include <string.h>
#include "sh.h"

struct DosLibrary *DOSBase;
struct WBStartup *_WBenchMsg;

#define STACK_WANT 65536UL

int sh_main(int argc, char **argv);
void os_free_all(void);

static jmp_buf exit_jb;
static int exit_status;
static int g_argc;
static char **g_argv;
static struct StackSwapStruct sss;
static BPTR self_seg;

/* Switch to the stack in *s, call fn and switch back.  Nothing of the
 * caller's frame may be used in between, so it is all done here. */
static LONG swap_call(__reg("a0") struct StackSwapStruct *s, __reg("a1") LONG (*fn)(void)) =
    "\tmovem.l\td2-d7/a2-a6,-(a7)\n"
    "\tmove.l\ta0,a2\n"
    "\tmove.l\ta1,a3\n"
    "\tmove.l\t4.w,a6\n"
    "\tjsr\t-732(a6)\n"
    "\tjsr\t(a3)\n"
    "\tmove.l\td0,d2\n"
    "\tmove.l\ta2,a0\n"
    "\tmove.l\t4.w,a6\n"
    "\tjsr\t-732(a6)\n"
    "\tmove.l\td2,d0\n"
    "\tmovem.l\t(a7)+,d2-d7/a2-a6";

/* Free this run's data (minres.o allocated it: a4 - 32766, _RESLEN
 * bytes) and return to the caller from the stack it called us with.
 * minres.o's own exit path finds that stack through pr_ReturnAddr,
 * which not every system sets. */
static void final_return(__reg("d2") LONG rc, __reg("a2") APTR sp) =
    "\tmove.l\ta4,a1\n"
    "\tsub.l\t#32766,a1\n"
    "\tmove.l\t#_RESLEN,d0\n"
    "\tmove.l\t4.w,a6\n"
    "\tjsr\t-210(a6)\n"
    "\tmove.l\ta2,a7\n"
    "\tmove.l\td2,d0\n"
    "\trts";

static LONG run(void)
{
    if (setjmp(exit_jb) == 0) exit_status = sh_main(g_argc, g_argv);
    return exit_status;
}

/* exit() for this build: unwind to main() below, which cleans up */
void pure_exit(int status)
{
    exit_status = status;
    longjmp(exit_jb, 1);
}

/* the loaded program, for the terminal to share with its tabs */
BPTR pure_seglist(void)
{
    return self_seg;
}

/* AmigaDOS command line -> argv: blanks separate words, "..." quotes
 * with *" *n *e ** escapes inside */
static void split_args(const char *name, const char *s)
{
    int cap = 8, n = 1;
    char **v = xmalloc(cap * sizeof(char *));
    char *buf = xmalloc(strlen(s) + 1), *o = buf;
    v[0] = xstrdup(name);
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        if (n + 1 >= cap) {
            cap *= 2;
            v = xrealloc(v, cap * sizeof(char *));
        }
        v[n++] = o;
        if (*s == '"') {
            s++;
            while (*s && *s != '"') {
                if (*s == '*' && s[1]) {
                    s++;
                    *o++ = *s == 'n' || *s == 'N' ? '\n' : *s == 'e' || *s == 'E' ? 27 : *s;
                    s++;
                } else
                    *o++ = *s++;
            }
            if (*s) s++;
        } else {
            while (*s && *s != ' ' && *s != '\t' && *s != '\n') *o++ = *s++;
        }
        *o++ = 0;
    }
    v[n] = NULL;
    g_argc = n;
    g_argv = v;
}

int main(char *args)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    char name[108];
    LONG rc;

    DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 36);
    if (!DOSBase) return 20;
    if (!me->pr_CLI) {
        WaitPort(&me->pr_MsgPort);
        _WBenchMsg = (struct WBStartup *)GetMsg(&me->pr_MsgPort);
        self_seg = _WBenchMsg->sm_Segment;
        strncpy(name, (char *)_WBenchMsg->sm_ArgList[0].wa_Name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        args = (char *)"";
    } else {
        self_seg = ((struct CommandLineInterface *)BADDR(me->pr_CLI))->cli_Module;
        if (!GetProgramName((STRPTR)name, sizeof(name))) strcpy(name, "bash");
    }
    split_args(name, args);

    /* the shell recurses (parser, expansion, functions): give it a big
     * stack whatever the Stack setting is */
    if (((ULONG)name < (ULONG)me->pr_Task.tc_SPLower || (ULONG)name > (ULONG)me->pr_Task.tc_SPUpper ||
         (ULONG)name - (ULONG)me->pr_Task.tc_SPLower < STACK_WANT - 4096) &&
        (sss.stk_Lower = AllocVec(STACK_WANT, MEMF_ANY)) != NULL) {
        sss.stk_Upper = (ULONG)sss.stk_Lower + STACK_WANT;
        sss.stk_Pointer = (APTR)sss.stk_Upper;
        rc = swap_call(&sss, run);
        FreeVec(sss.stk_Lower);
    } else
        rc = run();

    os_free_all();
    CloseLibrary((struct Library *)DOSBase);
    if (_WBenchMsg) {
        Forbid();
        ReplyMsg(&_WBenchMsg->sm_Message);
    }
    /* minres.o pushed the data pointer, then args and a return address */
    final_return(rc, (char *)&args + 8);
    return rc;
}

