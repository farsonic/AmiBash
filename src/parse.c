/*
 * parse.c - lexer and recursive descent parser for bash syntax.
 *
 * Words are kept as raw source text (quotes included); expand.c does
 * the quote processing later, exactly like bash.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include "sh.h"

enum {
    T_EOF, T_NEWLINE, T_WORD, T_SEMI, T_DSEMI, T_SEMIAND, T_DSEMIAND,
    T_AMP, T_AND, T_OR, T_PIPE, T_PIPEAMP, T_LPAREN, T_RPAREN, T_REDIR
};

typedef struct Tok {
    int type;
    int start, end;            /* source offsets                      */
    char *text;                /* T_WORD                              */
    int rop, rfd;              /* T_REDIR                             */
    char *rvar;                /* T_REDIR: {name}> variable           */
} Tok;

#define MAXPENDING 16
#define MAXALIAS 16

struct Parser {
    char *src;
    int pos;
    Arena *a;
    int line0;
    int lpos, lline;
    Tok tok;
    int have_tok;
    Redir *pending[MAXPENDING];
    int npending;
    jmp_buf jb;
    char err[200];
    char errline[200];         /* source line of a syntax error       */
    int errlineno;
    int incomplete;
    int stop_rparen;           /* scanning $( ... ): stop at ')'      */
    int rparen_pos;
    struct { char *name; int end; } al[MAXALIAS];
    int nal;
    int alias_next;            /* source position after an alias value
                                  ending in blank: the word there is
                                  alias-expanded too (-1: none)         */
    int subscript_ok;          /* assignment position: name[a b]=v     */
    int final;                 /* source is complete (file, eval): a
                                  here-document may end at EOF         */
    const char *open_kw;       /* the innermost compound command being
                                  read ("if", "{", "(" ...) and its line,
                                  for "unexpected end of file from ..." */
    int open_line;
};

static Node *parse_list(Parser *p, int multiline);
static Node *parse_command(Parser *p);
static const char *skip_dquote(const char *s);
static const char *skip_dollar(const char *s);
static const char *skip_backtick(const char *s);

/* ---------------------------------------------------------------- */
/* error handling                                                    */

static int line_at(Parser *p, int pos);

static void incomplete(Parser *p)
{
    int n = (int)strlen(p->src);
    p->incomplete = 1;
    if (p->open_kw)
        sprintf(p->err, "syntax error: unexpected end of file from `%s' command on line %d", p->open_kw,
                p->open_line);
    else
        strcpy(p->err, "syntax error: unexpected end of file");
    p->errline[0] = 0;
    p->errlineno = line_at(p, n) + (n > 0 && p->src[n - 1] != '\n');
    longjmp(p->jb, 1);
}

/* an unterminated quote or bracket in a word starting at s */
static void incomplete_match(Parser *p, const char *s, int ch)
{
    p->incomplete = 1;
    sprintf(p->err, "unexpected EOF while looking for matching `%c'", ch);
    p->errline[0] = 0;
    p->errlineno = line_at(p, (int)(s - p->src));
    longjmp(p->jb, 1);
}

static void syntax_error(Parser *p, Tok *t)
{
    const char *what;
    char buf[64];
    if (t->type == T_EOF) incomplete(p);
    switch (t->type) {
    case T_NEWLINE: what = "newline"; break;
    case T_WORD: what = t->text; break;
    default: {
        int n = t->end - t->start;
        if (n > 60) n = 60;
        memcpy(buf, p->src + t->start, n);
        buf[n] = 0;
        what = buf;
    }
    }
    sprintf(p->err, "syntax error near unexpected token `%.100s'", what);
    {
        /* remember the offending line for the "`line'" message */
        int ls = t->start, le = t->start, n;
        while (ls > 0 && p->src[ls - 1] != '\n') ls--;
        while (p->src[le] && p->src[le] != '\n') le++;
        n = le - ls < (int)sizeof(p->errline) - 1 ? le - ls : (int)sizeof(p->errline) - 1;
        memcpy(p->errline, p->src + ls, n);
        p->errline[n] = 0;
        p->errlineno = line_at(p, t->start);
    }
    longjmp(p->jb, 1);
}

static void parse_fail(Parser *p, const char *msg)
{
    strncpy(p->err, msg, sizeof(p->err) - 1);
    longjmp(p->jb, 1);
}

static int line_at(Parser *p, int pos)
{
    if (pos < p->lpos) {
        p->lpos = 0;
        p->lline = p->line0;
    }
    while (p->lpos < pos && p->src[p->lpos]) {
        if (p->src[p->lpos] == '\n') p->lline++;
        p->lpos++;
    }
    return p->lline;
}

/* ---------------------------------------------------------------- */
/* raw scanners (shared with expand.c)                               */

/* s points after the opening quote; returns pointer after closing */
static const char *skip_squote(const char *s)
{
    while (*s && *s != '\'') s++;
    return *s ? s + 1 : NULL;
}

/* ANSI-C $'...': s after the quote */
static const char *skip_ansi(const char *s)
{
    while (*s && *s != '\'') {
        if (*s == '\\' && s[1]) s++;
        s++;
    }
    return *s ? s + 1 : NULL;
}

static const char *skip_backtick(const char *s)
{
    while (*s && *s != '`') {
        if (*s == '\\' && s[1]) s++;
        s++;
    }
    return *s ? s + 1 : NULL;
}

static int delim_char(int c)
{
    return c == 0 || c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '(' ||
           c == ')' || c == '<' || c == '>';
}

/* s after "${" of "${ cmd; }" or "${| cmd; }" (bash 5.3): the '}' that ends
 * it is a word of its own where a command may start (after ';', a
 * newline, '&', ...); returns after it */
static const char *skip_funsub(const char *s)
{
    static const char *const kw[] = {"then", "do", "else", "elif", "if", "while", "until", "!", "time", NULL};
    int depth = 1, cmdpos = 1;
    if (*s == '|') s++;
    while (*s) {
        if (*s == ' ' || *s == '\t') {
            s++;
            continue;
        }
        if (*s == ';' || *s == '\n' || *s == '&' || *s == '|' || *s == '(' || *s == ')') {
            cmdpos = *s != ')' || cmdpos;
            s++;
            continue;
        }
        if (*s == '#' && cmdpos) {
            while (*s && *s != '\n') s++;
            continue;
        }
        if (cmdpos && (*s == '}' || (*s == '{' && delim_char((unsigned char)s[1])))) {
            if (*s == '}' && --depth == 0) return s + 1;
            if (*s == '{') depth++;
            s++;
            continue;
        }
        {
            /* a word: through quotes and expansions */
            const char *w = s;
            int k;
            while (*s && !delim_char((unsigned char)*s)) {
                if (*s == '\\') {
                    s += s[1] ? 2 : 1;
                } else if (*s == '\'') {
                    if (!(s = skip_squote(s + 1))) return NULL;
                } else if (*s == '"') {
                    if (!(s = skip_dquote(s + 1))) return NULL;
                } else if (*s == '`') {
                    if (!(s = skip_backtick(s + 1))) return NULL;
                } else if (*s == '$') {
                    if (!(s = skip_dollar(s))) return NULL;
                } else
                    s++;
            }
            cmdpos = 0;
            for (k = 0; kw[k]; k++)
                if ((int)strlen(kw[k]) == (int)(s - w) && !strncmp(w, kw[k], s - w)) cmdpos = 1;
        }
    }
    return NULL;
}

/* s after "${"; returns after matching '}' */
static const char *skip_brace(const char *s)
{
    int depth = 1;
    if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '|') return skip_funsub(s);
    while (*s) {
        switch (*s) {
        case '\\':
            if (s[1]) s++;
            s++;
            break;
        case '\'':
            /* single quotes are only special in some ${} contexts; bash
             * treats them as quotes in the word part, we do too */
            s = skip_squote(s + 1);
            if (!s) return NULL;
            break;
        case '"':
            s = skip_dquote(s + 1);
            if (!s) return NULL;
            break;
        case '`':
            s = skip_backtick(s + 1);
            if (!s) return NULL;
            break;
        case '$':
            s = skip_dollar(s);
            if (!s) return NULL;
            break;
        case '{':
            depth++;
            s++;
            break;
        case '}':
            if (--depth == 0) return s + 1;
            s++;
            break;
        default:
            s++;
        }
    }
    return NULL;
}

