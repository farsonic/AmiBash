/*
 * util.c - memory, string buffers, lists, arena, getopt.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "sh.h"

static void oom(void)
{
    static const char msg[] = "bash: out of memory\n";
    os_write(os_stderr(), msg, sizeof(msg) - 1);
    os_exit(20);
}

#ifdef AMIBASH_PURE
/* The resident build has no C library heap: memory comes from AllocVec()
 * and every block is listed, so that all of it is freed when the shell
 * exits (the code may stay loaded and run again). */
#include <exec/memory.h>
#include <proto/exec.h>

typedef struct SysBlk { struct SysBlk *next, *prev; } SysBlk;
static SysBlk *sysblks;

static void *sys_alloc(size_t n)
{
    SysBlk *b = AllocVec(sizeof(SysBlk) + n, MEMF_ANY);
    if (!b) return NULL;
    b->prev = NULL;
    b->next = sysblks;
    if (sysblks) sysblks->prev = b;
    sysblks = b;
    return b + 1;
}

static void sys_free(void *p)
{
    SysBlk *b = (SysBlk *)p - 1;
    if (b->prev) b->prev->next = b->next;
    else sysblks = b->next;
    if (b->next) b->next->prev = b->prev;
    FreeVec(b);
}

void os_free_all(void)
{
    while (sysblks) sys_free(sysblks + 1);
}
#else
#define sys_alloc(n) malloc(n)
#define sys_free(p) free(p)
#endif

#if defined(AMIGA) || defined(SH_POOL)
/*
 * Pooled allocator.  The Amiga's AllocMem() (used by the C library's
 * malloc) walks one global first-fit free list; with the thousands of
 * small strings a shell keeps alive this becomes very slow on a 68000.
 * Small blocks are therefore carved from 16 KB chunks and recycled
 * through per-size free lists (O(1) alloc and free).
 */
#define POOL_CHUNK 16384
#define BIGCLASS 255

static const unsigned short cls_size[] = {
    8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096
};
#define NCLS (int)(sizeof(cls_size) / sizeof(cls_size[0]))

typedef union BlkHdr { struct { unsigned long cls; unsigned long size; } h; double align; } BlkHdr;
typedef struct FreeBlk { struct FreeBlk *next; } FreeBlk;

static FreeBlk *freelist[NCLS];
static char *chunk_ptr;
static size_t chunk_left;
static unsigned char cls_of[4097 / 8 + 1];
static int cls_ready;

static void cls_init(void)
{
    int c = 0;
    size_t n;
    for (n = 0; n <= 4096; n += 8) {
        while (cls_size[c] < n) c++;
        cls_of[n / 8] = (unsigned char)c;
    }
    cls_ready = 1;
}

void *xmalloc(size_t n)
{
    BlkHdr *b;
    int c;
    if (!cls_ready) cls_init();
    if (n == 0) n = 1;
    if (n > 4096) {
        b = sys_alloc(sizeof(BlkHdr) + n);
        if (!b) oom();
        b->h.cls = BIGCLASS;
        b->h.size = n;
        return b + 1;
    }
    c = cls_of[(n + 7) / 8];
    if (freelist[c]) {
        FreeBlk *f = freelist[c];
        freelist[c] = f->next;
        b = (BlkHdr *)f - 1;
        return b + 1;
    }
    {
        size_t need = sizeof(BlkHdr) + cls_size[c];
        if (chunk_left < need) {
            /* the tail of the old chunk is simply abandoned (< 4 KB) */
            chunk_ptr = sys_alloc(POOL_CHUNK);
            if (!chunk_ptr) oom();
            chunk_left = POOL_CHUNK;
        }
        b = (BlkHdr *)chunk_ptr;
        chunk_ptr += need;
        chunk_left -= need;
        b->h.cls = (unsigned long)c;
        b->h.size = cls_size[c];
        return b + 1;
    }
}

void xfree(void *p)
{
    BlkHdr *b;
    if (!p) return;
    b = (BlkHdr *)p - 1;
    if (b->h.cls == BIGCLASS) {
        sys_free(b);
        return;
    }
    ((FreeBlk *)p)->next = freelist[b->h.cls];
    freelist[b->h.cls] = (FreeBlk *)p;
}

