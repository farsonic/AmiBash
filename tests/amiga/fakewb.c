/* fakewb DIR NAME - start a program the way Workbench does (WBStartup
 * message), to test Workbench launching without a running Workbench */
#include <exec/memory.h>
#include <dos/dostags.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
int main(int argc, char **argv)
{
    struct MsgPort *reply;
    struct WBStartup *wb;
    struct WBArg *wa;
    struct Process *p;
    BPTR seg, dir, old;
    if (argc < 3) return 20;
    dir = Lock((STRPTR)argv[1], SHARED_LOCK);
    if (!dir) return 20;
    old = CurrentDir(dir);
    seg = LoadSeg((STRPTR)argv[2]);
    CurrentDir(old);
    if (!seg) { Printf((STRPTR)"LoadSeg failed\n"); return 20; }
    reply = CreateMsgPort();
    wb = AllocVec(sizeof(*wb), MEMF_PUBLIC | MEMF_CLEAR);
    wa = AllocVec(sizeof(*wa), MEMF_PUBLIC | MEMF_CLEAR);
    p = CreateNewProcTags(NP_Seglist, seg, NP_FreeSeglist, FALSE, NP_Name, (ULONG)argv[2],
                          NP_StackSize, 65536, NP_CurrentDir, DupLock(dir),
                          NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE, NP_CloseOutput, FALSE,
                          TAG_END);
    if (!p) { Printf((STRPTR)"CreateNewProc failed\n"); return 20; }
    wa->wa_Lock = dir;
    wa->wa_Name = (BYTE *)argv[2];
    wb->sm_Message.mn_ReplyPort = reply;
    wb->sm_Message.mn_Length = sizeof(*wb);
    wb->sm_Process = &p->pr_MsgPort;
    wb->sm_Segment = seg;
    wb->sm_NumArgs = 1;
    wb->sm_ArgList = wa;
    PutMsg(&p->pr_MsgPort, &wb->sm_Message);
    WaitPort(reply);
    GetMsg(reply);
    Printf((STRPTR)"program returned its WBStartup message\n");
    UnLoadSeg(seg);
    UnLock(dir);
    return 0;
}