/* s after "$(("; find "))" at depth 0. returns after it, or NULL;
 * *notarith set when this turned out to be $( ( ... ) ) */
static const char *skip_arith(const char *s, int *notarith)
{
    int depth = 0;
    *notarith = 0;
    while (*s) {
        switch (*s) {
        case '\\':
            if (s[1]) s++;
            s++;
            break;
        case '\'':
            s = skip_squote(s + 1);
            if (!s) return NULL;
            break;
        case '"':
            s = skip_dquote(s + 1);
            if (!s) return NULL;
            break;
        case '`':
            s = skip_backtick(s + 1);
            if (!s) return NULL;
            break;
        case '$':
            s = skip_dollar(s);
            if (!s) return NULL;
            break;
        case '(':
            depth++;
            s++;
            break;
        case ')':
            if (depth == 0) {
                if (s[1] == ')') return s + 2;
                *notarith = 1;
                return NULL;
            }
            depth--;
            s++;
            break;
        default:
            s++;
        }
    }
    return NULL;
}

/* s at '$' */
static const char *skip_dollar(const char *s)
{
    s++;
    if (*s == '(') {
        if (s[1] == '(') {
            int na;
            const char *e = skip_arith(s + 2, &na);
            if (e) return e;
            if (!na) return NULL;
        }
        {
            const char *e = scan_cmdsub_end(s + 1);
            return e ? e + 1 : NULL;
        }
    }
    if (*s == '{') return skip_brace(s + 1);
    if (*s == '[') {
        /* $[ expr ] */
        const char *e = s + 1;
        int depth = 1;
        while (*e && depth) {
            if (*e == '[') depth++;
            else if (*e == ']') depth--;
            e++;
        }
        return depth ? NULL : e;
    }
    if (*s == '\'') return skip_ansi(s + 1);
    if (*s == '"') return skip_dquote(s + 1);
    return s;
}

/* s after '"' */
static const char *skip_dquote(const char *s)
{
    while (*s && *s != '"') {
        if (*s == '\\') {
            if (s[1]) s++;
            s++;
        } else if (*s == '$') {
            s = skip_dollar(s);
            if (!s) return NULL;
        } else if (*s == '`') {
            s = skip_backtick(s + 1);
            if (!s) return NULL;
        } else {
            s++;
        }
    }
    return *s ? s + 1 : NULL;
}

/* balanced parens (for a=( ... ), extglob, process substitution);
 * s after '(' ; returns after matching ')' */
static const char *skip_parens(const char *s)
{
    int depth = 1;
    while (*s) {
        switch (*s) {
        case '\\':
            if (s[1]) s++;
            s++;
            break;
        case '\'':
            s = skip_squote(s + 1);
            if (!s) return NULL;
            break;
        case '"':
            s = skip_dquote(s + 1);
            if (!s) return NULL;
            break;
        case '`':
            s = skip_backtick(s + 1);
            if (!s) return NULL;
            break;
        case '$':
            s = skip_dollar(s);
            if (!s) return NULL;
            break;
        case '#': {
            /* comment inside a multi-line array assignment */
            if (s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n' || s[-1] == '(') {
                while (*s && *s != '\n') s++;
            } else {
                s++;
            }
            break;
        }
        case '(':
            depth++;
            s++;
            break;
        case ')':
            if (--depth == 0) return s + 1;
            s++;
            break;
        default:
            s++;
        }
    }
    return NULL;
}

static int is_meta(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' ||
           c == '|' || c == '(' || c == ')' || c == '<' || c == '>' || c == 0;
}

/* does the word text so far look like an assignment prefix name= / name+= / name[..]= */
static int is_assign_prefix(const char *w, int n)
{
    int i = 0;
    if (n < 2 || w[n - 1] != '=') return 0;
    if (!(isalpha((unsigned char)w[0]) || w[0] == '_')) return 0;
    while (i < n && (isalnum((unsigned char)w[i]) || w[i] == '_')) i++;
    if (i < n && w[i] == '[') {
        while (i < n && w[i] != ']') i++;
        if (i < n) i++;
    }
    if (i < n && w[i] == '+') i++;
    return i == n - 1;
}

/* scan a word starting at s; returns end pointer (NULL = unterminated) */
static const char *scan_word(const char *s)
{
    const char *start = s;
    while (*s) {
        int c = (unsigned char)*s;
        if (is_meta(c)) {
            if (c == '(' && s > start && strchr("@!+*?", s[-1]) && sh.sh_extglob) {
                s = skip_parens(s + 1);
                if (!s) return NULL;
                continue;
            }
            if (c == '(' && is_assign_prefix(start, (int)(s - start))) {
                s = skip_parens(s + 1);
                if (!s) return NULL;
                continue;
            }
            if ((c == '<' || c == '>') && s[1] == '(' && s == start) {
                s = skip_parens(s + 2);
                if (!s) return NULL;
                continue;
            }
            break;
        }
        switch (c) {
        case '\\':
            if (!s[1]) return s + 1;
            s += 2;
            break;
        case '\'':
            s = skip_squote(s + 1);
            if (!s) return NULL;
            break;
        case '"':
            s = skip_dquote(s + 1);
            if (!s) return NULL;
            break;
        case '`':
            s = skip_backtick(s + 1);
            if (!s) return NULL;
            break;
        case '$':
            s = skip_dollar(s);
            if (!s) return NULL;
            break;
        default:
            s++;
        }
    }
    return s;
}

/* in assignment position, a subscript may contain blanks:
 * "name[key with spaces]=value" is one word */
static const char *scan_assign_word(const char *s)
{
    const char *p = s;
    if (isalpha((unsigned char)*p) || *p == '_') {
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && *p != '\n' && depth) {
                if (*p == '\\' && p[1]) p += 2;
                else if (*p == '\'') {
                    p = skip_squote(p + 1);
                    if (!p) return scan_word(s);
                } else if (*p == '"') {
                    p = skip_dquote(p + 1);
                    if (!p) return scan_word(s);
                } else if (*p == '$') {
                    const char *e = skip_dollar(p);
                    if (!e) return scan_word(s);
                    p = e > p ? e : p + 1;
                } else {
                    if (*p == '[') depth++;
                    else if (*p == ']') depth--;
                    p++;
                }
            }
            if (depth == 0 && (*p == '=' || (*p == '+' && p[1] == '=')))
                return scan_word(p);
        }
    }
    return scan_word(s);
}

/* which closing character is missing in an unterminated word */
static int unterminated_char(const char *s)
{
    while (*s && !is_meta((unsigned char)*s)) {
        const char *e;
        switch (*s) {
        case '\\':
            if (!s[1]) return 0;
            s += 2;
            continue;
        case '\'':
            e = skip_squote(s + 1);
            if (!e) return '\'';
            s = e;
            continue;
        case '"':
            e = skip_dquote(s + 1);
            if (!e) return '"';
            s = e;
            continue;
        case '`':
            e = skip_backtick(s + 1);
            if (!e) return '`';
            s = e;
            continue;
        case '$':
            e = skip_dollar(s);
            if (!e) return s[1] == '{' ? '}' : s[1] == '\'' ? '\'' : s[1] == '"' ? '"' : ')';
            s = e > s ? e : s + 1;
            continue;
        default:
            s++;
        }
    }
    if (*s == '(') return ')';
    return 0;
}

/* copy word text removing backslash-newline continuations */
static char *word_copy(Parser *p, const char *s, int n)
{
    char *out = arena_alloc(p->a, n + 1);
    int i = 0, o = 0, dq = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\'' && !dq) {
            out[o++] = s[i++];
            while (i < n && s[i] != '\'') out[o++] = s[i++];
            if (i < n) out[o++] = s[i++];
            continue;
        }
        if (c == '\\' && i + 1 < n) {
            if (s[i + 1] == '\n') {
                i += 2;
                continue;
            }
            out[o++] = s[i++];
            out[o++] = s[i++];
            continue;
        }
        if (c == '"') dq = !dq;
        out[o++] = s[i++];
    }
    out[o] = 0;
    return out;
}

