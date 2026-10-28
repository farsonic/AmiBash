/*
 * regex.c - POSIX extended and basic regular expressions.
 *
 * A pattern is parsed (syntax rules follow glibc's regcomp) into a
 * small tree, which is compiled into a program for a Pike VM: a
 * Thompson NFA simulation carrying submatch positions per thread.
 * Matching is O(len(subject) * len(program)), needs no recursion and
 * never backtracks.  The overall match is POSIX leftmost-longest; the
 * submatches are those of the highest priority path (greedy loops,
 * earlier alternatives first) that yields that overall match.
 *
 * Patterns with back-references cannot be handled by an NFA; they use
 * a backtracking matcher with an explicit heap stack and a step budget.
 * Pure literal patterns take a plain substring search.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "sh.h"

/* use the shell's allocator: error strings are released with xfree(),
 * and the resident Amiga build has no C library heap */
#define malloc(n) xmalloc(n)
#define realloc(p, n) xrealloc(p, n)
#define calloc(n, m) xcalloc((n) * (m))
#define free(p) xfree(p)

#define MAXDEPTH  32          /* parser nesting limit ( ( ( ...       */
#define MAXCDEPTH 160         /* compiler tree depth limit             */
#define MAXPROG   20000       /* compiled program size limit           */
#define DUPMAX    32767       /* largest {n,m} count (as glibc)        */
#define BT_BUDGET 1000000L    /* backtracker steps per re_exec         */

/* error codes (messages as glibc) */
enum {
    E_OK, E_BADPAT, E_COLLATE, E_CTYPE, E_ESCAPE, E_SUBREG, E_BRACK,
    E_PAREN, E_BRACE, E_BADBR, E_RANGE, E_SPACE, E_BADRPT, E_SIZE,
    E_RPAREN
};
static const char *const re_errmsg[] = {
    "Success", "Invalid regular expression", "Invalid collation character",
    "Invalid character class name", "Trailing backslash",
    "Invalid back reference", "Unmatched [, [^, [:, [., or [=",
    "Unmatched ( or \\(", "Unmatched \\{", "Invalid content of \\{\\}",
    "Invalid range end", "Memory exhausted",
    "Invalid preceding regular expression", "Regular expression too big",
    "Unmatched ) or \\)"
};

/* ---- program ---------------------------------------------------- */
enum {
    /* consuming instructions (and MATCH) first: threads park on these */
    O_CHAR, O_CHAR2, O_ANY, O_ANYNL, O_CLASS, O_MATCH,
    /* epsilon instructions */
    O_SPLIT, O_JMP, O_SAVE, O_LSET, O_LCHK, O_LCHK2, O_BREF,
    O_BOL, O_EOL, O_BOS, O_EOS, O_WORDB, O_NWORDB, O_WBEG, O_WEND
};

typedef struct Inst { int op, x, y; } Inst;

struct Regex {
    Inst *prog;
    int nprog;
    int nthr;                 /* instructions a thread can park on     */
    unsigned char *cls;       /* 32-byte bitmaps                       */
    int flags;
    int nsub;
    int bt;                   /* back-references: backtracking matcher */
    int nslot;                /* capture + loop register slots         */
    unsigned char first[32];  /* possible first bytes of a match       */
    int hasfirst;
    int anch;                 /* 1: only at string start, 2: line start */
    char *lit;                /* pure literal pattern (or NULL)        */
    int litlen;
    /* work areas, reused between calls */
    unsigned *mark;
    unsigned gen;
    int *stk;                 /* closure stack: 2*(nprog+1) ints       */
    int wcap;                 /* capture slots the lists are sized for */
    int *wpc[2], *wcp[2];
    int *scratch, *best;
    int *bstk;                /* backtracker stack                     */
    long bsz;
};

#define BIT(set, c)    ((set)[(c) >> 3] & (1 << ((c) & 7)))
#define SETBIT(set, c) ((set)[(c) >> 3] |= (unsigned char)(1 << ((c) & 7)))

/* ---- syntax tree ------------------------------------------------ */
enum {
    T_EMPTY, T_CHAR, T_ANY, T_CLASS, T_CAT, T_ALT, T_REP, T_GROUP,
    T_ASSERT, T_BREF
};

typedef struct RNode {
    int type;
    int a, b;                 /* char/class/group/op; REP min, max     */
    int kid, last, next;      /* children list (CAT, ALT), sibling     */
} RNode;

/* tokens */
enum {
    K_END, K_CHAR, K_ANY, K_LBRACK, K_LPAREN, K_RPAREN, K_ALT, K_STAR,
    K_PLUS, K_QMARK, K_LBRACE, K_RBRACE, K_ANCHOR, K_BREF, K_CLSESC,
    K_BSEND
};

typedef struct RxParser {
    const unsigned char *pat, *p;
    int ere, icase, nl;
    int err;
    int tok, tc;              /* current token and its character       */
    RNode *nd;
    int nn, ndcap;
    unsigned char *cls;
    int ncls, clscap;
    int ngroup;
    unsigned long done;       /* groups 1..31 already closed           */
    int depth;
    int hasbref;
} RxParser;

static int seterr(RxParser *P, int e)
{
    if (!P->err)
        P->err = e;
    return -1;
}

static int mknode(RxParser *P, int type, int a)
{
    RNode *n;
    if (P->err)
        return -1;
    if (P->nn >= P->ndcap) {
        int nc = P->ndcap ? P->ndcap * 2 : 32;
        RNode *nd = (RNode *)realloc(P->nd, nc * sizeof(RNode));
        if (!nd)
            return seterr(P, E_SPACE);
        P->nd = nd;
        P->ndcap = nc;
    }
    n = &P->nd[P->nn];
    n->type = type;
    n->a = a;
    n->b = 0;
    n->kid = n->last = n->next = -1;
    return P->nn++;
}

static void addkid(RxParser *P, int parent, int kid)
{
    RNode *p = &P->nd[parent];
    if (p->last < 0)
        p->kid = kid;
    else
        P->nd[p->last].next = kid;
    p->last = kid;
}

