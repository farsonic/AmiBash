/*
 * test_regex.c - compare src/regex.c against glibc's regcomp/regexec.
 *
 * Build:  make -C tests/regex      (or see Makefile)
 * Run:    tests/regex/test_regex [-v] [nrandom] [seed]
 *
 * For every case both engines compile the pattern; compile success
 * must agree, then match/no-match and m[0] must agree.  Submatch
 * differences are counted and reported separately.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

/* our API (sh.h cannot be included next to <regex.h>: RE_ICASE clashes) */
typedef struct Regex Regex;
typedef struct ReMatch { int so, eo; } ReMatch;
Regex *re_compile(const char *pat, int flags, char **err);
int    re_exec(Regex *re, const char *s, int nm, ReMatch *m, int eflags);
int    re_nsub(Regex *re);
void   re_free(Regex *re);
#undef RE_ICASE
#define RE_ICASE   1
#define RE_BASIC   2
#define RE_NEWLINE 4
#define RE_NOTBOL  1

static int verbose;
static sigjmp_buf alrm_jb;
static long nhang;

static void on_alarm(int sig)
{
    (void)sig;
    siglongjmp(alrm_jb, 1);
}
static long ncase, nfail_comp, nfail_match, nfail_m0, ndiff_sub;
static int shown_comp, shown_match, shown_sub;

static char lastpat[3][512];
static int lastfl[3];

/* show each pattern only once per category (unless -v) */
static int fresh(int cat, const char *pat, int fl)
{
    if (verbose)
        return 1;
    if (lastfl[cat] == fl && !strcmp(lastpat[cat], pat))
        return 0;
    strncpy(lastpat[cat], pat, 511);
    lastfl[cat] = fl;
    return 1;
}

static void show(const char *pat, int fl, const char *s, int ef)
{
    const char *p;
    printf("  pat=\"");
    for (p = pat; *p; p++)
        if (*p == '\n') printf("\\n"); else putchar(*p);
    printf("\" %s%s%s%s subj=\"", (fl & RE_BASIC) ? "BRE" : "ERE",
           (fl & RE_ICASE) ? " ICASE" : "", (fl & RE_NEWLINE) ? " NEWLINE" : "",
           ef ? " NOTBOL" : "");
    for (p = s; *p; p++)
        if (*p == '\n') printf("\\n"); else putchar(*p);
    printf("\"\n");
}

#define NM 10

