/* re_posix.c - stand-in for src/regex.c using the host's POSIX regex */
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "sh.h"

struct Regex { regex_t r; };

Regex *re_compile(const char *pat, int flags, char **err)
{
    Regex *re = xmalloc(sizeof(Regex));
    int f = 0, rc;
    if (!(flags & RE_BASIC)) f |= REG_EXTENDED;
    if (flags & RE_ICASE) f |= REG_ICASE;
    if (flags & RE_NEWLINE) f |= REG_NEWLINE;
    if (flags & RE_NOSUB) f |= REG_NOSUB;
    rc = regcomp(&re->r, pat, f);
    if (rc) {
        char buf[256];
        regerror(rc, &re->r, buf, sizeof(buf));
        if (err) *err = xstrdup(buf);
        xfree(re);
        return NULL;
    }
    return re;
}

int re_exec(Regex *re, const char *s, int nm, ReMatch *m, int eflags)
{
    regmatch_t pm[20];
    int i, rc;
    if (nm > 20) nm = 20;
    rc = regexec(&re->r, s, nm, pm, (eflags & RE_NOTBOL) ? REG_NOTBOL : 0);
    if (rc) return 0;
    for (i = 0; i < nm; i++) {
        m[i].so = (int)pm[i].rm_so;
        m[i].eo = (int)pm[i].rm_eo;
    }
    return 1;
}

int re_nsub(Regex *re)
{
    return (int)re->r.re_nsub;
}

void re_free(Regex *re)
{
    regfree(&re->r);
    xfree(re);
}
