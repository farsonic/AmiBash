/* argtest - prints its ReadArgs() arguments, one per line, in [brackets];
 * used to verify AmiBash's argument quoting for native commands.
 * Returns the number of arguments as its return code. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/rdargs.h>

int main(void)
{
    LONG args[1] = {0};
    struct RDArgs *rd = ReadArgs((STRPTR)"ARGS/M", args, NULL);
    int n = 0;
    if (!rd) {
        PrintFault(IoErr(), (STRPTR)"argtest");
        return 20;
    }
    if (args[0]) {
        STRPTR *a = (STRPTR *)args[0];
        for (; *a; a++, n++) Printf((STRPTR)"[%s]\n", (LONG)*a);
    }
    FreeArgs(rd);
    return n;
}