/* ---------------------------------------------------------------- */
/* lexer                                                             */

static void read_heredocs(Parser *p, int *pos);

static void lex(Parser *p, Tok *t)
{
    const char *s;
    int c;
again:
    s = p->src + p->pos;
    /* skip blanks and line continuations */
    for (;;) {
        if (*s == ' ' || *s == '\t' || *s == '\r') s++;
        else if (*s == '\\' && s[1] == '\n') s += 2;
        else break;
    }
    if (*s == '#') {
        while (*s && *s != '\n') s++;
    }
    t->start = (int)(s - p->src);
    t->text = NULL;
    t->rvar = NULL;
    c = (unsigned char)*s;
    if (c == 0) {
        t->type = T_EOF;
        t->end = t->start;
        return;
    }
    if (c == '\n') {
        int e = t->start + 1;
        t->type = T_NEWLINE;
        if (p->npending) read_heredocs(p, &e);
        t->end = e;
        return;
    }
#define OP(len, ty) do { t->type = ty; t->end = t->start + (len); return; } while (0)
#define ROP(len, op, fd) do { t->type = T_REDIR; t->rop = op; t->rfd = fd; t->end = t->start + (len); return; } while (0)
    switch (c) {
    case ';':
        if (s[1] == ';') {
            if (s[2] == '&') OP(3, T_DSEMIAND);
            OP(2, T_DSEMI);
        }
        if (s[1] == '&') OP(2, T_SEMIAND);
        OP(1, T_SEMI);
    case '&':
        if (s[1] == '&') OP(2, T_AND);
        if (s[1] == '>') {
            if (s[2] == '>') ROP(3, R_APPERR, -1);
            ROP(2, R_OUTERR, -1);
        }
        OP(1, T_AMP);
    case '|':
        if (s[1] == '|') OP(2, T_OR);
        if (s[1] == '&') OP(2, T_PIPEAMP);
        OP(1, T_PIPE);
    case '(':
        OP(1, T_LPAREN);
    case ')':
        OP(1, T_RPAREN);
    case '<':
        if (s[1] == '(') break;            /* process substitution word */
        if (s[1] == '<') {
            if (s[2] == '<') ROP(3, R_HERESTR, -1);
            if (s[2] == '-') ROP(3, R_HEREDOC, -2);
            ROP(2, R_HEREDOC, -1);
        }
        if (s[1] == '&') ROP(2, R_DUPIN, -1);
        if (s[1] == '>') ROP(2, R_RDWR, -1);
        ROP(1, R_IN, -1);
    case '>':
        if (s[1] == '(') break;
        if (s[1] == '>') ROP(2, R_APPEND, -1);
        if (s[1] == '&') ROP(2, R_DUPOUT, -1);
        if (s[1] == '|') ROP(2, R_CLOBBER, -1);
        ROP(1, R_OUT, -1);
    }
    /* {name}>file: the shell picks a descriptor and stores it in name */
    if (c == '{' && (isalpha((unsigned char)s[1]) || s[1] == '_')) {
        const char *d = s + 1;
        while (isalnum((unsigned char)*d) || *d == '_') d++;
        if (*d == '}' && (d[1] == '<' || d[1] == '>') && d[2] != '(') {
            int save = p->pos;
            Tok r;
            p->pos = (int)(d + 1 - p->src);
            lex(p, &r);
            p->pos = save;
            if (r.type == T_REDIR && r.rop != R_OUTERR && r.rop != R_APPERR &&
                r.rop != R_HEREDOC && r.rfd == -1) {
                *t = r;
                t->start = (int)(s - p->src);
                t->rvar = arena_strndup(p->a, s + 1, (int)(d - s - 1));
                return;
            }
        }
    }
    /* IO number: digits immediately followed by < or > */
    if (isdigit(c)) {
        const char *d = s;
        while (isdigit((unsigned char)*d)) d++;
        if ((*d == '<' || *d == '>') && d[1] != '(') {
            int fd = atoi(s);
            int save = p->pos;
            Tok r;
            p->pos = (int)(d - p->src);
            lex(p, &r);
            p->pos = save;
            if (r.type == T_REDIR && r.rop != R_OUTERR && r.rop != R_APPERR) {
                *t = r;
                t->start = (int)(s - p->src);
                if (t->rfd == -2) t->rfd = -(fd + 10);   /* <<- with fd */
                else t->rfd = fd;
                return;
            }
        }
    }
    {
        const char *e = p->subscript_ok ? scan_assign_word(s) : scan_word(s);
        if (!e) {
            int ch = unterminated_char(s);
            if (ch) incomplete_match(p, s, ch);
            incomplete(p);
        }
        if (e == s) {
            /* lone char we do not understand */
            e = s + 1;
        }
        t->type = T_WORD;
        t->end = (int)(e - p->src);
        t->text = word_copy(p, s, (int)(e - s));
        if (!t->text[0] && e - s == 2 && s[0] == '\\') {
            /* line continuation only */
            p->pos = t->end;
            goto again;
        }
    }
#undef OP
#undef ROP
}

static Tok *peek(Parser *p)
{
    if (!p->have_tok) {
        lex(p, &p->tok);
        p->have_tok = 1;
    }
    return &p->tok;
}

static void consume(Parser *p)
{
    peek(p);
    p->pos = p->tok.end;
    p->have_tok = 0;
}

static int peek_word(Parser *p, const char *w)
{
    Tok *t = peek(p);
    return t->type == T_WORD && strcmp(t->text, w) == 0;
}

static void expect_word(Parser *p, const char *w)
{
    Tok *t = peek(p);
    if (t->type == T_WORD && strcmp(t->text, w) == 0) {
        consume(p);
        return;
    }
    syntax_error(p, t);
}

static void skip_newlines(Parser *p)
{
    while (peek(p)->type == T_NEWLINE) consume(p);
}

/* strip quotes from a heredoc delimiter */
static char *unquote_delim(Parser *p, const char *w, int *quoted)
{
    char *o = arena_alloc(p->a, strlen(w) + 1);
    int i = 0;
    *quoted = 0;
    while (*w) {
        if (*w == '\'' || *w == '"') {
            char q = *w++;
            *quoted = 1;
            while (*w && *w != q) o[i++] = *w++;
            if (*w) w++;
        } else if (*w == '\\' && w[1]) {
            *quoted = 1;
            o[i++] = w[1];
            w += 2;
        } else if (*w == '$' && (w[1] == '\'' || w[1] == '"')) {
            w++;
        } else {
            o[i++] = *w++;
        }
    }
    o[i] = 0;
    return o;
}

/* read heredoc bodies starting at *pos (just after a newline) */
static void read_heredocs(Parser *p, int *pos)
{
    int i;
    for (i = 0; i < p->npending; i++) {
        Redir *r = p->pending[i];
        const char *s = p->src + *pos;
        SBuf b;
        int found = 0;
        sb_init(&b);
        sb_addn(&b, "", 0);
        while (*s) {
            const char *e = strchr(s, '\n');
            const char *ls = s;
            int n;
            if (!e) e = s + strlen(s);
            if (r->striptabs)
                while (ls < e && *ls == '\t') ls++;
            n = (int)(e - ls);
            if (n == (int)strlen(r->target) && strncmp(ls, r->target, n) == 0) {
                s = *e ? e + 1 : e;
                found = 1;
                break;
            }
            if (!*e) {
                /* last line without newline: delimiter not found yet */
                s = e;
                if (n) sb_addn(&b, ls, n);
                break;
            }
            sb_addn(&b, ls, (int)(e - ls) + 1);
            s = e + 1;
        }
        if (!found && p->final) {
            /* like bash: accept it with a warning */
            int save = sh.lineno, n = (int)strlen(p->src);
            int start = line_at(p, *pos > 0 ? *pos - 1 : 0);
            sh.lineno = line_at(p, n > 0 ? n - 1 : 0);
            sh_error("warning: here-document at line %d delimited by end-of-file (wanted `%s')",
                     start, r->target);
            sh.lineno = save;
            found = 1;
        }
        if (!found) {
            sb_free(&b);
            p->npending = 0;
            incomplete(p);
        }
        r->heredoc = arena_strdup(p->a, b.s);
        sb_free(&b);
        *pos = (int)(s - p->src);
    }
    p->npending = 0;
}

