/* cdtest DIR - changes the current directory of the calling process,
 * like the AmigaDOS CD command does when run by a shell */
#include <proto/exec.h>
#include <proto/dos.h>
int main(int argc, char **argv)
{
    BPTR l;
    if (argc < 2) return 20;
    l = Lock((STRPTR)argv[1], SHARED_LOCK);
    if (!l) return 20;
    UnLock(CurrentDir(l));
    return 0;
}