/* new class bitmap; returns its index */
static int newcls(RxParser *P, const unsigned char *set)
{
    if (P->ncls >= P->clscap) {
        int nc = P->clscap ? P->clscap * 2 : 4;
        unsigned char *c = (unsigned char *)realloc(P->cls, nc * 32);
        if (!c)
            return seterr(P, E_SPACE);
        P->cls = c;
        P->clscap = nc;
    }
    memcpy(P->cls + P->ncls * 32, set, 32);
    return P->ncls++;
}

/* finish a set: case folding, negation, no NUL; returns T_CLASS node */
static int clsnode(RxParser *P, unsigned char *set, int neg)
{
    int i, ix;
    if (P->icase) {
        unsigned char f[32];
        memset(f, 0, sizeof f);
        for (i = 1; i < 256; i++)
            if (BIT(set, i) || BIT(set, tolower(i)) || BIT(set, toupper(i)))
                SETBIT(f, i);
        memcpy(set, f, 32);
    }
    if (neg) {
        for (i = 0; i < 32; i++)
            set[i] = (unsigned char)~set[i];
        if (P->nl)
            set['\n' >> 3] &= (unsigned char)~(1 << ('\n' & 7));
    }
    set[0] &= 0xfe;
    ix = newcls(P, set);
    if (ix < 0)
        return -1;
    return mknode(P, T_CLASS, ix);
}

static int isword(int c)
{
    return c == '_' || isalnum(c);
}

/* add character class 'name' to set; 0 if unknown */
static int addclass(RxParser *P, unsigned char *set, const char *name)
{
    static const char *const names[] = {
        "alpha", "digit", "alnum", "upper", "lower", "space", "blank",
        "punct", "print", "graph", "cntrl", "xdigit", NULL
    };
    int k, c, ok;
    for (k = 0; names[k]; k++)
        if (!strcmp(names[k], name))
            break;
    if (!names[k])
        return 0;
    if (P->icase && (k == 3 || k == 4))
        k = 0;                      /* [:upper:] / [:lower:] -> alpha */
    for (c = 1; c < 256; c++) {
        switch (k) {
        case 0:  ok = isalpha(c); break;
        case 1:  ok = isdigit(c); break;
        case 2:  ok = isalnum(c); break;
        case 3:  ok = isupper(c); break;
        case 4:  ok = islower(c); break;
        case 5:  ok = isspace(c); break;
        case 6:  ok = c == ' ' || c == '\t'; break;
        case 7:  ok = ispunct(c); break;
        case 8:  ok = isprint(c); break;
        case 9:  ok = isgraph(c); break;
        case 10: ok = iscntrl(c); break;
        default: ok = isxdigit(c); break;
        }
        if (ok)
            SETBIT(set, c);
    }
    return 1;
}

/* ---- tokenizer -------------------------------------------------- */
/* caret: '^' is an anchor here even in a BRE (after \( or \|) */
static void fetch(RxParser *P, int caret)
{
    const unsigned char *p = P->p;
    int c, ere = P->ere;

    P->tc = 0;
    if (!*p) {
        P->tok = K_END;
        return;
    }
    c = *p++;
    P->tok = K_CHAR;
    P->tc = c;
    if (c == '\\') {
        if (!*p) {
            P->tok = K_BSEND;
            P->p = p;
            return;
        }
        c = *p++;
        P->tc = c;
        switch (c) {
        case '|': if (!ere) P->tok = K_ALT; break;
        case '(': if (!ere) P->tok = K_LPAREN; break;
        case ')': if (!ere) P->tok = K_RPAREN; break;
        case '{': if (!ere) P->tok = K_LBRACE; break;
        case '}': if (!ere) P->tok = K_RBRACE; break;
        case '+': if (!ere) P->tok = K_PLUS; break;
        case '?': if (!ere) P->tok = K_QMARK; break;
        case '<':  P->tok = K_ANCHOR; P->tc = O_WBEG; break;
        case '>':  P->tok = K_ANCHOR; P->tc = O_WEND; break;
        case 'b':  P->tok = K_ANCHOR; P->tc = O_WORDB; break;
        case 'B':  P->tok = K_ANCHOR; P->tc = O_NWORDB; break;
        case '`':  P->tok = K_ANCHOR; P->tc = O_BOS; break;
        case '\'': P->tok = K_ANCHOR; P->tc = O_EOS; break;
        case 'w': case 'W': case 's': case 'S': case 'd': case 'D':
            P->tok = K_CLSESC;
            break;
        default:
            if (c >= '1' && c <= '9') {
                P->tok = K_BREF;
                P->tc = c - '0';
            }
            break;
        }
    } else {
        switch (c) {
        case '|': if (ere) P->tok = K_ALT; break;
        case '*': P->tok = K_STAR; break;
        case '+': if (ere) P->tok = K_PLUS; break;
        case '?': if (ere) P->tok = K_QMARK; break;
        case '{': if (ere) P->tok = K_LBRACE; break;
        case '}': if (ere) P->tok = K_RBRACE; break;
        case '(': if (ere) P->tok = K_LPAREN; break;
        case ')': if (ere) P->tok = K_RPAREN; break;
        case '[': P->tok = K_LBRACK; break;
        case '.': P->tok = K_ANY; break;
        case '^':
            if (ere || caret || p - 1 == P->pat) {
                P->tok = K_ANCHOR;
                P->tc = O_BOL;
            }
            break;
        case '$':
            if (ere || !*p || (p[0] == '\\' && (p[1] == '|' || p[1] == ')'))) {
                P->tok = K_ANCHOR;
                P->tc = O_EOL;
            }
            break;
        }
    }
    P->p = p;
}

/* ---- parser ----------------------------------------------------- */
static int parse_reg(RxParser *P, int nest);

enum { BE_CHAR, BE_CLASS, BE_EQUIV };