/* returns 0 ok, 1 hard failure */
static int check(const char *pat, int fl, const char *s, int ef)
{
    regex_t g;
    Regex *r;
    char *err = NULL;
    int gfl = 0, gok, rc, gm, mm, i, bad = 0;
    regmatch_t gmt[NM];
    ReMatch m[NM];

    ncase++;
    if (!(fl & RE_BASIC)) gfl |= REG_EXTENDED;
    if (fl & RE_ICASE) gfl |= REG_ICASE;
    if (fl & RE_NEWLINE) gfl |= REG_NEWLINE;
    gok = regcomp(&g, pat, gfl) == 0;
    r = re_compile(pat, fl, &err);
    if (gok != (r != NULL)) {
        nfail_comp++;
        if (fresh(0, pat, fl) && shown_comp++ < 60) {
            printf("COMPILE mismatch: glibc %s, ours %s (%s)\n", gok ? "ok" : "error",
                   r ? "ok" : "error", err ? err : "");
            show(pat, fl, "", 0);
        }
        free(err);
        if (gok) regfree(&g);
        re_free(r);
        return 1;
    }
    free(err);
    if (!gok)
        return 0;
    /* glibc's regexec can loop forever on some nested empty loops */
    if (sigsetjmp(alrm_jb, 1)) {
        nhang++;
        if (verbose) { printf("glibc hangs:\n"); show(pat, fl, s, ef); }
        re_free(r);
        return 0;
    }
    alarm(2);
    rc = regexec(&g, s, NM, gmt, ef ? REG_NOTBOL : 0);
    alarm(0);
    gm = rc == 0;
    mm = re_exec(r, s, NM, m, ef ? RE_NOTBOL : 0);
    /* also the cheap paths: nm == 0 and nm == 1 */
    if (re_exec(r, s, 0, NULL, ef ? RE_NOTBOL : 0) != mm) {
        printf("nm=0 disagrees with nm=10\n");
        show(pat, fl, s, ef);
        bad = 1;
    }
    if (mm) {
        ReMatch m1;
        re_exec(r, s, 1, &m1, ef ? RE_NOTBOL : 0);
        if (m1.so != m[0].so || m1.eo != m[0].eo) {
            printf("nm=1 m[0]=(%d,%d) disagrees with nm=10 (%d,%d)\n", m1.so, m1.eo, m[0].so, m[0].eo);
            show(pat, fl, s, ef);
            bad = 1;
        }
    }
    if (gm != mm) {
        nfail_match++;
        bad = 1;
        if (fresh(1, pat, fl) && shown_match++ < 80) {
            printf("MATCH mismatch: glibc %d ours %d\n", gm, mm);
            show(pat, fl, s, ef);
        }
    } else if (gm && (gmt[0].rm_so != m[0].so || gmt[0].rm_eo != m[0].eo)) {
        nfail_m0++;
        bad = 1;
        if (fresh(1, pat, fl) && shown_match++ < 80) {
            printf("M0 mismatch: glibc (%d,%d) ours (%d,%d)\n", (int)gmt[0].rm_so,
                   (int)gmt[0].rm_eo, m[0].so, m[0].eo);
            show(pat, fl, s, ef);
        }
    } else if (gm) {
        int n = re_nsub(r) + 1, d = 0;
        if (re_nsub(r) != (int)g.re_nsub) {
            printf("NSUB mismatch %d %d\n", re_nsub(r), (int)g.re_nsub);
            show(pat, fl, s, ef);
            bad = 1;
        }
        for (i = 1; i < n && i < NM; i++)
            if (gmt[i].rm_so != m[i].so || gmt[i].rm_eo != m[i].eo)
                d = 1;
        if (d) {
            ndiff_sub++;
            if (fresh(2, pat, fl) && shown_sub++ < 80) {
                printf("SUB diff:");
                for (i = 1; i < n && i < NM; i++)
                    printf(" \\%d glibc(%d,%d) ours(%d,%d)", i, (int)gmt[i].rm_so,
                           (int)gmt[i].rm_eo, m[i].so, m[i].eo);
                printf("\n");
                show(pat, fl, s, ef);
            }
        }
    }
    regfree(&g);
    re_free(r);
    return bad;
}