/* ---------------------------------------------------------------- */
/* alias expansion                                                    */

static int try_alias(Parser *p)
{
    Tok *t = peek(p);
    const char *val;
    int i, n, vl;
    char *ns;
    if (t->type != T_WORD) return 0;
    if (!sh.sh_expand_aliases) return 0;
    if (strpbrk(t->text, "'\"\\$`")) return 0;
    val = alias_get(t->text);
    if (!val) return 0;
    for (i = 0; i < p->nal; i++)
        if (strcmp(p->al[i].name, t->text) == 0 && t->start < p->al[i].end) return 0;
    /* splice alias text into the source */
    n = (int)strlen(p->src);
    vl = (int)strlen(val);
    ns = arena_alloc(p->a, n - (t->end - t->start) + vl + 2);
    memcpy(ns, p->src, t->start);
    memcpy(ns + t->start, val, vl);
    strcpy(ns + t->start + vl, p->src + t->end);
    /* shift existing alias guards */
    for (i = 0; i < p->nal; i++)
        if (p->al[i].end > t->start) p->al[i].end += vl - (t->end - t->start);
    if (p->nal < MAXALIAS) {
        p->al[p->nal].name = arena_strdup(p->a, t->text);
        p->al[p->nal].end = t->start + vl;
        p->nal++;
    }
    /* a value ending in a blank makes the word following it subject to
     * alias expansion too */
    if (p->alias_next > t->start) p->alias_next += vl - (t->end - t->start);
    if (vl > 0 && (val[vl - 1] == ' ' || val[vl - 1] == '\t')) p->alias_next = t->start + vl;
    p->src = ns;
    p->pos = t->start;
    p->lpos = 0;
    p->lline = p->line0;
    p->have_tok = 0;
    return 1;
}

/* ---------------------------------------------------------------- */
/* nodes                                                              */

static Node *new_node(Parser *p, int type)
{
    Node *n = arena_alloc(p->a, sizeof(Node));
    n->type = type;
    n->lineno = line_at(p, p->have_tok ? p->tok.start : p->pos);
    return n;
}

static Word *new_word(Parser *p, char *text)
{
    Word *w = arena_alloc(p->a, sizeof(Word));
    w->text = text;
    return w;
}

static int is_reserved_end(const char *w)
{
    static const char *const words[] = {
        "then", "else", "elif", "fi", "do", "done", "esac", "}", NULL
    };
    int i;
    for (i = 0; words[i]; i++)
        if (strcmp(w, words[i]) == 0) return 1;
    return 0;
}

/* can the current token start a command? */
static int starts_command(Parser *p)
{
    Tok *t = peek(p);
    switch (t->type) {
    case T_WORD:
        return !is_reserved_end(t->text);
    case T_LPAREN:
    case T_REDIR:
        return 1;
    default:
        return 0;
    }
}

static Redir *parse_redir(Parser *p)
{
    Tok *t = peek(p);
    Redir *r = arena_alloc(p->a, sizeof(Redir));
    int op = t->rop, fd = t->rfd;
    r->varfd = t->rvar;
    consume(p);
    r->op = op;
    if (op == R_HEREDOC && (fd == -2 || fd <= -10)) {
        r->striptabs = 1;
        fd = fd == -2 ? -1 : -(fd + 10);
    }
    r->fd = fd;
    t = peek(p);
    if (t->type != T_WORD) syntax_error(p, t);
    if (op == R_HEREDOC) {
        r->target = unquote_delim(p, t->text, &r->quoted);
        if (p->npending < MAXPENDING) p->pending[p->npending++] = r;
    } else {
        r->target = t->text;
    }
    consume(p);
    return r;
}

static void append_redir(Redir **list, Redir *r)
{
    while (*list) list = &(*list)->next;
    *list = r;
}

static void parse_trailing_redirs(Parser *p, Node *n)
{
    while (peek(p)->type == T_REDIR) append_redir(&n->redirs, parse_redir(p));
}

/* compound list terminated by one of the given reserved words */
static Node *parse_body(Parser *p)
{
    Node *n;
    skip_newlines(p);
    n = parse_list(p, 1);
    if (!n) {
        Tok *t = peek(p);
        syntax_error(p, t);
    }
    return n;
}

static Node *parse_if(Parser *p)
{
    Node *n = new_node(p, N_IF);
    consume(p);                               /* if / elif */
    n->u.ifn.cond = parse_body(p);
    expect_word(p, "then");
    n->u.ifn.then = parse_body(p);
    if (peek_word(p, "elif")) {
        n->u.ifn.els = parse_if(p);
        return n;
    }
    if (peek_word(p, "else")) {
        consume(p);
        n->u.ifn.els = parse_body(p);
    }
    expect_word(p, "fi");
    return n;
}

static Node *parse_do_group(Parser *p)
{
    Node *b;
    skip_newlines(p);
    if (peek_word(p, "{")) {
        /* for x in ...; { list; } */
        consume(p);
        b = parse_body(p);
        expect_word(p, "}");
        return b;
    }
    expect_word(p, "do");
    b = parse_body(p);
    expect_word(p, "done");
    return b;
}

static Node *parse_while(Parser *p, int until)
{
    Node *n = new_node(p, until ? N_UNTIL : N_WHILE);
    consume(p);
    n->u.loop.cond = parse_body(p);
    n->u.loop.body = parse_do_group(p);
    return n;
}

/* text between (( and )) starting at src offset; returns arena string */
static char *scan_dparen(Parser *p, int start, int *endpos)
{
    int na;
    const char *s = p->src + start + 2;
    const char *e = skip_arith(s, &na);
    if (!e) {
        if (na) return NULL;
        incomplete(p);
    }
    *endpos = (int)(e - p->src);
    return arena_strndup(p->a, s, (e - 2) - s);
}

static Node *parse_for(Parser *p, int select)
{
    Node *n;
    Tok *t;
    int line = line_at(p, peek(p)->start);
    consume(p);                               /* for */
    t = peek(p);
    if (!select && t->type == T_LPAREN && p->src[t->start + 1] == '(') {
        int end;
        char *expr = scan_dparen(p, t->start, &end);
        char *a, *b;
        if (!expr) syntax_error(p, t);
        p->pos = end;
        p->have_tok = 0;
        n = new_node(p, N_ARITHFOR);
        n->lineno = line;
        /* split on top level ';' */
        a = strchr(expr, ';');
        if (!a) parse_fail(p, "syntax error: `;' expected in for (( ))");
        *a++ = 0;
        b = strchr(a, ';');
        if (!b) parse_fail(p, "syntax error: `;' expected in for (( ))");
        *b++ = 0;
        n->u.afor.init = expr;
        n->u.afor.cond = a;
        n->u.afor.step = b;
        if (peek(p)->type == T_SEMI) consume(p);
        n->u.afor.body = parse_do_group(p);
        return n;
    }
    n = new_node(p, select ? N_SELECT : N_FOR);
    n->lineno = line;
    if (t->type != T_WORD || !str_isname(t->text)) syntax_error(p, t);
    n->u.forn.var = t->text;
    consume(p);
    skip_newlines(p);
    if (peek_word(p, "in")) {
        Word **wp = &n->u.forn.words;
        consume(p);
        n->u.forn.has_in = 1;
        while ((t = peek(p))->type == T_WORD) {
            *wp = new_word(p, t->text);
            wp = &(*wp)->next;
            consume(p);
        }
        if (t->type != T_SEMI && t->type != T_NEWLINE) syntax_error(p, t);
        consume(p);
    } else if (peek(p)->type == T_SEMI) {
        consume(p);
    }
    n->u.forn.body = parse_do_group(p);
    return n;
}