/* one bracket element at *pp: char, [.c.], [=c=] or [:class:] */
static int brack_elem(RxParser *P, const unsigned char **pp, int *ch,
                      unsigned char *set, int accept_hyphen)
{
    const unsigned char *p = *pp;
    if (*p == '[' && (p[1] == '.' || p[1] == '=' || p[1] == ':')) {
        char name[32];
        int delim = p[1], i, c;
        p += 2;
        if (!*p)
            return seterr(P, E_BRACK);
        for (i = 0;; i++) {
            if (i >= 32)
                return seterr(P, E_BRACK);
            c = *p++;
            if (!*p)
                return seterr(P, E_BRACK);
            if (c == delim && *p == ']')
                break;
            name[i] = (char)c;
        }
        p++;
        name[i] = 0;
        *pp = p;
        if (delim == ':') {
            if (!addclass(P, set, name))
                return seterr(P, E_CTYPE);
            return BE_CLASS;
        }
        if (i != 1)
            return seterr(P, E_COLLATE);
        *ch = (unsigned char)name[0];
        return delim == '=' ? BE_EQUIV : BE_CHAR;
    }
    if (*p == '-' && !accept_hyphen && p[1] != ']')
        return seterr(P, E_RANGE);
    *ch = *p++;
    *pp = p;
    return BE_CHAR;
}

/* bracket expression; P->p is just past '[' */
static int parse_bracket(RxParser *P)
{
    unsigned char set[32];
    const unsigned char *p = P->p;
    int neg = 0, first = 1, t, t2, c1, c2, i;

    memset(set, 0, sizeof set);
    if (!*p)
        return seterr(P, E_BADPAT);     /* "[" at the end: as glibc */
    if (*p == '^') {
        neg = 1;
        p++;
        if (!*p)
            return seterr(P, E_BADPAT);
    }
    for (;;) {
        int range = 0;
        if (!*p)
            return seterr(P, E_BRACK);
        t = brack_elem(P, &p, &c1, set, first);
        if (t < 0)
            return -1;
        first = 0;
        if (t == BE_CHAR) {
            if (!*p)
                return seterr(P, E_BRACK);
            if (*p == '-') {
                if (!p[1])
                    return seterr(P, E_BRACK);
                if (p[1] != ']') {
                    p++;
                    t2 = brack_elem(P, &p, &c2, set, 1);
                    if (t2 < 0)
                        return -1;
                    if (t2 != BE_CHAR || c1 > c2)
                        return seterr(P, E_RANGE);
                    for (i = c1; i <= c2; i++)
                        SETBIT(set, i);
                    range = 1;
                }
            }
        }
        if (!range && t != BE_CLASS)
            SETBIT(set, c1);
        if (!*p)
            return seterr(P, E_BRACK);
        if (*p == ']') {
            p++;
            break;
        }
    }
    P->p = p;
    return clsnode(P, set, neg);
}

/* {n,m} number, glibc rules: -1 no digits, -2 invalid */
static int fetch_num(RxParser *P)
{
    int num = -1;
    for (;;) {
        fetch(P, 0);
        if (P->tok == K_END)
            return -2;
        if (P->tok == K_RBRACE || P->tc == ',')
            break;
        if (P->tok != K_CHAR || P->tc < '0' || P->tc > '9' || num == -2)
            num = -2;
        else if (num == -1)
            num = P->tc - '0';
        else {
            num = num * 10 + P->tc - '0';
            if (num > DUPMAX)
                num = DUPMAX + 1;
        }
    }
    return num;
}

/* apply the repetition operator in P->tok to node n */
static int parse_dup(RxParser *P, int n)
{
    int lo, hi = 0, r;
    if (P->tok == K_LBRACE) {
        lo = fetch_num(P);
        if (lo == -1) {
            if (P->tok == K_CHAR && P->tc == ',')
                lo = 0;                 /* {,m} is {0,m} */
            else
                return seterr(P, E_BADBR);
        }
        if (lo != -2) {
            if (P->tok == K_RBRACE)
                hi = lo;
            else if (P->tok == K_CHAR && P->tc == ',')
                hi = fetch_num(P);
            else
                hi = -2;
        }
        if (lo == -2 || hi == -2)
            return seterr(P, P->tok == K_END ? E_BRACE : E_BADBR);
        if ((hi != -1 && lo > hi) || P->tok != K_RBRACE)
            return seterr(P, E_BADBR);
        if ((hi == -1 ? lo : hi) > DUPMAX)
            return seterr(P, E_SIZE);
    } else {
        lo = P->tok == K_PLUS;
        hi = P->tok == K_QMARK ? 1 : -1;
    }
    fetch(P, 0);
    if (P->err)
        return -1;
    if (lo == 0 && hi == 0)
        return mknode(P, T_EMPTY, 0);
    if (lo == 1 && hi == 1)
        return n;
    if (P->nd[n].type == T_REP) {
        /* fold x** x+? x?* ... into one loop */
        RNode *k = &P->nd[n];
        if (k->a <= 1 && k->b == -1 && lo <= 1 && (hi == -1 || hi == 1)) {
            k->a = k->a * lo;
            return n;
        }
        if (k->a == 0 && k->b == 1 && lo <= 1 && (hi == -1 || hi == 1)) {
            k->b = hi;
            return n;
        }
    }
    r = mknode(P, T_REP, lo);
    if (r < 0)
        return -1;
    P->nd[r].b = hi;
    P->nd[r].kid = n;
    return r;
}

static int parse_group(RxParser *P, int nest)
{
    int idx = ++P->ngroup, kid, n;
    if (++P->depth > MAXDEPTH)
        return seterr(P, E_SIZE);
    fetch(P, 1);
    if (P->tok == K_RPAREN)
        kid = mknode(P, T_EMPTY, 0);
    else {
        kid = parse_reg(P, nest + 1);
        if (P->err)
            return -1;
        if (P->tok != K_RPAREN)
            return seterr(P, E_PAREN);
    }
    P->depth--;
    if (idx < 32)
        P->done |= 1UL << idx;
    n = mknode(P, T_GROUP, idx);
    if (n >= 0)
        P->nd[n].kid = kid;
    return n;
}