/* ---- fixed cases ---- */
static const char *ere_pats[] = {
    "", "a", "abc", "a*", "a+", "a?", "a|b", "ab|cd", "(a|ab)(c|bcd)(d*)",
    "(a*)*", "(a*)+", "(a|b)*", "((a)|b)*", "(a*)(a*)", "x*(a*)", "(.*)\\.(.*)",
    "^abc$", "^a", "a$", "^$", "^", "$", "a{2}", "a{2,}", "a{,2}", "a{1,3}",
    "a{0}", "(a){0}b", "[abc]", "[^abc]", "[]a]", "[^]a]", "[a-]", "[-a]",
    "[a-c-e]", "[[:alpha:]]+", "[[:digit:]]+", "[[:alnum:]_]+", "[[:upper:]]",
    "[[:lower:]]", "[[:space:]]", "[[:blank:]]", "[[:punct:]]+", "[[:print:]]+",
    "[[:graph:]]+", "[[:cntrl:]]", "[[:xdigit:]]+", "[[:foo:]]", "[[.a.]]",
    "[[=a=]]", "[[.a.]-c]", "[a-[.c.]]", "[z-a]", "[", "[a", "[^", "a]", "a)",
    "(", "(a", ")", "a{", "a{1", "a{1,", "a{x}", "a{2,1}", "{1}", "*a", "+a",
    "a**", "a+*", "a*?", "(*a)", "a|*b", "^*", "$*", "\\", "a\\", "\\.", "\\*",
    "\\(", "\\{", "\\|", "\\<a", "a\\>", "\\ba", "a\\b", "\\Ba", "\\w+",
    "\\W+", "\\s+", "\\S+", "\\`a", "a\\'", "(a)\\1", "(a*)\\1", "(a|b)\\1",
    "\\1", "(a\\1)", "()", "(|a)", "a||b", "|a", "a|", "(a|)+", "(ab|a)(bc|c)",
    "(a+|b+)*c", "(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)", "a.c", "a.*c", ".*", ".+",
    "(a|b|c|d|e)+", "((((a))))", "(a(b(c)))", "a(b)?c", "(a)?(b)?(c)?",
    "[.]", "[*]", "[\\]", "[a\\]b]", "x{0,0}", "(ab){2}", "(a{2}){2}",
    "a{1}{2}", "^(a|b)*$", "(^a|b)", "(a$|b)", "a^b", "a$b", "\\(a\\)",
    "a{1,2}{3}", "(foo|foobar)", "(foobar|foo)", "(a|ab)*c", "(a*)b\\1",
    "(wee|week)(knights|night)", "(.*)(.*)", "(a*)*b", "(a|aa)*", "(aa|a)*",
    "([a-c]*)\\1", "(x|xy)(z|yz)", "[[:alpha:]-]", "[[:alpha:]-z]",
    "[a-[:alpha:]]", "[[:alpha:]", "[[:alpha", "[[.ab.]]", "[[==]]",
    NULL
};

static const char *bre_pats[] = {
    "", "a", "a*", "*a", "\\(*a\\)", "^*", "a\\{2\\}", "a\\{2,\\}", "a\\{,2\\}",
    "\\(a\\)\\1", "\\(a*\\)\\1", "\\(a\\|b\\)*", "a\\|b", "a\\+", "a\\?", "a+",
    "a?", "a|b", "(a)", "a{2}", "\\(ab\\)*c", "^a", "a^", "a$", "$a", "a$b",
    "^^", "$$", "\\(^a\\)", "\\(a$\\)", "a\\|^b", "a$\\|b", "x\\(a\\|b$\\)",
    "\\{1\\}", "a\\{1", "a\\{1\\}\\{2\\}", "a**", "a*\\{2\\}", "a\\{2\\}*",
    "a\\+*", "a*\\+", "\\)", "\\(", "\\(a", "a\\)", "\\(\\)", "[ab]*",
    "\\(.*\\)\\1", "\\([a-c]*\\) \\1", "\\(a\\)\\(b\\)\\2\\1", ".\\{3\\}",
    "\\<", "\\>", "\\(a\\|ab\\)\\(c\\|bcd\\)", "\\(\\(a\\)\\|b\\)*\\2",
    "\\(a*\\)*\\1", "\\(a\\)*\\1", "\\(.\\)\\1*", "*", "**", "\\(*\\)",
    "a\\|*b", "^\\{1\\}", "a\\{,\\}", "a\\{\\}", "\\(a\\)\\{0\\}b",
    NULL
};

static const char *subjects[] = {
    "", "a", "b", "aa", "ab", "abc", "abcd", "aaa", "aaaa", "xabcx", "abab",
    "foobar", "foo", "a.b.c", "ABC", "aBc", "hello world", "a\nb", "\nab\n",
    "ab\ncd", "a]b", "a-b", "x{1}", "a)", "a*", "*a", "weeknights", "xyz",
    "a b  c", "  ", "123abc", "a_b9!", "\t x", "abcabc", "aabb", "baaa",
    "abbc", "{1}", "a{", "a|b", "a+b", "a?", "(a)", "AbAb", "\\", "a\\b",
    "^a", "a$", "$a", "a^b", "a$b", "abcdefghijk", "abab abab", "abcab",
    NULL
};

/* ---- random generation ---- */
static unsigned long rs = 1;
static int rnd(int n)
{
    rs = rs * 1103515245UL + 12345UL;
    return (int)((rs >> 16) % (unsigned long)n);
}