static Node *parse_case(Parser *p)
{
    Node *n = new_node(p, N_CASE);
    Tok *t;
    CaseItem **ip = &n->u.casen.items;
    consume(p);
    t = peek(p);
    if (t->type != T_WORD) syntax_error(p, t);
    n->u.casen.word = t->text;
    consume(p);
    skip_newlines(p);
    expect_word(p, "in");
    for (;;) {
        CaseItem *ci;
        Word **wp;
        skip_newlines(p);
        if (peek_word(p, "esac")) {
            consume(p);
            break;
        }
        ci = arena_alloc(p->a, sizeof(CaseItem));
        wp = &ci->pats;
        if (peek(p)->type == T_LPAREN) consume(p);
        for (;;) {
            t = peek(p);
            if (t->type != T_WORD) syntax_error(p, t);
            *wp = new_word(p, t->text);
            wp = &(*wp)->next;
            consume(p);
            t = peek(p);
            if (t->type == T_PIPE) {
                consume(p);
                continue;
            }
            if (t->type == T_RPAREN) {
                consume(p);
                break;
            }
            syntax_error(p, t);
        }
        skip_newlines(p);
        t = peek(p);
        if (t->type == T_DSEMI || t->type == T_SEMIAND || t->type == T_DSEMIAND ||
            (t->type == T_WORD && strcmp(t->text, "esac") == 0)) {
            ci->body = NULL;
        } else {
            ci->body = parse_list(p, 1);
        }
        t = peek(p);
        if (t->type == T_DSEMI) ci->term = 0, consume(p);
        else if (t->type == T_SEMIAND) ci->term = 1, consume(p);
        else if (t->type == T_DSEMIAND) ci->term = 2, consume(p);
        else if (!(t->type == T_WORD && strcmp(t->text, "esac") == 0)) syntax_error(p, t);
        *ip = ci;
        ip = &ci->next;
    }
    return n;
}

/* ---- [[ ]] ---- */

static int cond_tok(Parser *p, char **text, int *start)
{
    const char *s = p->src + p->pos;
    for (;;) {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        else if (*s == '\\' && s[1] == '\n') s += 2;
        else break;
    }
    *start = (int)(s - p->src);
    if (!*s) incomplete(p);
    if (s[0] == ']' && s[1] == ']' && is_meta((unsigned char)s[2])) {
        *text = "]]";
        return 2;
    }
    if ((s[0] == '&' && s[1] == '&') || (s[0] == '|' && s[1] == '|')) {
        *text = s[0] == '&' ? "&&" : "||";
        return 2;
    }
    if (*s == '(' || *s == ')') {
        *text = *s == '(' ? "(" : ")";
        return 1;
    }
    if (*s == '<' || *s == '>') {
        *text = *s == '<' ? "<" : ">";
        return 1;
    }
    {
        const char *e;
        int save = sh.sh_extglob;
        sh.sh_extglob = 1;          /* [[ a == @(x|y) ]] always parses */
        e = scan_word(s);
        sh.sh_extglob = save;
        if (!e) incomplete(p);
        if (e == s) e = s + 1;
        *text = word_copy(p, s, (int)(e - s));
        return (int)(e - s);
    }
}

static char *cond_next(Parser *p)
{
    char *t;
    int st, n = cond_tok(p, &t, &st);
    p->pos = st + n;
    return t;
}

static char *cond_peek(Parser *p)
{
    char *t;
    int st;
    cond_tok(p, &t, &st);
    return t;
}

/* regex operand of =~ : may contain unquoted ( ) | */
static char *cond_regex(Parser *p)
{
    const char *s = p->src + p->pos, *b;
    int depth = 0;
    while (*s == ' ' || *s == '\t') s++;
    b = s;
    while (*s) {
        if (*s == '\\' && s[1]) {
            s += 2;
            continue;
        }
        if (*s == '\'') {
            s = skip_squote(s + 1);
            if (!s) incomplete(p);
            continue;
        }
        if (*s == '"') {
            s = skip_dquote(s + 1);
            if (!s) incomplete(p);
            continue;
        }
        if (*s == '$') {
            s = skip_dollar(s);
            if (!s) incomplete(p);
            continue;
        }
        if (*s == '(') depth++;
        else if (*s == ')') {
            if (depth == 0) break;
            depth--;
        } else if (depth == 0 && (*s == ' ' || *s == '\t' || *s == '\n')) break;
        else if (depth == 0 && ((s[0] == '&' && s[1] == '&') || (s[0] == '|' && s[1] == '|'))) break;
        s++;
    }
    if (s == b) parse_fail(p, "syntax error in conditional expression");
    p->pos = (int)(s - p->src);
    return arena_strndup(p->a, b, s - b);
}

static CondNode *cond_or(Parser *p);

static CondNode *cnode(Parser *p, int type)
{
    CondNode *c = arena_alloc(p->a, sizeof(CondNode));
    c->type = type;
    return c;
}

static CondNode *cond_prim(Parser *p)
{
    char *t = cond_next(p);
    CondNode *c;
    if (strcmp(t, "]]") == 0) parse_fail(p, "syntax error in conditional expression");
    if (strcmp(t, "!") == 0) {
        c = cnode(p, C_NOT);
        c->l = cond_prim(p);
        return c;
    }
    if (strcmp(t, "(") == 0) {
        c = cond_or(p);
        if (strcmp(cond_next(p), ")") != 0)
            parse_fail(p, "syntax error in conditional expression: expected `)'");
        return c;
    }
    if (t[0] == '-' && t[1] && !t[2] && is_test_unop(t)) {
        char *nx = cond_peek(p);
        if (strcmp(nx, "]]") != 0 && strcmp(nx, "&&") != 0 && strcmp(nx, "||") != 0 &&
            strcmp(nx, ")") != 0) {
            c = cnode(p, C_UNARY);
            c->op = t;
            c->w1 = cond_next(p);
            return c;
        }
    }
    {
        char *op = cond_peek(p);
        if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "=~") == 0 ||
            (op[0] == '-' && is_test_binop(op))) {
            cond_next(p);
            c = cnode(p, C_BINARY);
            c->op = op;
            c->w1 = t;
            c->w2 = strcmp(op, "=~") == 0 ? cond_regex(p) : cond_next(p);
            if (strcmp(c->w2, "]]") == 0) parse_fail(p, "syntax error in conditional expression");
            return c;
        }
    }
    c = cnode(p, C_WORD);
    c->w1 = t;
    return c;
}

static CondNode *cond_and(Parser *p)
{
    CondNode *l = cond_prim(p);
    while (strcmp(cond_peek(p), "&&") == 0) {
        CondNode *c = cnode(p, C_AND);
        cond_next(p);
        c->l = l;
        c->r = cond_prim(p);
        l = c;
    }
    return l;
}

static CondNode *cond_or(Parser *p)
{
    CondNode *l = cond_and(p);
    while (strcmp(cond_peek(p), "||") == 0) {
        CondNode *c = cnode(p, C_OR);
        cond_next(p);
        c->l = l;
        c->r = cond_and(p);
        l = c;
    }
    return l;
}

static Node *parse_cond(Parser *p)
{
    Node *n = new_node(p, N_COND);
    consume(p);                               /* [[ */
    p->have_tok = 0;
    n->u.cond.expr = cond_or(p);
    if (strcmp(cond_next(p), "]]") != 0)
        parse_fail(p, "syntax error in conditional expression");
    return n;
}

/* ---- function definitions ---- */