/* \w \W \s \S \d \D (kept out of parse_expr: its frame is recursive) */
static int clsesc(RxParser *P, int c)
{
    unsigned char set[32];
    int n, i, nl = P->nl;

    memset(set, 0, sizeof set);
    switch (c | 0x20) {
    case 'w': addclass(P, set, "alnum"); SETBIT(set, '_'); break;
    case 's': addclass(P, set, "space"); break;
    default:  addclass(P, set, "digit"); break;
    }
    if (c & 0x20)
        return clsnode(P, set, 0);
    for (i = 0; i < 32; i++)
        set[i] = (unsigned char)~set[i];
    P->nl = 0;                      /* \W \S match newline (as glibc) */
    n = clsnode(P, set, 0);
    P->nl = nl;
    return n;
}

static int parse_expr(RxParser *P, int nest)
{
    int n;

    switch (P->tok) {
    case K_CHAR:
        n = mknode(P, T_CHAR, P->tc);
        break;
    case K_ANY:
        n = mknode(P, T_ANY, 0);
        break;
    case K_LBRACK:
        n = parse_bracket(P);
        break;
    case K_LPAREN:
        n = parse_group(P, nest);
        break;
    case K_BREF:
        if (!(P->done & (1UL << P->tc)))
            return seterr(P, E_SUBREG);
        P->hasbref = 1;
        n = mknode(P, T_BREF, P->tc);
        break;
    case K_LBRACE:
        return seterr(P, E_BADRPT);
    case K_STAR:
    case K_PLUS:
    case K_QMARK:
        if (P->ere)
            return seterr(P, E_BADRPT);
        n = mknode(P, T_CHAR, P->tc);   /* BRE: literal at start */
        break;
    case K_RPAREN:
        if (!P->ere)
            return seterr(P, E_RPAREN);
        n = mknode(P, T_CHAR, ')');
        break;
    case K_RBRACE:
        n = mknode(P, T_CHAR, '}');
        break;
    case K_ANCHOR:
        /* anchors take no repetition; '^' may follow */
        n = mknode(P, T_ASSERT, P->tc);
        fetch(P, P->ere);
        return n;
    case K_CLSESC:
        n = clsesc(P, P->tc);
        break;
    case K_BSEND:
        return seterr(P, E_ESCAPE);
    default:
        return seterr(P, E_BADPAT);
    }
    if (n < 0 || P->err)
        return -1;
    fetch(P, 0);
    while (P->tok == K_STAR || P->tok == K_PLUS || P->tok == K_QMARK ||
           P->tok == K_LBRACE) {
        n = parse_dup(P, n);
        if (n < 0)
            return -1;
        if (!P->ere && (P->tok == K_STAR || P->tok == K_LBRACE))
            return seterr(P, E_BADRPT);
    }
    return n;
}

static int at_branch_end(RxParser *P, int nest)
{
    return P->tok == K_ALT || P->tok == K_END ||
           (nest > 0 && P->tok == K_RPAREN);
}

static int parse_branch(RxParser *P, int nest)
{
    int n, cat = -1, e;
    if (at_branch_end(P, nest))
        return mknode(P, T_EMPTY, 0);
    n = parse_expr(P, nest);
    while (n >= 0 && !at_branch_end(P, nest)) {
        e = parse_expr(P, nest);
        if (e < 0)
            return -1;
        if (cat < 0) {
            cat = mknode(P, T_CAT, 0);
            if (cat < 0)
                return -1;
            addkid(P, cat, n);
            n = cat;
        }
        addkid(P, cat, e);
    }
    return n;
}

static int parse_reg(RxParser *P, int nest)
{
    unsigned long done0 = P->done, acc;
    int n = parse_branch(P, nest), alt, b;
    if (n < 0 || P->tok != K_ALT)
        return n;
    alt = mknode(P, T_ALT, 0);
    if (alt < 0)
        return -1;
    addkid(P, alt, n);
    while (P->tok == K_ALT) {
        fetch(P, 1);
        /* as glibc: no back-reference into an earlier alternative */
        acc = P->done;
        P->done = done0;
        b = parse_branch(P, nest);
        P->done |= acc;
        if (b < 0)
            return -1;
        addkid(P, alt, b);
    }
    return alt;
}

/* ---- compiler --------------------------------------------------- */
typedef struct RxComp {
    RxParser *P;
    Inst *prog;
    int n, cap;
    int bt;                   /* emit empty-loop guards                */
    int nreg;                 /* next free loop register               */
    int err;
} RxComp;

static int emit(RxComp *C, int op, int x, int y)
{
    if (C->err)
        return -1;
    if (C->n >= C->cap) {
        int nc = C->cap ? C->cap * 2 : 64;
        Inst *p;
        if (C->n >= MAXPROG) {
            C->err = E_SIZE;
            return -1;
        }
        p = (Inst *)realloc(C->prog, nc * sizeof(Inst));
        if (!p) {
            C->err = E_SPACE;
            return -1;
        }
        C->prog = p;
        C->cap = nc;
    }
    C->prog[C->n].op = op;
    C->prog[C->n].x = x;
    C->prog[C->n].y = y;
    return C->n++;
}

static int nullable(RxParser *P, int n)
{
    RNode *r = &P->nd[n];
    int k;
    switch (r->type) {
    case T_CHAR: case T_ANY: case T_CLASS:
        return 0;
    case T_CAT:
        for (k = r->kid; k >= 0; k = P->nd[k].next)
            if (!nullable(P, k))
                return 0;
        return 1;
    case T_ALT:
        for (k = r->kid; k >= 0; k = P->nd[k].next)
            if (nullable(P, k))
                return 1;
        return 0;
    case T_REP:
        return r->a == 0 || nullable(P, r->kid);
    case T_GROUP:
        return nullable(P, r->kid);
    default:
        return 1;
    }
}

static void comp(RxComp *C, int n, int depth);

/*
 * Loops whose body can match empty need care.  x* is compiled as
 *     lset r; L: split B, out; B: [lset r+1] x; lchk2 r, out; jmp L
 * lchk2 leaves the loop (keeping the captures of the empty iteration,
 * as POSIX/glibc do) when nothing at all was consumed since the loop
 * was entered (Pike VM; a later empty iteration dies on the per-step
 * marks), or when the iteration was empty (backtracker, r+1).  x+ is
 *     L: [lset r+1] x; split C, out; C: [lchk r+1]; jmp L
 * Registers live in capture slots past the groups.
 */
