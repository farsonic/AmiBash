/*
 * selftest.c - built-in self test ("bash SELFTEST"), used to check the
 * interpreter logic quickly under vamos without a full emulator.
 */
#include <string.h>
#include "sh.h"

static int fails, passes;

static void check_str(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) {
        passes++;
    } else {
        fails++;
        sh_printf(1, "FAIL %s\n  got:  [%s]\n  want: [%s]\n", what, got, want);
    }
}

/* run a script fragment and compare its captured stdout */
static void t(const char *script, const char *want)
{
    char *out = command_subst(script);
    check_str(script, out, want);
    xfree(out);
}

int sh_run_selftest(void)
{
    char *s;
    sh_puts(1, "AmiBash self test\n");

    /* path translation (Amiga rules, tested on every platform) */
    s = path_amiga_normalize("/dh0/work/x", "SYS:");
    check_str("path unix", s, "dh0:work/x"); xfree(s);
    s = path_amiga_normalize("../x", "DH0:a/b");
    check_str("path ..", s, "DH0:a/x"); xfree(s);
    s = path_amiga_normalize("..", "DH0:");
    check_str("path above root", s, "/"); xfree(s);
    s = path_amiga_normalize("../Work/y", "DH0:");
    check_str("path via vroot", s, "Work:y"); xfree(s);
    s = path_amiga_normalize("Work", "/");
    check_str("path from vroot", s, "Work:"); xfree(s);
    s = path_amiga_normalize(":Prefs", "SYS:Devs");
    check_str("path colon root", s, "SYS:Prefs"); xfree(s);
    s = path_amiga_normalize("RAM:t/./a/../b", NULL);
    check_str("path dots", s, "RAM:t/b"); xfree(s);
    s = path_join("DH0:", "x");
    check_str("join colon", s, "DH0:x"); xfree(s);
    s = path_dirname("DH0:foo");
    check_str("dirname colon", s, "DH0:"); xfree(s);

    /* patterns */
    check_str("pat *", pat_match("*.c", "main.c", 0) ? "y" : "n", "y");
    check_str("pat []", pat_match("[a-c]x", "bx", 0) ? "y" : "n", "y");
    check_str("pat nocase", pat_match("*.C", "main.c", PM_NOCASE) ? "y" : "n", "y");

    /* interpreter */
    t("echo hello world", "hello world");
    t("x=5; echo $((x * 3 + 1))", "16");
    t("a=(one two three); echo ${#a[@]} ${a[1]}", "3 two");
    t("s=abcdef; echo ${s:1:3} ${s#a*c} ${s%e*} ${s/cd/XX}", "bcd def abcd abXXef");
    t("for i in 1 2 3; do printf '%s,' $i; done", "1,2,3,");
    t("f() { local v=$1; echo \"<$v>\"; }; f 'a b'", "<a b>");
    t("echo {a,b}{1,2}", "a1 a2 b1 b2");
    t("case foo.txt in *.txt) echo text;; *) echo other;; esac", "text");
    t("x=1; (x=2); echo $x", "1");
    t("echo $(echo inner $(echo deep))", "inner deep");
    t("i=0; while [ $i -lt 3 ]; do i=$((i+1)); done; echo $i", "3");
    t("[[ abc == a* && 5 -gt 3 ]] && echo yes", "yes");
    t("[[ 2026-10-20 =~ ^([0-9]+)-([0-9]+) ]] && echo ${BASH_REMATCH[2]}", "10");
    t("echo one two | { read a b; echo $b; }", "two");
    t("printf '%05d|%-3s|%x\\n' 42 ab 255", "00042|ab |ff");
    t("declare -A m; m[k]=v; echo ${m[k]}", "v");
    t("set -- a b c; shift; echo $# $@", "2 b c");
    t("echo ${UNSET:-def} ${#UNSET}", "def 0");
    t("x=$'a\\tb'; echo \"${#x}\"", "3");
    t("cat <<EOF\nhere $((1+1))\nEOF", "here 2");
    t("echo abc | tr a-z A-Z", "ABC");
    t("printf 'b\\na\\nc\\n' | sort | head -n 2 | wc -l", "2");
    t("v=hello; echo ${v^^} ${v^}", "HELLO Hello");
    t("echo $(( 2 ** 10 )) $(( 17 % 5 )) $(( 1 < 2 ? 7 : 9 ))", "1024 2 7");

    sh_printf(1, "%d passed, %d failed\n", passes, fails);
    sh_flush_all();
    return fails ? 10 : 0;
}