static Node *parse_funcbody(Parser *p, char *name)
{
    Node *n = new_node(p, N_FUNCDEF);
    int start;
    skip_newlines(p);
    start = peek(p)->start;
    n->u.func.name = name;
    n->u.func.body = parse_command(p);
    if (!n->u.func.body) syntax_error(p, peek(p));
    if (n->u.func.body->type != N_GROUP && n->u.func.body->type != N_SUBSHELL &&
        n->u.func.body->type != N_IF && n->u.func.body->type != N_WHILE &&
        n->u.func.body->type != N_UNTIL && n->u.func.body->type != N_FOR &&
        n->u.func.body->type != N_CASE && n->u.func.body->type != N_COND &&
        n->u.func.body->type != N_ARITH && n->u.func.body->type != N_ARITHFOR)
        parse_fail(p, "syntax error: function body must be a compound command");
    n->u.func.src = arena_strndup(p->a, p->src + start, p->pos - start);
    return n;
}

/* ---- simple commands ---- */

static int is_assignment_word(const char *w)
{
    const char *e = strchr(w, '=');
    if (!e || e == w) return 0;
    return is_assign_prefix(w, (int)(e - w) + 1);
}

static Node *parse_simple(Parser *p)
{
    Node *n = new_node(p, N_SIMPLE);
    Word **ap = &n->u.simple.assigns, **wp = &n->u.simple.words;
    int nwords = 0, last_end = -1;
    for (;;) {
        Tok *t = peek(p);
        if (t->type == T_REDIR) {
            append_redir(&n->redirs, parse_redir(p));
            last_end = p->pos;
            continue;
        }
        if (t->type != T_WORD) break;
        if (nwords == 0 && !p->subscript_ok && strchr(t->text, '[') && !strchr(t->text, ']')) {
            /* maybe name[sub with blanks]=value: re-scan */
            p->have_tok = 0;
            p->subscript_ok = 1;
            t = peek(p);
            p->subscript_ok = 0;
        }
        if (nwords == 0 && is_assignment_word(t->text)) {
            *ap = new_word(p, t->text);
            ap = &(*ap)->next;
            consume(p);
            last_end = p->pos;
            continue;
        }
        if (nwords > 0 && p->alias_next >= 0 && t->start >= p->alias_next) {
            p->alias_next = -1;
            if (try_alias(p)) continue;
        }
        *wp = new_word(p, t->text);
        wp = &(*wp)->next;
        nwords++;
        consume(p);
        last_end = p->pos;
        /* "name ()" function definition */
        if (nwords == 1 && !n->u.simple.assigns && !n->redirs && peek(p)->type == T_LPAREN) {
            int save = p->pos;
            Tok lp = *peek(p);
            consume(p);
            if (peek(p)->type == T_RPAREN) {
                consume(p);
                return parse_funcbody(p, n->u.simple.words->text);
            }
            (void)save;
            syntax_error(p, &lp);
        }
    }
    if (!n->u.simple.words && !n->u.simple.assigns && !n->redirs) syntax_error(p, peek(p));
    /* like bash 5.3, $LINENO of a simple command is the line it starts on
     * (new_node() took it), but the line it ends on for assignments only */
    if (last_end > 0 && !n->u.simple.words) n->lineno = line_at(p, last_end - 1);
    return n;
}

static Node *parse_compound(Parser *p, int guard);

/* a command; for compound ones the parser notes which is open, for the
 * message if the input ends inside it (as bash 5.3) */
static Node *parse_command(Parser *p)
{
    static const char *const kws[] = {"if", "while", "until", "for", "select", "case", "{", NULL};
    const char *save_kw = p->open_kw, *kw = NULL;
    int save_line = p->open_line, k;
    Tok *t = peek(p);
    Node *n;
    if (t->type == T_WORD) {
        for (k = 0; kws[k]; k++)
            if (!strcmp(t->text, kws[k])) kw = kws[k];
    } else if (t->type == T_LPAREN && p->src[t->start + 1] != '(')
        kw = "(";
    if (kw) {
        p->open_kw = kw;
        p->open_line = line_at(p, t->start);
    }
    n = parse_compound(p, 0);
    p->open_kw = save_kw;
    p->open_line = save_line;
    return n;
}

static Node *parse_compound(Parser *p, int guard)
{
    Tok *t;
    Node *n;
again:
    t = peek(p);
    if (t->type == T_WORD) {
        const char *w = t->text;
        if (strcmp(w, "if") == 0) n = parse_if(p);
        else if (strcmp(w, "while") == 0) n = parse_while(p, 0);
        else if (strcmp(w, "until") == 0) n = parse_while(p, 1);
        else if (strcmp(w, "for") == 0) n = parse_for(p, 0);
        else if (strcmp(w, "select") == 0) n = parse_for(p, 1);
        else if (strcmp(w, "case") == 0) n = parse_case(p);
        else if (strcmp(w, "{") == 0) {
            n = new_node(p, N_GROUP);
            consume(p);
            n->u.one.body = parse_body(p);
            expect_word(p, "}");
        } else if (strcmp(w, "[[") == 0) {
            n = parse_cond(p);
        } else if (strcmp(w, "function") == 0) {
            char *name;
            consume(p);
            t = peek(p);
            if (t->type != T_WORD) syntax_error(p, t);
            name = t->text;
            consume(p);
            if (peek(p)->type == T_LPAREN) {
                consume(p);
                if (peek(p)->type != T_RPAREN) syntax_error(p, peek(p));
                consume(p);
            }
            return parse_funcbody(p, name);
        } else {
            if (guard == 0) p->alias_next = -1;
            if (guard++ < 20 && try_alias(p)) goto again;
            return parse_simple(p);
        }
        parse_trailing_redirs(p, n);
        return n;
    }
    if (t->type == T_LPAREN) {
        if (p->src[t->start + 1] == '(') {
            int end;
            char *expr = scan_dparen(p, t->start, &end);
            if (expr) {
                n = new_node(p, N_ARITH);
                n->u.arith.expr = expr;
                p->pos = end;
                p->have_tok = 0;
                parse_trailing_redirs(p, n);
                return n;
            }
        }
        n = new_node(p, N_SUBSHELL);
        consume(p);
        skip_newlines(p);
        n->u.one.body = parse_list(p, 1);
        if (!n->u.one.body) syntax_error(p, peek(p));
        if (peek(p)->type != T_RPAREN) syntax_error(p, peek(p));
        consume(p);
        parse_trailing_redirs(p, n);
        return n;
    }
    if (t->type == T_REDIR) return parse_simple(p);
    syntax_error(p, t);
    return NULL;
}

static Node *parse_pipeline(Parser *p)
{
    Node *cmds[64];
    int n = 0, bang = 0, timed = 0;
    Node *pn;
    while (peek_word(p, "!") || peek_word(p, "time")) {
        if (peek_word(p, "!")) bang = !bang;
        else timed = 1;
        consume(p);
        if (timed && peek_word(p, "-p")) {
            consume(p);
            timed = 2;                 /* POSIX output format */
        }
    }
    if (timed && !starts_command(p)) {
        /* bare "time" */
        pn = new_node(p, N_PIPE);
        pn->u.pipe.n = 0;
        pn->u.pipe.timed = 1;
        pn->u.pipe.bang = bang;
        return pn;
    }
    for (;;) {
        Tok *t;
        Node *c = parse_command(p);
        if (n < 64) cmds[n++] = c;
        t = peek(p);
        if (t->type == T_PIPE || t->type == T_PIPEAMP) {
            if (t->type == T_PIPEAMP) {
                /* cmd |& cmd  ==  cmd 2>&1 | cmd */
                Redir *r = arena_alloc(p->a, sizeof(Redir));
                r->op = R_DUPOUT;
                r->fd = 2;
                r->target = "1";
                append_redir(&c->redirs, r);
            }
            consume(p);
            skip_newlines(p);
            continue;
        }
        break;
    }
    if (n == 1 && !bang && !timed) return cmds[0];
    pn = new_node(p, N_PIPE);
    pn->u.pipe.cmds = arena_alloc(p->a, n * sizeof(Node *));
    memcpy(pn->u.pipe.cmds, cmds, n * sizeof(Node *));
    pn->u.pipe.n = n;
    pn->u.pipe.bang = bang;
    pn->u.pipe.timed = timed;
    pn->lineno = cmds[0]->lineno;
    return pn;
}