static void comp_rep(RxComp *C, RNode *r, int depth)
{
    int lo = r->a, hi = r->b, kid = r->kid, i, L, S, reg = -1, chain;

    for (i = 0; i < lo - (hi == -1 ? 1 : 0); i++)
        comp(C, kid, depth);
    if (hi == -1) {
        if (nullable(C->P, kid)) {
            reg = 2 * (C->P->ngroup + 1) + C->nreg;
            C->nreg += 2;
        }
        if (lo == 0) {
            if (reg >= 0)
                emit(C, O_LSET, reg, 0);
            L = emit(C, O_SPLIT, C->n + 1, 0);
            if (reg >= 0 && C->bt)
                emit(C, O_LSET, reg + 1, 0);
            comp(C, kid, depth);
            S = reg >= 0 ? emit(C, O_LCHK2, reg, 0) : -1;
            emit(C, O_JMP, L, 0);
            if (!C->err) {
                C->prog[L].y = C->n;
                if (S >= 0)
                    C->prog[S].y = C->n;
            }
        } else {
            L = C->n;
            if (reg >= 0 && C->bt)
                emit(C, O_LSET, reg + 1, 0);
            comp(C, kid, depth);
            if (reg >= 0 && C->bt) {
                S = emit(C, O_SPLIT, C->n + 1, 0);
                emit(C, O_LCHK, reg + 1, 0);
                emit(C, O_JMP, L, 0);
                if (!C->err)
                    C->prog[S].y = C->n;
            } else
                emit(C, O_SPLIT, L, C->n + 1);
        }
    } else {
        /* optional copies, nested: (x(x(x)?)?)? */
        chain = -1;
        for (i = lo; i < hi && !C->err; i++) {
            S = emit(C, O_SPLIT, C->n + 1, chain);
            chain = S;
            comp(C, kid, depth);
        }
        while (chain >= 0 && !C->err) {
            S = C->prog[chain].y;
            C->prog[chain].y = C->n;
            chain = S;
        }
    }
}

static void comp(RxComp *C, int n, int depth)
{
    RxParser *P = C->P;
    RNode *r;
    int k, S, chain, c;

    if (C->err)
        return;
    if (++depth > MAXCDEPTH) {
        C->err = E_SIZE;
        return;
    }
    r = &P->nd[n];
    switch (r->type) {
    case T_CHAR:
        c = r->a;
        if (P->icase && tolower(c) != toupper(c))
            emit(C, O_CHAR2, tolower(c), toupper(c));
        else
            emit(C, O_CHAR, c, 0);
        break;
    case T_ANY:
        emit(C, P->nl ? O_ANYNL : O_ANY, 0, 0);
        break;
    case T_CLASS:
        emit(C, O_CLASS, r->a, 0);
        break;
    case T_CAT:
        for (k = r->kid; k >= 0; k = P->nd[k].next)
            comp(C, k, depth);
        break;
    case T_ALT:
        chain = -1;
        for (k = r->kid; k >= 0 && !C->err; k = P->nd[k].next) {
            if (P->nd[k].next >= 0) {
                S = emit(C, O_SPLIT, C->n + 1, 0);
                comp(C, k, depth);
                chain = emit(C, O_JMP, chain, 0);
                if (!C->err)
                    C->prog[S].y = C->n;
            } else
                comp(C, k, depth);
        }
        while (chain >= 0 && !C->err) {
            S = C->prog[chain].x;
            C->prog[chain].x = C->n;
            chain = S;
        }
        break;
    case T_GROUP:
        emit(C, O_SAVE, 2 * r->a, 0);
        comp(C, r->kid, depth);
        emit(C, O_SAVE, 2 * r->a + 1, 0);
        break;
    case T_ASSERT:
        emit(C, r->a, 0, 0);
        break;
    case T_BREF:
        emit(C, O_BREF, r->a, 0);
        break;
    case T_REP:
        comp_rep(C, r, depth);
        break;
    }
}

/* walk the epsilon closure of the program start.  pass 0 collects the
 * set of possible first bytes (0 if a match can be empty); pass 1
 * checks whether every path begins with ^ (returns 1) */
static int scan(Regex *re, int pass, unsigned char *vis, int *st)
{
    Inst *prog = re->prog, *ip;
    int sp = 0, pc, i;

    memset(vis, 0, re->nprog);
    st[sp++] = 0;
    while (sp > 0) {
        pc = st[--sp];
        while (!vis[pc]) {
            vis[pc] = 1;
            ip = &prog[pc];
            if (pass == 1) {
                if (ip->op == O_BOL || ip->op == O_BOS)
                    break;
                if (ip->op <= O_MATCH || ip->op == O_BREF)
                    return 0;
            }
            switch (ip->op) {
            case O_CHAR:
                SETBIT(re->first, ip->x);
                break;
            case O_CHAR2:
                SETBIT(re->first, ip->x);
                SETBIT(re->first, ip->y);
                break;
            case O_ANY:
            case O_ANYNL:
                for (i = 1; i < 256; i++)
                    if (ip->op == O_ANY || i != '\n')
                        SETBIT(re->first, i);
                break;
            case O_CLASS:
                for (i = 0; i < 32; i++)
                    re->first[i] |= re->cls[ip->x * 32 + i];
                break;
            case O_MATCH:
            case O_BREF:
                return 0;
            case O_SPLIT:
                st[sp++] = ip->y;
                pc = ip->x;
                continue;
            case O_JMP:
                pc = ip->x;
                continue;
            default:
                pc++;
                continue;
            }
            break;
        }
    }
    return 1;
}

static void analyze(Regex *re)
{
    unsigned char *vis = (unsigned char *)malloc(re->nprog);
    int *st = (int *)malloc((re->nprog + 1) * sizeof(int));
    int i, cnt = 0;

    if (vis && st) {
        if (scan(re, 0, vis, st)) {
            for (i = 1; i < 256; i++)
                if (BIT(re->first, i))
                    cnt++;
            re->hasfirst = cnt < 255;
        }
        if (scan(re, 1, vis, st))
            re->anch = (re->flags & RE_NEWLINE) ? 2 : 1;
    }
    free(vis);
    free(st);
}