static void gen_ere(char *b, int *n, int depth, int max, int *ngrp, int allowbref)
{
    int k = rnd(depth > 2 ? 6 : 12), i, cnt;
    if (*n > max - 20) { b[(*n)++] = 'a'; return; }
    switch (k) {
    case 0: case 1: case 2: b[(*n)++] = "abc"[rnd(3)]; break;
    case 3: b[(*n)++] = '.'; break;
    case 4: {
        static const char *cl[] = { "[ab]", "[^a]", "[a-c]", "[[:alpha:]]", "[^b\n]", "[b]" };
        const char *c = cl[rnd(6)];
        memcpy(b + *n, c, strlen(c)); *n += (int)strlen(c);
        break;
    }
    case 5:
        if (allowbref && *ngrp > 0 && rnd(3) == 0) {
            b[(*n)++] = '\\'; b[(*n)++] = (char)('1' + rnd(*ngrp));
        } else
            /* anchors only outside groups: glibc mishandles e.g. (^a)+ */
            b[(*n)++] = depth == 0 && rnd(4) ? "^$"[rnd(2)] : 'b';
        break;
    case 6: case 7: case 8:
        /* group */
        b[(*n)++] = '(';
        cnt = 1 + rnd(3);
        for (i = 0; i < cnt; i++) {
            if (i) b[(*n)++] = '|';
            {
                int j, len = rnd(3) + (rnd(5) ? 1 : 0);
                for (j = 0; j < len; j++)
                    gen_ere(b, n, depth + 1, max, ngrp, allowbref);
            }
        }
        b[(*n)++] = ')';
        if (*ngrp < 9) ++*ngrp;
        break;
    default: {
        int j, len = 1 + rnd(3);
        for (j = 0; j < len; j++)
            gen_ere(b, n, depth + 1, max, ngrp, allowbref);
        break;
    }
    }
    /* quantifier */
    if (k != 5 && rnd(3) == 0) {
        static const char *q[] = { "*", "+", "?", "{2}", "{0,1}", "{1,2}", "{2,}", "{0}" };
        const char *c = q[rnd(8)];
        memcpy(b + *n, c, strlen(c)); *n += (int)strlen(c);
    }
}

/* ERE -> BRE spelling of the same pattern (anchors only where valid) */
static void to_bre(const char *e, char *b)
{
    for (; *e; e++) {
        if (*e == '[') {             /* copy bracket verbatim */
            const char *q = e + 1;
            if (*q == '^') q++;
            if (*q == ']') q++;
            while (*q && *q != ']') {
                if (*q == '[' && q[1] == ':') { q = strstr(q, ":]") + 2; continue; }
                q++;
            }
            memcpy(b, e, (size_t)(q - e + 1)); b += q - e + 1; e = q;
            continue;
        }
        if (*e == '\\') { *b++ = *e++; *b++ = *e; continue; }
        if (strchr("(){}|+?", *e)) *b++ = '\\';
        *b++ = *e;
    }
    *b = 0;
}

static void rand_subject(char *s, int nl)
{
    int n = rnd(12), i;
    for (i = 0; i < n; i++)
        s[i] = nl && rnd(8) == 0 ? '\n' : "abcAB"[rnd(rnd(4) ? 3 : 5)];
    s[n] = 0;
}