static Node *parse_andor(Parser *p)
{
    Node *l = parse_pipeline(p);
    for (;;) {
        Tok *t = peek(p);
        Node *n;
        if (t->type != T_AND && t->type != T_OR) break;
        n = new_node(p, t->type == T_AND ? N_AND : N_OR);
        consume(p);
        skip_newlines(p);
        n->u.bin.l = l;
        n->u.bin.r = parse_pipeline(p);
        n->lineno = l->lineno;
        l = n;
    }
    return l;
}

static Node *mkbin(Parser *p, int type, Node *l, Node *r)
{
    Node *n = new_node(p, type);
    n->u.bin.l = l;
    n->u.bin.r = r;
    n->lineno = l->lineno;
    return n;
}

/* list of and-or lists separated by ; & (and newlines if multiline) */
static Node *parse_list(Parser *p, int multiline)
{
    Node *result = NULL;
    for (;;) {
        Node *c;
        Tok *t;
        if (multiline) skip_newlines(p);
        if (!starts_command(p)) break;
        c = parse_andor(p);
        t = peek(p);
        if (t->type == T_AMP) {
            Node *bg = new_node(p, N_BG);
            bg->u.one.body = c;
            bg->lineno = c->lineno;
            c = bg;
            consume(p);
        } else if (t->type == T_SEMI) {
            consume(p);
        } else if (t->type == T_NEWLINE && multiline) {
            consume(p);
        } else {
            result = result ? mkbin(p, N_SEQ, result, c) : c;
            break;
        }
        result = result ? mkbin(p, N_SEQ, result, c) : c;
        if (!multiline && peek(p)->type == T_NEWLINE) break;
    }
    return result;
}

/* ---------------------------------------------------------------- */
/* public API                                                         */

Parser *parser_new(const char *src, Arena *arena, int lineno)
{
    Parser *p = xcalloc(sizeof(Parser));
    p->a = arena;
    p->src = arena_strdup(arena, src);
    p->line0 = lineno;
    p->lline = lineno;
    p->alias_next = -1;
    return p;
}

void parser_free(Parser *p)
{
    xfree(p);
}

void parser_set_final(Parser *p)
{
    p->final = 1;
}

int parser_lineno(Parser *p)
{
    return line_at(p, p->pos);
}

const char *parser_errmsg(Parser *p)
{
    return p->err;
}

/* text and line of the token a syntax error was found at ("" / 0) */
const char *parser_errline(Parser *p, int *lineno)
{
    if (lineno) *lineno = p->errlineno;
    return p->errline;
}

const char *parser_pos(Parser *p)
{
    return p->src + p->pos;
}

int parser_next(Parser *p, Node **out)
{
    Node *n;
    Tok *t;
    *out = NULL;
    p->incomplete = 0;
    p->err[0] = 0;
    p->nal = 0;
    if (setjmp(p->jb)) {
        p->have_tok = 0;
        p->npending = 0;
        return p->incomplete ? PARSE_INCOMPLETE : PARSE_ERROR;
    }
    t = peek(p);
    if (t->type == T_EOF) return PARSE_EOF;
    if (t->type == T_NEWLINE) {
        consume(p);
        return PARSE_OK;
    }
    n = parse_list(p, 0);
    t = peek(p);
    if (t->type == T_NEWLINE) {
        consume(p);
    } else if (t->type != T_EOF) {
        syntax_error(p, t);
    } else if (p->npending) {
        incomplete(p);
    }
    *out = n;
    return PARSE_OK;
}

/* skip to the next line after a syntax error */
void parser_skip_line(Parser *p)
{
    const char *s = p->src + p->pos;
    const char *e = strchr(s, '\n');
    p->pos = e ? (int)(e - p->src) + 1 : (int)strlen(p->src);
    p->have_tok = 0;
}

Node *parse_string(const char *src, Arena *a, int *status)
{
    Parser *p = parser_new(src, a, 1);
    Node *result = NULL;
    int r;
    *status = 0;
    for (;;) {
        Node *n;
        r = parser_next(p, &n);
        if (r == PARSE_EOF) break;
        if (r != PARSE_OK) {
            sh_error("%s", p->err);
            *status = 2;
            parser_free(p);
            return NULL;
        }
        if (n) {
            if (!result) {
                result = n;
            } else {
                Node *s = arena_alloc(a, sizeof(Node));
                s->type = N_SEQ;
                s->lineno = result->lineno;
                s->u.bin.l = result;
                s->u.bin.r = n;
                result = s;
            }
        }
    }
    parser_free(p);
    return result;
}

/* find the ')' closing a $( ... ); s points just after "$(" */
const char *scan_cmdsub_end(const char *s)
{
    Arena *a = arena_new();
    Parser *p = xcalloc(sizeof(Parser));
    const char *res = NULL;
    p->a = a;
    p->src = (char *)s;   /* not modified unless an alias is spliced */
    p->line0 = 1;
    p->lline = 1;
    p->stop_rparen = 1;
    if (setjmp(p->jb) == 0) {
        Tok *t;
        skip_newlines(p);
        parse_list(p, 1);
        t = peek(p);
        if (t->type == T_RPAREN && p->src == s) res = s + t->start;
        else if (t->type == T_EOF) res = NULL;
        else res = (const char *)-1;
    } else {
        res = p->incomplete ? NULL : (const char *)-1;
    }
    if (res == (const char *)-1) {
        /* fall back to simple paren matching */
        const char *e = skip_parens(s);
        res = e ? e - 1 : NULL;
    }
    xfree(p);
    arena_free(a);
    return res;
}

/* ---------------------------------------------------------------- */
/* pretty printer (declare -f, type)                                  */

static void indent(SBuf *b, int n)
{
    int i;
    for (i = 0; i < n; i++) sb_adds(b, "    ");
}

static void print_redirs(Redir *r, SBuf *b)
{
    static const char *const ops[] = {
        "<", ">", ">>", ">|", "<>", "<&", ">&", "<<", "<<<", "&>", "&>>"
    };
    for (; r; r = r->next) {
        sb_addc(b, ' ');
        if (r->fd >= 0) sb_printf(b, "%d", r->fd);
        sb_adds(b, ops[r->op]);
        if (r->op == R_HEREDOC && r->striptabs) sb_addc(b, '-');
        if (r->op != R_HEREDOC && r->op != R_DUPIN && r->op != R_DUPOUT)
            sb_addc(b, ' ');
        if (r->op == R_HEREDOC) {
            if (r->quoted) sb_printf(b, "'%s'", r->target);
            else sb_adds(b, r->target);
        } else {
            sb_adds(b, r->target);
        }
    }
}

static void print_heredocs(Redir *r, SBuf *b)
{
    for (; r; r = r->next) {
        if (r->op == R_HEREDOC) {
            sb_addc(b, '\n');
            sb_adds(b, r->heredoc ? r->heredoc : "");
            sb_adds(b, r->target);
            sb_addc(b, '\n');
        }
    }
}

static void add_trimmed(SBuf *b, const char *s)
{
    int l;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    l = (int)strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\n')) l--;
    sb_addn(b, s, l);
}

static int has_heredoc(Node *n)
{
    Redir *r;
    if (!n) return 0;
    if (n->type == N_PIPE && n->u.pipe.n > 0) return has_heredoc(n->u.pipe.cmds[n->u.pipe.n - 1]);
    if (n->type == N_AND || n->type == N_OR) return has_heredoc(n->u.bin.r);
    for (r = n->redirs; r; r = r->next)
        if (r->op == R_HEREDOC) return 1;
    return 0;
}

/* term: also terminate the last command with ';' (bodies of if, while,
 * for: the way bash prints them) */
static void print_list(Node *n, SBuf *b, int ind, int term);
static int hd_after;             /* the last command printed had a here-document */