static char *errstr(int e)
{
    const char *m = re_errmsg[e];
    char *s = (char *)malloc(strlen(m) + 1);
    if (s)
        strcpy(s, m);
    return s;
}

void re_free(Regex *re)
{
    if (!re)
        return;
    free(re->prog);
    free(re->cls);
    free(re->lit);
    free(re->mark);
    free(re->stk);
    free(re->wpc[0]);
    free(re->wpc[1]);
    free(re->wcp[0]);
    free(re->wcp[1]);
    free(re->scratch);
    free(re->best);
    free(re->bstk);
    free(re);
}

Regex *re_compile(const char *pat, int flags, char **err)
{
    RxParser P;
    RxComp C;
    Regex *re = NULL;
    int root, e = E_OK, i, k;

    if (err)
        *err = NULL;
    memset(&P, 0, sizeof P);
    memset(&C, 0, sizeof C);
    P.pat = P.p = (const unsigned char *)pat;
    P.ere = !(flags & RE_BASIC);
    P.icase = (flags & RE_ICASE) != 0;
    P.nl = (flags & RE_NEWLINE) != 0;
    fetch(&P, 1);
    root = parse_reg(&P, 0);
    if (!P.err && P.tok != K_END)
        P.err = P.ere ? E_BADPAT : E_RPAREN;
    if (P.err) {
        e = P.err;
        goto fail;
    }
    re = (Regex *)calloc(1, sizeof(Regex));
    if (!re) {
        e = E_SPACE;
        goto fail;
    }
    re->flags = flags;
    re->nsub = P.ngroup;
    re->bt = P.hasbref;

    /* pure literal: plain substring search */
    if (!P.ngroup) {
        RNode *r = &P.nd[root];
        int n = 0, ok = 1;
        if (r->type == T_CAT) {
            for (k = r->kid; k >= 0; k = P.nd[k].next, n++)
                if (P.nd[k].type != T_CHAR)
                    ok = 0;
        } else if (r->type == T_CHAR)
            n = 1;
        else if (r->type != T_EMPTY)
            ok = 0;
        if (ok) {
            re->lit = (char *)malloc(n + 1);
            if (!re->lit) {
                e = E_SPACE;
                goto fail;
            }
            if (r->type == T_CHAR)
                re->lit[0] = (char)r->a;
            else if (r->type == T_CAT)
                for (i = 0, k = r->kid; k >= 0; k = P.nd[k].next)
                    re->lit[i++] = (char)P.nd[k].a;
            if (P.icase)
                for (i = 0; i < n; i++)
                    re->lit[i] = (char)tolower((unsigned char)re->lit[i]);
            re->lit[n] = 0;
            re->litlen = n;
            free(P.nd);
            free(P.cls);
            return re;
        }
    }

    C.P = &P;
    C.bt = re->bt;
    emit(&C, O_SAVE, 0, 0);
    comp(&C, root, 0);
    emit(&C, O_SAVE, 1, 0);
    emit(&C, O_MATCH, 0, 0);
    if (C.err) {
        e = C.err;
        goto fail;
    }
    re->prog = C.prog;
    C.prog = NULL;
    re->nprog = C.n;
    re->cls = P.cls;
    P.cls = NULL;
    re->nslot = 2 * (P.ngroup + 1) + C.nreg;
    for (i = 0; i < re->nprog; i++)
        if (re->prog[i].op <= O_MATCH)
            re->nthr++;
    re->mark = (unsigned *)calloc(re->nprog, sizeof(unsigned));
    re->stk = (int *)malloc(2 * (re->nprog + 1) * sizeof(int));
    if (!re->mark || !re->stk) {
        e = E_SPACE;
        goto fail;
    }
    analyze(re);
    free(P.nd);
    return re;

fail:
    free(P.nd);
    free(P.cls);
    free(C.prog);
    re_free(re);
    if (err)
        *err = errstr(e);
    return NULL;
}

int re_nsub(Regex *re)
{
    return re ? re->nsub : 0;
}

/* ---- matching --------------------------------------------------- */
typedef struct Ctx {
    Regex *re;
    const unsigned char *s;
    int len;
    int notbol, nl, icase;
    int ncap;
} Ctx;

static int assert_ok(Ctx *x, int op, int pos)
{
    const unsigned char *s = x->s;
    int a, b;
    switch (op) {
    case O_BOL:
        return (pos == 0 && !x->notbol) || (pos > 0 && x->nl && s[pos - 1] == '\n');
    case O_EOL:
        return pos == x->len || (x->nl && s[pos] == '\n');
    case O_BOS:
        return pos == 0;
    case O_EOS:
        return pos == x->len;
    }
    a = pos > 0 && isword(s[pos - 1]);
    b = pos < x->len && isword(s[pos]);
    switch (op) {
    case O_WORDB:  return a != b;
    case O_NWORDB: return a == b;
    case O_WBEG:   return !a && b;
    default:       return a && !b;      /* O_WEND */
    }
}

static unsigned newgen(Regex *re)
{
    if (++re->gen == 0) {
        memset(re->mark, 0, re->nprog * sizeof(unsigned));
        re->gen = 1;
    }
    return re->gen;
}

/* follow epsilon edges from pc in priority order, parking threads in
 * the list (lpc, lcap, ln).  cap is modified and restored. */