void *xcalloc(size_t n)
{
    void *p = xmalloc(n);
    memset(p, 0, n);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    BlkHdr *b;
    void *q;
    if (!p) return xmalloc(n);
    b = (BlkHdr *)p - 1;
    if (n <= b->h.size && (b->h.cls != BIGCLASS || n * 2 > b->h.size)) return p;
    q = xmalloc(n);
    memcpy(q, p, b->h.size < n ? b->h.size : n);
    xfree(p);
    return q;
}
#else
void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n)
{
    void *p = xmalloc(n);
    memset(p, 0, n);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

void xfree(void *p)
{
    if (p) free(p);
}
#endif

char *xstrdup(const char *s)
{
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ---- string buffer ---- */

void sb_init(SBuf *b)
{
    b->s = NULL;
    b->len = b->cap = 0;
}

static void sb_grow(SBuf *b, int need)
{
    if (b->len + need + 1 > b->cap) {
        int nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + need + 1) nc *= 2;
        b->s = xrealloc(b->s, nc);
        b->cap = nc;
    }
}

void sb_addc(SBuf *b, int c)
{
    sb_grow(b, 1);
    b->s[b->len++] = (char)c;
    b->s[b->len] = 0;
}

void sb_addn(SBuf *b, const char *s, int n)
{
    if (n <= 0) {
        if (!b->s) sb_grow(b, 0), b->s[0] = 0;
        return;
    }
    sb_grow(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void sb_adds(SBuf *b, const char *s)
{
    sb_addn(b, s, (int)strlen(s));
}

void sb_vprintf(SBuf *b, const char *fmt, va_list ap)
{
    char tmp[256];
    int n;
    va_list ap2;
    va_copy(ap2, ap);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    if (n < (int)sizeof(tmp)) {
        sb_addn(b, tmp, n);
    } else {
        sb_grow(b, n);
        vsnprintf(b->s + b->len, n + 1, fmt, ap2);
        b->len += n;
    }
    va_end(ap2);
}

void sb_printf(SBuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(b, fmt, ap);
    va_end(ap);
}

char *sb_take(SBuf *b)
{
    char *s = b->s;
    if (!s) s = xstrdup("");
    b->s = NULL;
    b->len = b->cap = 0;
    return s;
}

void sb_clear(SBuf *b)
{
    b->len = 0;
    if (b->s) b->s[0] = 0;
}

void sb_free(SBuf *b)
{
    xfree(b->s);
    sb_init(b);
}

/* ---- string lists ---- */

void sl_init(StrList *l)
{
    l->v = NULL;
    l->n = l->cap = 0;
}

void sl_add(StrList *l, char *s)
{
    if (l->n + 2 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, l->cap * sizeof(char *));
    }
    l->v[l->n++] = s;
    l->v[l->n] = NULL;
}

void sl_adddup(StrList *l, const char *s)
{
    sl_add(l, xstrdup(s));
}

void sl_free(StrList *l)
{
    int i;
    for (i = 0; i < l->n; i++) xfree(l->v[i]);
    xfree(l->v);
    sl_init(l);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_strcase(const void *a, const void *b)
{
    int r = str_casecmp(*(char *const *)a, *(char *const *)b);
    return r ? r : strcmp(*(char *const *)a, *(char *const *)b);
}

void sl_sort(StrList *l, int nocase)
{
    if (l->n > 1) qsort(l->v, l->n, sizeof(char *), nocase ? cmp_strcase : cmp_str);
}

char **sl_argv(StrList *l)
{
    if (!l->v) sl_add(l, NULL), l->n = 0;
    return l->v;
}

/* ---- string helpers ---- */

int str_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
}

int str_ncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

int str_startswith(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

int str_endswith(const char *s, const char *p)
{
    size_t ls = strlen(s), lp = strlen(p);
    return ls >= lp && strcmp(s + ls - lp, p) == 0;
}

int str_isname(const char *s)
{
    if (!*s || !(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (s++; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
    return 1;
}

int str_isnum(const char *s)
{
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

char *str_cat(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = xmalloc(la + lb + 1);
    memcpy(p, a, la);
    memcpy(p + la, b, lb + 1);
    return p;
}

char *str_printf(const char *fmt, ...)
{
    SBuf b;
    va_list ap;
    sb_init(&b);
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    return sb_take(&b);
}

char *str_itoa(ShInt v)
{
    char buf[32];
    char *p = buf + sizeof(buf) - 1;
    unsigned long long u;
    int neg = v < 0;
    u = neg ? (unsigned long long)0 - (unsigned long long)v : (unsigned long long)v;
    *p = 0;
    if (u <= 0xFFFFFFFFUL) {
        /* 32-bit fast path: 64-bit division is a slow library call on m68k */
        unsigned long w = (unsigned long)u;
        do {
            *--p = (char)('0' + (int)(w % 10));
            w /= 10;
        } while (w);
        if (neg) *--p = '-';
        return xstrdup(p);
    }
    do {
        *--p = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (neg) *--p = '-';
    return xstrdup(p);
}

int str_toint(const char *s, ShInt *out)
{
    ShInt v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') neg = 1, s++;
    else if (*s == '+') s++;
    if (!isdigit((unsigned char)*s)) return 0;
    {
        /* accumulate in 32 bits while it cannot overflow */
        unsigned long w = 0;
        while (isdigit((unsigned char)*s) && w < 0x0CCCCCCCUL) w = w * 10 + (unsigned long)(*s++ - '0');
        v = (ShInt)w;
    }
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s) return 0;
    *out = neg ? -v : v;
    return 1;
}

/* ---- arena ---- */

#define ARENA_CHUNK 2000

typedef struct ArenaBlk { struct ArenaBlk *next; size_t used, size; } ArenaBlk;
struct Arena { ArenaBlk *blk; int refs; };

Arena *arena_new(void)
{
    Arena *a = xmalloc(sizeof(Arena));
    a->blk = NULL;
    a->refs = 1;
    return a;
}

void *arena_alloc(Arena *a, size_t n)
{
    ArenaBlk *b = a->blk;
    char *p;
    n = (n + 7) & ~(size_t)7;
    if (!b || b->used + n > b->size) {
        size_t sz = n > ARENA_CHUNK ? n : ARENA_CHUNK;
        b = xmalloc(sizeof(ArenaBlk) + 8 + sz);
        b->size = sz;
        b->used = 0;
        b->next = a->blk;
        a->blk = b;
    }
    p = (char *)(b + 1) + 8 + b->used;  /* +8 keeps 8-byte alignment */
    b->used += n;
    memset(p, 0, n);
    return p;
}

void arena_ref(Arena *a)
{
    if (a) a->refs++;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *arena_strdup(Arena *a, const char *s)
{
    return arena_strndup(a, s, strlen(s));
}

void arena_free(Arena *a)
{
    ArenaBlk *b, *n;
    if (!a || --a->refs > 0) return;
    for (b = a->blk; b; b = n) {
        n = b->next;
        xfree(b);
    }
    xfree(a);
}

/* ---- getopt ---- */

void sh_getopt_init(GetOpt *g, const char *cmdname)
{
    g->ind = 1;
    g->sp = 1;
    g->arg = NULL;
    g->opt = 0;
    g->name = cmdname;
}

int sh_getopt(GetOpt *g, int argc, char **argv, const char *optstring)
{
    const char *cp;
    int c;
    if (g->sp == 1) {
        if (g->ind >= argc || argv[g->ind][0] != '-' || argv[g->ind][1] == 0)
            return -1;
        if (strcmp(argv[g->ind], "--") == 0) {
            g->ind++;
            return -1;
        }
    }
    c = (unsigned char)argv[g->ind][g->sp];
    g->opt = c;
    cp = strchr(optstring, c);
    if (c == ':' || !cp) {
        sh_cmd_error(g->name, "-%c: invalid option", c);
        if (argv[g->ind][++g->sp] == 0) g->ind++, g->sp = 1;
        return '?';
    }
    if (cp[1] == ':') {
        if (argv[g->ind][g->sp + 1]) {
            g->arg = &argv[g->ind][g->sp + 1];
            g->ind++;
        } else if (g->ind + 1 < argc) {
            g->arg = argv[g->ind + 1];
            g->ind += 2;
        } else {
            sh_cmd_error(g->name, "-%c: option requires an argument", c);
            g->ind++;
            g->sp = 1;
            return '?';
        }
        g->sp = 1;
    } else {
        g->arg = NULL;
        if (argv[g->ind][++g->sp] == 0) {
            g->sp = 1;
            g->ind++;
        }
    }
    return c;
}