static void print_cond(CondNode *c, SBuf *b)
{
    switch (c->type) {
    case C_AND:
    case C_OR:
        print_cond(c->l, b);
        sb_adds(b, c->type == C_AND ? " && " : " || ");
        print_cond(c->r, b);
        break;
    case C_NOT:
        sb_adds(b, "! ");
        print_cond(c->l, b);
        break;
    case C_UNARY:
        sb_printf(b, "%s %s", c->op, c->w1);
        break;
    case C_BINARY:
        sb_printf(b, "%s %s %s", c->w1, c->op, c->w2);
        break;
    default:
        sb_adds(b, "-n ");          /* bash shows [[ word ]] as [[ -n word ]] */
        sb_adds(b, c->w1);
    }
}

static void print_body(Node *n, SBuf *b, int ind)
{
    indent(b, ind);
    print_list(n, b, ind, 1);
    sb_addc(b, '\n');
}

static void print_cmd(Node *n, SBuf *b, int ind)
{
    Word *w;
    CaseItem *ci;
    if (!n) return;
    switch (n->type) {
    case N_SIMPLE: {
        int first = 1;
        for (w = n->u.simple.assigns; w; w = w->next) {
            if (!first) sb_addc(b, ' ');
            sb_adds(b, w->text);
            first = 0;
        }
        for (w = n->u.simple.words; w; w = w->next) {
            if (!first) sb_addc(b, ' ');
            sb_adds(b, w->text);
            first = 0;
        }
        break;
    }
    case N_PIPE: {
        int i;
        if (n->u.pipe.timed) sb_adds(b, n->u.pipe.timed == 2 ? "time -p " : "time ");
        if (n->u.pipe.bang) sb_adds(b, "! ");
        for (i = 0; i < n->u.pipe.n; i++) {
            if (i) sb_adds(b, " | ");
            print_cmd(n->u.pipe.cmds[i], b, ind);
        }
        return;
    }
    case N_AND:
    case N_OR:
        print_cmd(n->u.bin.l, b, ind);
        sb_adds(b, n->type == N_AND ? " && " : " || ");
        print_cmd(n->u.bin.r, b, ind);
        return;
    case N_SEQ:
    case N_BG:
        print_list(n, b, ind, 0);
        return;
    case N_NOT:
        sb_adds(b, "! ");
        print_cmd(n->u.one.body, b, ind);
        return;
    case N_SUBSHELL:
        sb_adds(b, "( ");
        print_list(n->u.one.body, b, ind, 0);
        sb_adds(b, " )");
        break;
    case N_GROUP:
        sb_adds(b, "{ \n");
        indent(b, ind + 1);
        print_list(n->u.one.body, b, ind + 1, 0);
        sb_addc(b, '\n');
        indent(b, ind);
        sb_adds(b, "}");
        break;
    case N_IF:
        sb_adds(b, "if ");
        print_list(n->u.ifn.cond, b, ind + 1, 0);
        sb_adds(b, "; then\n");
        print_body(n->u.ifn.then, b, ind + 1);
        if (n->u.ifn.els) {
            /* elif is printed as else + nested if, like bash */
            indent(b, ind);
            sb_adds(b, "else\n");
            print_body(n->u.ifn.els, b, ind + 1);
        }
        indent(b, ind);
        sb_adds(b, "fi");
        break;
    case N_WHILE:
    case N_UNTIL:
        sb_adds(b, n->type == N_WHILE ? "while " : "until ");
        print_list(n->u.loop.cond, b, ind + 1, 0);
        sb_adds(b, "; do\n");
        print_body(n->u.loop.body, b, ind + 1);
        indent(b, ind);
        sb_adds(b, "done");
        break;
    case N_FOR:
    case N_SELECT:
        sb_printf(b, "%s %s", n->type == N_FOR ? "for" : "select", n->u.forn.var);
        if (n->u.forn.has_in) {
            sb_adds(b, " in");
            for (w = n->u.forn.words; w; w = w->next) sb_printf(b, " %s", w->text);
        }
        sb_adds(b, ";\n");
        indent(b, ind);
        sb_adds(b, "do\n");
        print_body(n->u.forn.body, b, ind + 1);
        indent(b, ind);
        sb_adds(b, "done");
        break;
    case N_ARITHFOR:
        sb_adds(b, "for ((");
        add_trimmed(b, n->u.afor.init);
        sb_adds(b, "; ");
        add_trimmed(b, n->u.afor.cond);
        sb_adds(b, "; ");
        add_trimmed(b, n->u.afor.step);
        sb_adds(b, "))\n");
        indent(b, ind);
        sb_adds(b, "do\n");
        print_body(n->u.afor.body, b, ind + 1);
        indent(b, ind);
        sb_adds(b, "done");
        break;
    case N_CASE:
        sb_printf(b, "case %s in \n", n->u.casen.word);
        for (ci = n->u.casen.items; ci; ci = ci->next) {
            indent(b, ind + 1);
            for (w = ci->pats; w; w = w->next) {
                sb_adds(b, w->text);
                if (w->next) sb_adds(b, " | ");
            }
            sb_adds(b, ")\n");
            indent(b, ind + 2);
            print_list(ci->body, b, ind + 2, 0);
            sb_addc(b, '\n');
            indent(b, ind + 1);
            sb_adds(b, ci->term == 0 ? ";;\n" : ci->term == 1 ? ";&\n" : ";;&\n");
        }
        indent(b, ind);
        sb_adds(b, "esac");
        break;
    case N_FUNCDEF:
        sb_printf(b, "%s () \n", n->u.func.name);
        indent(b, ind);
        if (n->u.func.body && n->u.func.body->type != N_GROUP) {
            sb_adds(b, "{ \n");
            indent(b, ind + 1);
            print_cmd(n->u.func.body, b, ind + 1);
            sb_addc(b, '\n');
            indent(b, ind);
            sb_adds(b, "}");
        } else {
            print_cmd(n->u.func.body, b, ind);
        }
        return;
    case N_ARITH:
        sb_printf(b, "((%s))", n->u.arith.expr);
        break;
    case N_COND:
        sb_adds(b, "[[ ");
        print_cond(n->u.cond.expr, b);
        sb_adds(b, " ]]");
        break;
    }
    print_redirs(n->redirs, b);
    print_heredocs(n->redirs, b);
}

static void print_list(Node *n, SBuf *b, int ind, int term)
{
    if (!n) return;
    if (n->type == N_SEQ) {
        Node *last = n->u.bin.l;
        print_list(n->u.bin.l, b, ind, 1);
        while (last->type == N_SEQ) last = last->u.bin.r;
        if (last->type == N_BG) {
            sb_addc(b, ' ');       /* "cmd & next" stays on one line */
        } else {
            sb_addc(b, '\n');
            indent(b, ind);
        }
        print_list(n->u.bin.r, b, ind, term);
        return;
    }
    if (n->type == N_BG) {
        print_cmd(n->u.one.body, b, ind);
        sb_adds(b, " &");
        return;
    }
    print_cmd(n, b, ind);
    {
        /* as bash: a command with a here-document ends its line, and the
         * command after it gets no ';' either (bash 5.3: also "! cmd") */
        int after = hd_after;
        hd_after = has_heredoc(n);
        if (term && !hd_after && !after) sb_addc(b, ';');
    }
}

void node_to_text(Node *n, SBuf *b, int ind)
{
    if (n && n->type != N_GROUP) {
        /* function bodies other than { } are shown inside braces */
        sb_adds(b, "{ \n");
        indent(b, ind + 1);
        print_list(n, b, ind + 1, 0);
        sb_addc(b, '\n');
        indent(b, ind);
        sb_adds(b, "}");
        return;
    }
    print_list(n, b, ind, 0);
}

/* scanners exported for expand.c */
const char *scan_skip_squote(const char *s) { return skip_squote(s); }
const char *scan_skip_dquote(const char *s) { return skip_dquote(s); }
const char *scan_skip_dollar(const char *s) { return skip_dollar(s); }
const char *scan_skip_backtick(const char *s) { return skip_backtick(s); }
const char *scan_skip_brace(const char *s) { return skip_brace(s); }
const char *scan_skip_ansi(const char *s) { return skip_ansi(s); }
const char *scan_skip_parens(const char *s) { return skip_parens(s); }
const char *scan_word_end(const char *s) { return scan_word(s); }