static void addthread(Ctx *x, int *lpc, int *lcap, int *ln, int pc, int pos,
                      int *cap, unsigned gen)
{
    Regex *re = x->re;
    Inst *prog = re->prog, *ip;
    unsigned *mark = re->mark;
    int *stk = re->stk, sp = 0, ncap = x->ncap, i, v;

    stk[sp++] = pc;
    stk[sp++] = 0;
    while (sp > 0) {
        sp -= 2;
        pc = stk[sp];
        if (pc < 0) {                   /* restore a capture slot */
            cap[-1 - pc] = stk[sp + 1];
            continue;
        }
        for (;;) {
            if (mark[pc] == gen)
                break;
            mark[pc] = gen;
            ip = &prog[pc];
            switch (ip->op) {
            case O_JMP:
                pc = ip->x;
                continue;
            case O_SPLIT:
                stk[sp++] = ip->y;
                stk[sp++] = 0;
                pc = ip->x;
                continue;
            case O_SAVE:
            case O_LSET:
                v = ip->x;
                if (v < ncap) {
                    stk[sp++] = -1 - v;
                    stk[sp++] = cap[v];
                    cap[v] = pos;
                }
                pc++;
                continue;
            case O_LCHK2:
                if (ip->x < ncap && cap[ip->x] == pos)
                    pc = ip->y;         /* loop matched empty: leave */
                else
                    pc++;
                continue;
            case O_LCHK:
                pc++;
                continue;
            case O_BOL: case O_EOL: case O_BOS: case O_EOS:
            case O_WORDB: case O_NWORDB: case O_WBEG: case O_WEND:
                if (!assert_ok(x, ip->op, pos))
                    break;
                pc++;
                continue;
            default:                    /* consuming or MATCH: park */
                i = (*ln)++;
                lpc[i] = pc;
                if (ncap)
                    memcpy(lcap + i * ncap, cap, ncap * sizeof(int));
                break;
            }
            break;
        }
    }
}

static int ensure_work(Regex *re, int ncap)
{
    int i, n = re->nthr;
    if (re->wpc[0] && re->wcap >= ncap)
        return 1;
    for (i = 0; i < 2; i++) {
        free(re->wpc[i]);
        free(re->wcp[i]);
        re->wpc[i] = (int *)malloc(n * sizeof(int));
        re->wcp[i] = (int *)malloc((n * ncap + 1) * sizeof(int));
    }
    free(re->best);
    re->best = (int *)malloc((ncap + 1) * sizeof(int));
    re->wcap = ncap;
    if (!re->wpc[0] || !re->wpc[1] || !re->wcp[0] || !re->wcp[1] || !re->best) {
        re->wcap = -1;
        return 0;
    }
    return 1;
}

/* can a match start at pos? (cheap pre-checks) */
static int can_start(Regex *re, const unsigned char *s, int pos, int len)
{
    if (re->anch && pos > 0 && (re->anch == 1 || s[pos - 1] != '\n'))
        return 0;
    if (re->hasfirst && (pos >= len || !BIT(re->first, s[pos])))
        return 0;
    return 1;
}

/* next position >= pos where a match could start, or -1 */
static int next_start(Regex *re, const unsigned char *s, int pos, int len)
{
    for (;; pos++) {
        if (re->anch && pos > 0) {
            if (re->anch == 1)
                return -1;
            while (pos <= len && s[pos - 1] != '\n')
                pos++;
            if (pos > len)
                return -1;
        }
        if (!re->hasfirst)
            return pos;
        while (pos < len && !BIT(re->first, s[pos]))
            pos++;
        if (pos >= len)
            return -1;
        if (!re->anch || pos == 0 || s[pos - 1] == '\n')
            return pos;
    }
}

static int pike(Ctx *x, int *best)
{
    Regex *re = x->re;
    Inst *prog = re->prog, *ip;
    const unsigned char *s = x->s;
    int len = x->len, ncap = x->ncap;
    int *cpc = re->wpc[0], *npc = re->wpc[1];
    int *ccp = re->wcp[0], *ncp = re->wcp[1], *t;
    int cn = 0, nn, pos = 0, matched = 0, bst = 0, i, c, ok;
    unsigned cgen, ngen;
    int *cap = re->scratch;

    cgen = newgen(re);
    for (;;) {
        if (!matched) {
            if (cn == 0) {
                pos = next_start(re, s, pos, len);
                if (pos < 0)
                    break;
                cgen = newgen(re);
                for (i = 0; i < ncap; i++)
                    cap[i] = -1;
                addthread(x, cpc, ccp, &cn, 0, pos, cap, cgen);
            } else if (can_start(re, s, pos, len)) {
                for (i = 0; i < ncap; i++)
                    cap[i] = -1;
                addthread(x, cpc, ccp, &cn, 0, pos, cap, cgen);
            }
        }
        c = pos < len ? s[pos] : 0;
        ngen = newgen(re);
        nn = 0;
        for (i = 0; i < cn; i++) {
            t = ccp + i * ncap;
            if (matched && t[0] > bst)
                continue;               /* starts right of the match */
            ip = &prog[cpc[i]];
            switch (ip->op) {
            case O_MATCH:
                if (!ncap)
                    return 1;
                if (!matched || t[0] < bst || (t[0] == bst && t[1] > best[1])) {
                    memcpy(best, t, ncap * sizeof(int));
                    bst = t[0];
                    matched = 1;
                }
                ok = 0;
                break;
            case O_CHAR:  ok = c == ip->x; break;
            case O_CHAR2: ok = c == ip->x || c == ip->y; break;
            case O_ANY:   ok = c != 0; break;
            case O_ANYNL: ok = c != 0 && c != '\n'; break;
            default:      ok = BIT(re->cls + ip->x * 32, c); break;
            }
            if (ok)
                addthread(x, npc, ncp, &nn, cpc[i] + 1, pos + 1, t, ngen);
        }
        if (pos >= len)
            break;
        t = cpc; cpc = npc; npc = t;
        t = ccp; ccp = ncp; ncp = t;
        cn = nn;
        cgen = ngen;
        pos++;
        if (cn == 0 && matched)
            break;
    }
    return matched;
}

/* backtracking matcher (patterns with back-references): explores all
 * paths from start, keeping the longest; returns 1 if matched */
static int bt_push(Regex *re, long *sp, int a, int b)
{
    if (*sp + 2 > re->bsz) {
        long nsz = re->bsz ? re->bsz * 2 : 256;
        int *p = (int *)realloc(re->bstk, nsz * sizeof(int));
        if (!p)
            return 0;
        re->bstk = p;
        re->bsz = nsz;
    }
    re->bstk[(*sp)++] = a;
    re->bstk[(*sp)++] = b;
    return 1;
}

