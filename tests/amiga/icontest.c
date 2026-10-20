/* icontest NAME - check that NAME.info is a valid icon */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <workbench/workbench.h>
struct Library *IconBase;
int main(int argc, char **argv)
{
    struct DiskObject *d;
    if (argc < 2 || !(IconBase = OpenLibrary((STRPTR)"icon.library", 36))) return 20;
    d = GetDiskObject((STRPTR)argv[1]);
    if (!d) { Printf((STRPTR)"no icon\n"); return 10; }
    Printf((STRPTR)"type=%ld w=%ld h=%ld depth=%ld stack=%ld tt0=%s\n", (LONG)d->do_Type,
           (LONG)d->do_Gadget.Width, (LONG)d->do_Gadget.Height,
           (LONG)((struct Image *)d->do_Gadget.GadgetRender)->Depth, d->do_StackSize,
           (LONG)(d->do_ToolTypes && d->do_ToolTypes[0] ? d->do_ToolTypes[0] : (STRPTR)"-"));
    FreeDiskObject(d);
    CloseLibrary(IconBase);
    return 0;
}