int main(int argc, char **argv)
{
    int i, j, f, nrand = 20000, a = 1;
    static const int ffl[] = { 0, RE_ICASE, RE_NEWLINE, RE_ICASE | RE_NEWLINE };
    long t;

    signal(SIGALRM, on_alarm);
    if (a < argc && !strcmp(argv[a], "-v")) { verbose = 1; a++; }
    if (a + 1 < argc && !strcmp(argv[a], "-p")) {
        /* probe: -p pattern [B][I][N][T] subject : show both results */
        const char *o = a + 2 < argc ? argv[a + 2] : "";
        int fl = 0, ef = 0, k, gfl = 0;
        regex_t g; regmatch_t gm[NM]; ReMatch m[NM]; Regex *r; char *err = NULL;
        if (strchr(o, 'B')) fl |= RE_BASIC; else gfl |= REG_EXTENDED;
        if (strchr(o, 'I')) { fl |= RE_ICASE; gfl |= REG_ICASE; }
        if (strchr(o, 'N')) { fl |= RE_NEWLINE; gfl |= REG_NEWLINE; }
        if (strchr(o, 'T')) ef = 1;
        k = regcomp(&g, argv[a + 1], gfl);
        printf("glibc: compile %d", k);
        if (!k) {
            k = regexec(&g, a + 3 < argc ? argv[a + 3] : "", NM, gm, ef ? REG_NOTBOL : 0);
            printf(" match %d", !k);
            if (!k) for (i = 0; i <= (int)g.re_nsub && i < NM; i++) printf(" (%d,%d)", (int)gm[i].rm_so, (int)gm[i].rm_eo);
        }
        r = re_compile(argv[a + 1], fl, &err);
        printf("\nours:  compile %s", r ? "ok" : err);
        if (r) {
            k = re_exec(r, a + 3 < argc ? argv[a + 3] : "", NM, m, ef);
            printf(" match %d", k);
            if (k) for (i = 0; i <= re_nsub(r) && i < NM; i++) printf(" (%d,%d)", m[i].so, m[i].eo);
        }
        printf("\n");
        return 0;
    }
    if (a < argc) nrand = atoi(argv[a++]);
    if (a < argc) rs = strtoul(argv[a++], NULL, 10);

    /* fixed cases */
    for (i = 0; ere_pats[i]; i++)
        for (f = 0; f < 4; f++)
            for (j = 0; subjects[j]; j++) {
                check(ere_pats[i], ffl[f], subjects[j], 0);
                if (f == 2 && j % 4 == 0) check(ere_pats[i], ffl[f], subjects[j], 1);
            }
    for (i = 0; bre_pats[i]; i++)
        for (f = 0; f < 4; f++)
            for (j = 0; subjects[j]; j++) {
                check(bre_pats[i], ffl[f] | RE_BASIC, subjects[j], 0);
                if (f == 2 && j % 4 == 0) check(bre_pats[i], ffl[f] | RE_BASIC, subjects[j], 1);
            }
    printf("fixed: %ld cases\n", ncase);

    /* random structured patterns */
    for (t = 0; t < nrand; t++) {
        char pat[400], bpat[800], subj[64];
        int n = 0, ng = 0, k, fl = ffl[rnd(4)], bref = rnd(4) == 0;
        int len = 1 + rnd(4), maxlen = 20 + rnd(3) * 20;
        for (k = 0; k < len; k++)
            gen_ere(pat, &n, 0, maxlen, &ng, bref);
        pat[n] = 0;
        for (k = 0; k < 6; k++) {
            rand_subject(subj, fl & RE_NEWLINE);
            check(pat, fl, subj, rnd(8) == 0);
        }
        if (rnd(3) == 0) {
            to_bre(pat, bpat);
            rand_subject(subj, fl & RE_NEWLINE);
            check(bpat, fl | RE_BASIC, subj, 0);
        }
    }
    /* random junk for the parser */
    for (t = 0; t < nrand; t++) {
        char pat[16], subj[64];
        static const char alpha[] = "ab()[]{}|*+?^$.\\-,12:";
        int n = rnd(10), k;
        for (k = 0; k < n; k++) pat[k] = alpha[rnd((int)sizeof alpha - 1)];
        pat[n] = 0;
        rand_subject(subj, 0);
        check(pat, rnd(2) ? RE_BASIC : 0, subj, 0);
    }
    if (nhang)
        printf("skipped %ld cases where glibc regexec hung\n", nhang);
    printf("total %ld cases: compile mismatches %ld, match mismatches %ld, "
           "m[0] mismatches %ld, submatch diffs %ld\n",
           ncase, nfail_comp, nfail_match, nfail_m0, ndiff_sub);
    return (nfail_comp || nfail_match || nfail_m0) ? 1 : 0;
}