static int bt_at(Ctx *x, int start, int *caps, int *best, long *budget,
                 int firstonly)
{
    Regex *re = x->re;
    Inst *prog = re->prog, *ip;
    const unsigned char *s = x->s;
    int len = x->len, ncap = re->nslot, found = 0, bestend = -1;
    int pc, pos, i, k, so, l, ok;
    long sp = 0;

    for (i = 0; i < ncap; i++)
        caps[i] = -1;
    if (!bt_push(re, &sp, 0, start))
        return 0;
    while (sp > 0) {
        sp -= 2;
        pc = re->bstk[sp];
        pos = re->bstk[sp + 1];
        if (pc < 0) {
            caps[-1 - pc] = pos;
            continue;
        }
        for (;;) {
            if (--*budget < 0)
                return found;
            ip = &prog[pc];
            ok = 1;
            switch (ip->op) {
            case O_CHAR:  ok = pos < len && s[pos] == ip->x; break;
            case O_CHAR2: ok = pos < len && (s[pos] == ip->x || s[pos] == ip->y); break;
            case O_ANY:   ok = pos < len; break;
            case O_ANYNL: ok = pos < len && s[pos] != '\n'; break;
            case O_CLASS: ok = pos < len && BIT(re->cls + ip->x * 32, s[pos]); break;
            case O_MATCH:
                if (pos > bestend) {
                    bestend = pos;
                    memcpy(best, caps, ncap * sizeof(int));
                    found = 1;
                    if (pos == len || firstonly)
                        return 1;
                }
                ok = 0;
                break;
            case O_SPLIT:
                if (!bt_push(re, &sp, ip->y, pos))
                    return found;
                pc = ip->x;
                continue;
            case O_JMP:
                pc = ip->x;
                continue;
            case O_SAVE:
            case O_LSET:
                k = ip->x;
                if (!bt_push(re, &sp, -1 - k, caps[k]))
                    return found;
                caps[k] = pos;
                pc++;
                continue;
            case O_LCHK:
                ok = caps[ip->x] != pos;
                if (ok) {
                    pc++;
                    continue;
                }
                break;
            case O_LCHK2:
                /* empty iteration: leave the loop (as glibc) */
                pc = caps[ip->x + 1] == pos ? ip->y : pc + 1;
                continue;
            case O_BREF:
                k = ip->x;
                so = caps[2 * k];
                l = caps[2 * k + 1] - so;
                ok = so >= 0 && l >= 0 && pos + l <= len;
                for (i = 0; ok && i < l; i++)
                    ok = x->icase ? tolower(s[so + i]) == tolower(s[pos + i])
                                  : s[so + i] == s[pos + i];
                if (ok) {
                    pos += l;
                    pc++;
                    continue;
                }
                break;
            default:
                ok = assert_ok(x, ip->op, pos);
                if (ok) {
                    pc++;
                    continue;
                }
                break;
            }
            if (!ok)
                break;
            pos++;                      /* consumed one byte */
            pc++;
        }
    }
    return found;
}

static int backtrack(Ctx *x, int *best, int firstonly)
{
    Regex *re = x->re;
    long budget = BT_BUDGET;
    int pos = 0;
    if (!re->scratch)
        return 0;
    for (;;) {
        pos = next_start(re, x->s, pos, x->len);
        if (pos < 0)
            return 0;
        if (bt_at(x, pos, re->scratch, best, &budget, firstonly))
            return 1;
        if (budget < 0 || pos >= x->len)
            return 0;
        pos++;
    }
}

/* pure literal search */
static int litsearch(Regex *re, const unsigned char *s, int len, int *at)
{
    const unsigned char *l = (const unsigned char *)re->lit;
    int n = re->litlen, i, j, c0;
    if (n == 0) {
        *at = 0;
        return 1;
    }
    c0 = l[0];
    for (i = 0; i + n <= len; i++) {
        if (re->flags & RE_ICASE) {
            if (tolower(s[i]) != c0)
                continue;
            for (j = 1; j < n && tolower(s[i + j]) == l[j]; j++)
                ;
        } else {
            if (s[i] != c0)
                continue;
            for (j = 1; j < n && s[i + j] == l[j]; j++)
                ;
        }
        if (j == n) {
            *at = i;
            return 1;
        }
    }
    return 0;
}

int re_exec(Regex *re, const char *str, int nm, ReMatch *m, int eflags)
{
    Ctx x;
    int i, r, ncap, *best;

    if (!re || !str)
        return 0;
    if (re->flags & RE_NOSUB)
        nm = 0;
    for (i = 0; i < nm; i++)
        m[i].so = m[i].eo = -1;
    x.re = re;
    x.s = (const unsigned char *)str;
    x.len = (int)strlen(str);
    x.notbol = (eflags & RE_NOTBOL) != 0;
    x.nl = (re->flags & RE_NEWLINE) != 0;
    x.icase = (re->flags & RE_ICASE) != 0;

    if (re->lit) {
        if (!litsearch(re, x.s, x.len, &i))
            return 0;
        if (nm > 0) {
            m[0].so = i;
            m[0].eo = i + re->litlen;
        }
        return 1;
    }

    if (re->bt) {
        ncap = re->nslot;
        if (!re->scratch) {
            re->scratch = (int *)malloc(ncap * sizeof(int));
            re->best = (int *)malloc(ncap * sizeof(int));
            if (!re->scratch || !re->best)
                return 0;
        }
        best = re->best;
        r = backtrack(&x, best, nm == 0);
    } else {
        /* track m[0] only, or everything (loop registers included) */
        ncap = nm > 1 && re->nsub > 0 ? re->nslot : nm > 0 ? 2 : 0;
        if (!ensure_work(re, ncap))
            return 0;
        if (!re->scratch) {
            re->scratch = (int *)malloc(re->nslot * sizeof(int));
            if (!re->scratch)
                return 0;
        }
        x.ncap = ncap;
        best = re->best;
        r = pike(&x, best);
    }
    if (!r)
        return 0;
    for (i = 0; i < nm && i <= re->nsub && 2 * i + 1 < ncap; i++) {
        int so = best[2 * i], eo = best[2 * i + 1];
        if (so >= 0 && eo >= so) {
            m[i].so = so;
            m[i].eo = eo;
        }
    }
    return 1;
}
