/*
 * InstallTTF - install TrueType/OpenType fonts for diskfont.library, the
 * way AATextManager or FTManager do, from the Shell:
 *
 *   InstallTTF FILES/M/A,NAME/K,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,COPY/S,REPLACE/S,INFO/S
 *
 * For each font file it writes FONTS:<name>.otag (the outline font tags:
 * engine, family, style, the font file, a code page, the sizes to list)
 * and FONTS:<name>.font, so that every program can open the font with
 * OpenDiskFont() through the font engine library (aatext.library from
 * AAText, or freetype2.library), and AAText draws it anti-aliased.
 *
 * It reads the font's own tables (name, OS/2, post, head, hhea, hmtx,
 * cmap), so it needs no FreeType itself and runs on any Amiga with OS 2.04
 * or newer.  Public domain / MIT, like AmiBash.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/dosasl.h>
#include <graphics/text.h>
#include <diskfont/diskfont.h>
#include <diskfont/diskfonttag.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "charsets.h"

static const char version[] = "$VER: InstallTTF 1.4 (22.10.2026)";

struct Library *DiskfontBase;
struct GfxBase *GfxBase;

#define TAG(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))

/* ---- reading the font file ------------------------------------------- */

typedef struct Font {
    BPTR fh;
    ULONG base;                          /* this face's table directory */
    ULONG nfaces;
    struct { ULONG tag, off, len; } tab[64];
    int ntab;
    /* what we learn */
    char family[128], style[64], full[128];
    UWORD upem, weight;
    int italic, fixed, serif;
    LONG space_adv;                      /* font units */
    LONG hasc, hdesc;                    /* hhea ascender, -descender */
} Font;

static int read_at(BPTR fh, ULONG off, void *buf, ULONG len)
{
    if (Seek(fh, (LONG)off, OFFSET_BEGINNING) < 0) return 0;
    return Read(fh, buf, (LONG)len) == (LONG)len;
}

/* utility.library's Stricmp() needs V37; these work everywhere */
static int ci_ncmp(const char *a, const char *b, size_t n)
{
    for (; n; a++, b++, n--) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a) return d;
    }
    return 0;
}
#define Stricmp(a, b) ci_ncmp((const char *)(a), (const char *)(b), (size_t)-1)
#define Strnicmp(a, b, n) ci_ncmp((const char *)(a), (const char *)(b), (size_t)(n))

static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static UWORD be16(const UBYTE *p) { return (UWORD)(p[0] << 8 | p[1]); }

static int find_table(Font *f, ULONG tag, ULONG *off, ULONG *len)
{
    int i;
    for (i = 0; i < f->ntab; i++)
        if (f->tab[i].tag == tag) {
            *off = f->tab[i].off;
            *len = f->tab[i].len;
            return 1;
        }
    return 0;
}

/* the whole table in memory (FreeVec it); NULL if absent */
static UBYTE *load_table(Font *f, ULONG tag, ULONG *len)
{
    ULONG off;
    UBYTE *b;
    if (!find_table(f, tag, &off, len) || *len == 0 || *len > 4000000) return NULL;
    if (!(b = AllocVec(*len, MEMF_ANY))) return NULL;
    if (!read_at(f->fh, off, b, *len)) {
        FreeVec(b);
        return NULL;
    }
    return b;
}

/* open the file and face; 0 if it is not a TrueType/OpenType font */
static int font_open(Font *f, const char *path, ULONG face)
{
    UBYTE h[12], e[16];
    ULONG i;
    memset(f, 0, sizeof(*f));
    if (!(f->fh = Open((STRPTR)path, MODE_OLDFILE))) return 0;
    if (!read_at(f->fh, 0, h, 12)) return 0;
    f->nfaces = 1;
    if (be32(h) == TAG('t', 't', 'c', 'f')) {   /* a collection */
        f->nfaces = be32(h + 8);
        if (face >= f->nfaces || !read_at(f->fh, 12 + face * 4, e, 4)) return 0;
        f->base = be32(e);
        if (!read_at(f->fh, f->base, h, 12)) return 0;
    } else if (face > 0)
        return 0;
    if (be32(h) != 0x00010000 && be32(h) != TAG('O', 'T', 'T', 'O') && be32(h) != TAG('t', 'r', 'u', 'e'))
        return 0;
    f->ntab = be16(h + 4);
    if (f->ntab > 64) f->ntab = 64;
    for (i = 0; i < (ULONG)f->ntab; i++) {
        if (!read_at(f->fh, f->base + 12 + i * 16, e, 16)) return 0;
        f->tab[i].tag = be32(e);
        f->tab[i].off = be32(e + 8);
        f->tab[i].len = be32(e + 12);
    }
    return 1;
}

static void font_close(Font *f)
{
    if (f->fh) Close(f->fh);
    f->fh = 0;
}

/* a name record as Latin-1 (other characters become '?') */
static int get_name(const UBYTE *nm, ULONG len, int id, char *out, int max)
{
    UWORD count, strings, i;
    int best = -1, bestscore = 0;
    if (len < 6) return 0;
    count = be16(nm + 2);
    strings = be16(nm + 4);
    for (i = 0; i < count && 6 + i * 12 + 12 <= len; i++) {
        const UBYTE *r = nm + 6 + i * 12;
        int score = 0;
        if (be16(r + 6) != id) continue;
        if (be16(r) == 3 && (be16(r + 2) == 1 || be16(r + 2) == 10)) score = be16(r + 4) == 0x409 ? 3 : 2;
        else if (be16(r) == 1 && be16(r + 2) == 0) score = be16(r + 4) == 0 ? 2 : 1;
        else if (be16(r) == 0) score = 1;
        if (score > bestscore) {
            bestscore = score;
            best = i;
        }
    }
    if (best < 0) return 0;
    {
        const UBYTE *r = nm + 6 + best * 12;
        ULONG l = be16(r + 8), o = strings + be16(r + 10), k;
        int n = 0, wide = be16(r) != 1;
        if (o + l > len) return 0;
        for (k = 0; k < l && n < max - 1; k += wide ? 2 : 1) {
            unsigned c = wide ? be16(nm + o + k) : nm[o + k];
            if (!wide && c >= 0x80) c = '?';                 /* Mac Roman: keep it simple */
            out[n++] = (char)(c < 0x20 ? ' ' : c < 0x100 ? c : '?');
        }
        out[n] = 0;
        return n > 0;
    }
}

/* glyph index of a character through cmap format 4 or 12 */
static UWORD glyph_of(const UBYTE *cm, ULONG len, ULONG ch)
{
    UWORD n, i;
    if (len < 4) return 0;
    n = be16(cm + 2);
    for (i = 0; i < n && 4 + i * 8 + 8 <= len; i++) {
        const UBYTE *r = cm + 4 + i * 8;
        UWORD pid = be16(r), eid = be16(r + 2);
        ULONG o = be32(r + 4);
        if (!((pid == 3 && (eid == 1 || eid == 10)) || pid == 0) || o + 8 > len) continue;
        if (be16(cm + o) == 4) {
            UWORD segx2 = be16(cm + o + 6), s;
            const UBYTE *ends = cm + o + 14, *starts = ends + segx2 + 2, *deltas = starts + segx2,
                        *ranges = deltas + segx2;
            if (o + 16 + segx2 * 4 > len) continue;
            for (s = 0; s < segx2; s += 2)
                if (be16(ends + s) >= ch) {
                    UWORD st = be16(starts + s), ro = be16(ranges + s);
                    if (st > ch) return 0;
                    if (!ro) return (UWORD)(ch + be16(deltas + s));
                    {
                        const UBYTE *g = ranges + s + ro + (ch - st) * 2;
                        if (g + 2 > cm + len || !be16(g)) return 0;
                        return (UWORD)(be16(g) + be16(deltas + s));
                    }
                }
        } else if (be16(cm + o) == 12) {
            ULONG ng = be32(cm + o + 12), k;
            for (k = 0; k < ng && o + 16 + k * 12 + 12 <= len; k++) {
                const UBYTE *g = cm + o + 16 + k * 12;
                if (ch >= be32(g) && ch <= be32(g + 4)) return (UWORD)(be32(g + 8) + ch - be32(g));
            }
        }
    }
    return 0;
}

static int font_info(Font *f)
{
    UBYTE b[64], *t;
    ULONG off, len;
    static const char *const plain[] = {"Regular", "Book", "Normal", "Roman", "Plain", "Medium", NULL};
    int i;
    if (!find_table(f, TAG('h', 'e', 'a', 'd'), &off, &len) || !read_at(f->fh, off, b, 54)) return 0;
    f->upem = be16(b + 18);
    if (!f->upem) f->upem = 2048;
    f->weight = 400;
    if (find_table(f, TAG('O', 'S', '/', '2'), &off, &len) && len >= 64 && read_at(f->fh, off, b, 64)) {
        f->weight = be16(b + 4);
        f->italic = (be16(b + 62) & 1) != 0;
        f->space_adv = (WORD)be16(b + 2);          /* xAvgCharWidth until we know better */
        f->serif = b[32] == 2 && b[33] >= 2 && b[33] <= 10;   /* PANOSE: text, serif styles */
    }
    if (find_table(f, TAG('p', 'o', 's', 't'), &off, &len) && read_at(f->fh, off, b, 16))
        f->fixed = be32(b + 12) != 0;
    if ((t = load_table(f, TAG('n', 'a', 'm', 'e'), &len)) != NULL) {
        if (!get_name(t, len, 16, f->family, sizeof(f->family))) get_name(t, len, 1, f->family, sizeof(f->family));
        if (!get_name(t, len, 17, f->style, sizeof(f->style))) get_name(t, len, 2, f->style, sizeof(f->style));
        get_name(t, len, 4, f->full, sizeof(f->full));
        FreeVec(t);
    }
    if (!f->family[0]) return 0;
    if (strstr(f->style, "Italic") || strstr(f->style, "Oblique")) f->italic = 1;
    /* the space's advance from cmap + hmtx */
    if (find_table(f, TAG('h', 'h', 'e', 'a'), &off, &len) && read_at(f->fh, off, b, 36)) {
        UWORD nh = be16(b + 34), g;
        f->hasc = (WORD)be16(b + 4);
        f->hdesc = -(LONG)(WORD)be16(b + 6);
        ULONG cl, ho, hl;
        UBYTE *cm = load_table(f, TAG('c', 'm', 'a', 'p'), &cl);
        if (cm) {
            g = glyph_of(cm, cl, ' ');
            FreeVec(cm);
            if (nh && find_table(f, TAG('h', 'm', 't', 'x'), &ho, &hl)) {
                if (g >= nh) g = nh - 1;
                if (read_at(f->fh, ho + g * 4, b, 2)) f->space_adv = be16(b);
            }
        }
    }
    for (i = 0; plain[i]; i++)
        if (!Stricmp((STRPTR)f->style, (STRPTR)plain[i])) f->style[0] = 0;
    return 1;
}

/* ---- writing the .otag and .font ----------------------------------- */

typedef struct Otag {
    ULONG tags[64];
    int nt;
    UBYTE data[2048];
    ULONG nd;
} Otag;

static void ot_val(Otag *o, ULONG tag, ULONG v)
{
    o->tags[o->nt++] = tag;
    o->tags[o->nt++] = v;
}

/* indirect data: the tag's value is the offset of the data in the file,
 * fixed up once the tag list's size is known */
static void ot_data(Otag *o, ULONG tag, const void *p, ULONG len)
{
    ot_val(o, tag, o->nd);
    memcpy(o->data + o->nd, p, len);
    o->nd += len;
    if (o->nd & 1) o->data[o->nd++] = 0;
}

static void ot_str(Otag *o, ULONG tag, const char *s)
{
    ot_data(o, tag, s, strlen(s) + 1);
}

static int write_file(const char *name, const void *a, ULONG al, const void *b, ULONG bl)
{
    BPTR fh = Open((STRPTR)name, MODE_NEWFILE);
    int ok;
    if (!fh) return 0;
    ok = Write(fh, (APTR)a, (LONG)al) == (LONG)al && (!bl || Write(fh, (APTR)b, (LONG)bl) == (LONG)bl);
    Close(fh);
    if (!ok) DeleteFile((STRPTR)name);
    return ok;
}

static UBYTE stem_weight(UWORD w)
{
    return w <= 150 ? OTS_Thin : w <= 250 ? OTS_ExtraLight : w <= 350 ? OTS_Light : w <= 450 ? OTS_Book :
           w <= 550 ? OTS_Medium : w <= 650 ? OTS_SemiBold : w <= 750 ? OTS_Bold : w <= 850 ? OTS_ExtraBold :
           OTS_Black;
}

/* "8-16,18,20,24" -> sorted list, at most OT_MAXAVAILSIZES */
static int parse_sizes(const char *s, UWORD *out)
{
    int n = 0;
    while (*s && n < OT_MAXAVAILSIZES) {
        LONG a = 0, b;
        while (*s && !isdigit((unsigned char)*s)) s++;
        while (isdigit((unsigned char)*s)) a = a * 10 + (*s++ - '0');
        b = a;
        if (*s == '-') {
            s++;
            b = 0;
            while (isdigit((unsigned char)*s)) b = b * 10 + (*s++ - '0');
        }
        for (; a <= b && n < OT_MAXAVAILSIZES; a++)
            if (a >= 4 && a <= 999) out[n++] = (UWORD)a;
    }
    return n;
}

static int exists(const char *name)
{
    BPTR l = Lock((STRPTR)name, SHARED_LOCK);
    if (l) UnLock(l);
    return l != 0;
}

static int copy_file(const char *from, const char *to)
{
    BPTR in = Open((STRPTR)from, MODE_OLDFILE), out;
    static UBYTE buf[16384];
    LONG n;
    int ok = 1;
    if (!in) return 0;
    if (!(out = Open((STRPTR)to, MODE_NEWFILE))) {
        Close(in);
        return 0;
    }
    while ((n = Read(in, buf, sizeof(buf))) > 0)
        if (Write(out, buf, n) != n) {
            ok = 0;
            break;
        }
    if (n < 0) ok = 0;
    Close(in);
    Close(out);
    if (!ok) DeleteFile((STRPTR)to);
    return ok;
}

struct Opts {
    char *name, *charset, *engine, *sizes, *to;
    LONG face;
    int haveface, copy, replace, info, nocheck;
};

/* make a name fit for FONTS: (no : or /) */
static void clean_name(char *s)
{
    for (; *s; s++)
        if (*s == ':' || *s == '/' || *s == '"') *s = ' ';
}

static int install(const char *path, ULONG face, struct Opts *o, ULONG *nfaces)
{
    Font f;
    Otag *ot;
    char name[128], fontfile[300], otagname[300], fontname[300], *fp;
    UWORD cp[256], sizes[OT_MAXAVAILSIZES + 1];
    int i, ns, cs = -1, ok = 0;
    if (!font_open(&f, path, face) || !font_info(&f)) {
        font_close(&f);
        Printf((STRPTR)"%s: not a TrueType or OpenType font InstallTTF can read\n", (ULONG)path);
        return 0;
    }
    *nfaces = f.nfaces;
    font_close(&f);


    /* the name in FONTS: family, plus the style unless it is the plain one */
    if (o->name && (f.nfaces == 1 || o->haveface)) strcpy(name, o->name);
    else if (f.style[0]) sprintf(name, "%.80s %.40s", f.family, f.style);
    else sprintf(name, "%.120s", f.family);
    clean_name(name);
    if (strlen(name) > 26)
        Printf((STRPTR)"%s: the name \"%s\" is long; some programs show only 26 characters (NAME= sets "
                       "another)\n", (ULONG)path, (ULONG)name);

    if (o->charset && !Stricmp(o->charset, "none")) {
        cs = -1;
    } else if (o->charset) {
        for (cs = 0; cs < (int)NCHARSETS && Stricmp((STRPTR)o->charset, (STRPTR)charsets[cs].name); cs++) ;
        if (cs == (int)NCHARSETS) {
            Printf((STRPTR)"unknown CHARSET %s; known:", (ULONG)o->charset);
            for (cs = 0; cs < (int)NCHARSETS; cs++) Printf((STRPTR)" %s", (ULONG)charsets[cs].name);
            Printf((STRPTR)"\n");
            return 0;
        }
    } else
        /* aatext.library takes the code page from the .otag (latin1
         * unless CHARSET says otherwise); freetype2.library uses
         * ENV:ftcodepage, and some versions hang on one in the .otag */
        cs = Stricmp(o->engine, "aatext") ? -1 : 0;
    for (i = 0; i < 128; i++) cp[i] = (UWORD)i;
    for (i = 0; cs >= 0 && i < 128; i++) cp[128 + i] = charsets[cs].hi[i];
    ns = parse_sizes(o->sizes ? o->sizes : "8-16,18,20,24", sizes + 1);
    sizes[0] = (UWORD)ns;

    /* where the font file is: copied to TO (FONTS:_ttf), or where it is */
    fp = (char *)FilePart((STRPTR)path);
    if (o->copy) {
        const char *dir = o->to ? o->to : "FONTS:_ttf";
        BPTR l;
        if (!exists(dir) && (l = CreateDir((STRPTR)dir)) != 0) UnLock(l);
        strcpy(fontfile, dir);
        AddPart((STRPTR)fontfile, (STRPTR)fp, sizeof(fontfile));
    } else if (!Strnicmp((STRPTR)path, (STRPTR)"FONTS:", 6)) {
        strcpy(fontfile, path);
    } else {
        BPTR l = Lock((STRPTR)path, SHARED_LOCK);
        if (!l || !NameFromLock(l, (STRPTR)fontfile, sizeof(fontfile))) strcpy(fontfile, path);
        if (l) UnLock(l);
    }
    sprintf(otagname, "FONTS:%s.otag", name);
    sprintf(fontname, "FONTS:%s.font", name);

    Printf((STRPTR)"%s%s -> FONTS:%s.font\n", (ULONG)path, (ULONG)(f.nfaces > 1 ? " (a face)" : ""), (ULONG)name);
    Printf((STRPTR)"  family \"%s\"%s%s, weight %ld%s%s, %s, engine %s\n", (ULONG)f.family,
           (ULONG)(f.style[0] ? ", style " : ""), (ULONG)f.style, (ULONG)f.weight,
           (ULONG)(f.italic ? ", italic" : ""), (ULONG)(f.fixed ? ", fixed width" : ""),
           (ULONG)(cs >= 0 ? charsets[cs].name : "no code page"), (ULONG)o->engine);
    Printf((STRPTR)"  font file %s\n", (ULONG)fontfile);
    if (o->info) return 1;

    if (!o->replace && (exists(otagname) || exists(fontname))) {
        Printf((STRPTR)"  FONTS:%s.font exists already; REPLACE overwrites it\n", (ULONG)name);
        return 0;
    }
    if (o->copy && Stricmp((STRPTR)path, (STRPTR)fontfile) && !copy_file(path, fontfile)) {
        Printf((STRPTR)"  cannot copy the font to %s\n", (ULONG)fontfile);
        return 0;
    }

    if (!(ot = AllocVec(sizeof(Otag), MEMF_CLEAR))) return 0;
    ot_val(ot, OT_FileIdent, 0);                       /* the file size, below */
    ot_str(ot, OT_Engine, o->engine);
    ot_str(ot, OT_Family, f.family);
    ot_val(ot, OT_YSizeFactor, 0x00010001);
    ot_val(ot, OT_SpaceWidth, (ULONG)((f.space_adv * 2540L * 250L / 72L) / f.upem));
    ot_val(ot, OT_IsFixed, f.fixed);
    ot_val(ot, OT_SerifFlag, f.serif);
    ot_val(ot, OT_StemWeight, stem_weight(f.weight));
    ot_val(ot, OT_SlantStyle, f.italic ? OTS_Italic : OTS_Upright);
    ot_val(ot, OT_HorizStyle, OTH_Normal);
    /* the space's width as a fraction of the em (16.16): 3.x diskfont makes
     * a fixed-width font's cells this times the pixel height wide, so 1.0
     * gives square cells (AATextManager's "fixed width cells too wide") */
    {
        /* The width of a fixed-width font's cells: SpaceFactor times the
         * pixel height.  With metric 2 the engine fits the hhea ascender +
         * descender into that height, so the cell must be the advance as a
         * fraction of that span, not of the em: else the cells are wider
         * than the letters, and AAText, which sizes its letters to fill
         * the cells, draws them too big and they are cut off at the top. */
        LONG span = f.hasc + f.hdesc > 0 ? f.hasc + f.hdesc : f.upem;
        ot_val(ot, OT_SpaceFactor, f.fixed ? (ULONG)(((ULONG)f.space_adv << 16) / span) : 0x10000);
    }
    ot_val(ot, OT_InhibitAlgoStyle, f.italic ? 7 : 3);
    ot_val(ot, OT_SpecCount, cs >= 0 ? 5 : 4);
    ot_str(ot, OT_Spec1 | OT_Indirect, fontfile);                        /* the font file */
    if (cs >= 0) ot_data(ot, (OT_Spec1 + 1) | OT_Indirect, cp, sizeof(cp));   /* code page */
    ot_str(ot, (OT_Spec1 + 2) | OT_Indirect, "");                        /* no AFM file */
    ot_val(ot, OT_Spec1 + 3, 2);                                         /* metric: ascender */
    ot_val(ot, OT_Spec1 + 5, face);                                      /* face in the file */
    ot_data(ot, OT_AvailSizes, sizes, (ns + 1) * sizeof(UWORD));
    ot_val(ot, TAG_DONE, 0);
    {
        /* indirect values become offsets from the start of the file */
        ULONG head = ot->nt * 4;
        for (i = 0; i < ot->nt; i += 2)
            if ((ot->tags[i] & OT_Indirect) && ot->tags[i] != TAG_DONE) ot->tags[i + 1] += head;
        ot->tags[1] = head + ot->nd;
        if (write_file(otagname, ot->tags, head, ot->data, ot->nd)) {
            /* the .font: an outline font header without bitmap sizes */
            static const UWORD fch[2] = {OFCH_ID, 0};
            ok = write_file(fontname, fch, sizeof(fch), NULL, 0);
        }
    }
    FreeVec(ot);
    if (!ok) {
        Printf((STRPTR)"  cannot write the font in FONTS:\n");
        return 0;
    }

    /* open it as a program would, as a check */
    if (DiskfontBase && !o->nocheck) {
        struct TextAttr ta;
        struct TextFont *tf;
        char tn[140];
        sprintf(tn, "%s.font", name);
        ta.ta_Name = (STRPTR)tn;
        ta.ta_YSize = 14;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        if ((tf = OpenDiskFont(&ta)) != NULL) {
            Printf((STRPTR)"  ok: diskfont opens it (14 pixels: %ld x %ld%s)\n", (ULONG)tf->tf_XSize,
                   (ULONG)tf->tf_YSize, (ULONG)((tf->tf_Flags & FPF_PROPORTIONAL) ? ", proportional" : ""));
            CloseFont(tf);
        } else
            Printf((STRPTR)"  WARNING: diskfont cannot open it - is LIBS:%s.library installed?\n",
                   (ULONG)o->engine);
    }
    return 1;
}

static int font_suffix_ok(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && (!Stricmp(n + l - 4, ".ttf") || !Stricmp(n + l - 4, ".otf") || !Stricmp(n + l - 4, ".ttc"));
}

/* ---- METRICS: what diskfont makes of an installed font, and what AAText
 * will do with it ---------------------------------------------------------- */

/* top (yMax) of a TrueType glyph from glyf/loca, font units; -1 if unknown */
static LONG glyph_top(Font *f, UWORD g)
{
    ULONG lo, ll, go, gl, a, b;
    UBYTE h[54], e[8], hd[10];
    int longoff;
    if (!find_table(f, TAG('h', 'e', 'a', 'd'), &lo, &ll) || !read_at(f->fh, lo, h, 54)) return -1;
    longoff = be16(h + 50) != 0;
    if (!find_table(f, TAG('l', 'o', 'c', 'a'), &lo, &ll) || !find_table(f, TAG('g', 'l', 'y', 'f'), &go, &gl))
        return -1;
    if (longoff) {
        if (!read_at(f->fh, lo + g * 4, e, 8)) return -1;
        a = be32(e);
        b = be32(e + 4);
    } else {
        if (!read_at(f->fh, lo + g * 2, e, 4)) return -1;
        a = be16(e) * 2UL;
        b = be16(e + 2) * 2UL;
    }
    if (b <= a || !read_at(f->fh, go + a, hd, 10)) return 0;
    return (LONG)(WORD)be16(hd + 8);         /* numberOfContours, xMin, yMin, xMax, yMax */
}

/* first and last ink rows of a character in a bitmap font */
static int ink_rows(struct TextFont *tf, UBYTE c, int *top, int *bot)
{
    const UBYTE *data = tf->tf_CharData;
    ULONG loc;
    int idx, bit0, w, x, y;
    if (!data || !tf->tf_CharLoc || c < tf->tf_LoChar || c > tf->tf_HiChar) return 0;
    idx = c - tf->tf_LoChar;
    loc = ((ULONG *)tf->tf_CharLoc)[idx];
    bit0 = (int)(loc >> 16);
    w = (int)(loc & 0xFFFF);
    *top = -1;
    for (y = 0; y < tf->tf_YSize; y++) {
        const UBYTE *row = data + y * tf->tf_Modulo;
        for (x = 0; x < w; x++) {
            int b = bit0 + x;
            if (row[b >> 3] & (0x80 >> (b & 7))) {
                if (*top < 0) *top = y;
                *bot = y;
                break;
            }
        }
    }
    return *top >= 0;
}

static int metrics(const char *fontname)
{
    char name[120], otag[160], path[300];
    ULONG *tags = NULL;
    LONG len, i;
    ULONG face = 0;
    BPTR fh;
    Font f;
    UWORD gf, gi, gl;
    LONG topf = -1, topi = -1, topl = -1, adv;
    int size;
    sprintf(name, "%.110s", fontname);
    if (strlen(name) > 5 && !Stricmp(name + strlen(name) - 5, ".font")) name[strlen(name) - 5] = 0;
    if (font_suffix_ok(name)) {
        /* a font file: the name it is installed under, as install() makes it */
        if (!font_open(&f, fontname, 0) || !font_info(&f)) {
            font_close(&f);
            Printf((STRPTR)"%s: not a TrueType or OpenType font InstallTTF can read\n", (ULONG)fontname);
            return 0;
        }
        font_close(&f);
        if (f.style[0]) sprintf(name, "%.80s %.29s", f.family, f.style);
        else sprintf(name, "%.110s", f.family);
        clean_name(name);
    }
    sprintf(otag, "FONTS:%s.otag", name);
    path[0] = 0;
    if ((fh = Open((STRPTR)otag, MODE_OLDFILE)) != 0) {
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len > 8 && len < 100000 && (tags = AllocVec(len + 1, MEMF_CLEAR)) != NULL && Read(fh, tags, len) == len) {
            for (i = 0; (i + 1) * 8 <= len && tags[i * 2]; i++) {
                ULONG t = tags[i * 2], v = tags[i * 2 + 1];
                if (t == (OT_Spec1 | OT_Indirect) && v < (ULONG)len) sprintf(path, "%.299s", (char *)tags + v);
                if (t == OT_Spec1 + 5) face = v;
            }
        }
        Close(fh);
    }
    if (tags) FreeVec(tags);
    if (!path[0]) {
        Printf((STRPTR)"%s: no FONTS:%s.otag with a font file\n", (ULONG)fontname, (ULONG)name);
        return 0;
    }
    if (!font_open(&f, path, face) || !font_info(&f)) {
        font_close(&f);
        Printf((STRPTR)"%s: cannot read %s\n", (ULONG)fontname, (ULONG)path);
        return 0;
    }
    {
        ULONG cl;
        UBYTE *cm = load_table(&f, TAG('c', 'm', 'a', 'p'), &cl);
        gf = cm ? glyph_of(cm, cl, 'f') : 0;
        gi = cm ? glyph_of(cm, cl, 'i') : 0;
        gl = cm ? glyph_of(cm, cl, 'l') : 0;
        if (cm) FreeVec(cm);
        if (gf) topf = glyph_top(&f, gf);
        if (gi) topi = glyph_top(&f, gi);
        if (gl) topl = glyph_top(&f, gl);
    }
    adv = f.space_adv;
    font_close(&f);
    Printf((STRPTR)"%s (%s)\n  upem %ld, advance %ld, top of f/i/l %ld/%ld/%ld\n", (ULONG)name, (ULONG)path,
           (ULONG)f.upem, adv, topf, topi, topl);
    Printf((STRPTR)"  size: YSize XSize Baseline | bitmap ink rows f i l H | AAText px, rows f/i need, room\n");
    Flush(Output());
    for (size = 10; size <= 24; size++) {
        struct TextAttr ta;
        struct TextFont *tf;
        char tn[140];
        int t[4], b[4], k, px, need, room;
        static const char ch[4] = {'f', 'i', 'l', 'H'};
        sprintf(tn, "%s.font", name);
        ta.ta_Name = (STRPTR)tn;
        ta.ta_YSize = (UWORD)size;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        if (!DiskfontBase || !(tf = OpenDiskFont(&ta))) {
            Printf((STRPTR)"  %2ld: cannot open\n", (ULONG)size);
            continue;
        }
        Printf((STRPTR)"  %2ld: %3ld %3ld %3ld |", (ULONG)size, (ULONG)tf->tf_YSize, (ULONG)tf->tf_XSize,
               (ULONG)tf->tf_Baseline);
        for (k = 0; k < 4; k++) {
            if (ink_rows(tf, (UBYTE)ch[k], &t[k], &b[k])) Printf((STRPTR)" %ld-%ld", (ULONG)t[k], (ULONG)b[k]);
            else Printf((STRPTR)" -");
        }
        /* AAText (safe metrics): the pixel size whose advance fills the cell */
        px = adv > 0 ? (int)((tf->tf_XSize * (LONG)f.upem + adv / 2) / adv) : 0;
        need = topf > topi ? (int)((topf * px + f.upem - 1) / f.upem) : (int)((topi * px + f.upem - 1) / f.upem);
        room = tf->tf_Baseline + 1;
        Printf((STRPTR)" | %ld, %ld, %ld%s\n", (ULONG)px, (ULONG)need, (ULONG)room,
               (ULONG)(need > room ? "  <- cut off" : ""));
        Flush(Output());
        CloseFont(tf);
    }
    return 1;
}

int main(void)
{
    static const char tmpl[] = "FILES/M/A,NAME/K,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,COPY/S,REPLACE/S,INFO/S,NOCHECK/S,METRICS/S";
    LONG args[12];
    struct RDArgs *rd;
    struct Opts o;
    char **files;
    int rc = RETURN_OK;
    memset(args, 0, sizeof(args));
    if (!(rd = ReadArgs((STRPTR)tmpl, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)"InstallTTF");
        Printf((STRPTR)"usage: InstallTTF <font.ttf|.otf|.ttc>... [NAME=name] [FACE=n] [CHARSET=latin1]\n"
                       "                  [ENGINE=aatext|freetype2] [SIZES=8-16,18,20,24] [COPY [TO=dir]]\n"
                       "                  [REPLACE] [INFO] [NOCHECK]\n");
        return RETURN_FAIL;
    }
    Printf((STRPTR)"%s\n", (ULONG)(version + 6));
    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 36);
    memset(&o, 0, sizeof(o));
    o.name = (char *)args[1];
    if (args[2]) {
        o.face = *(LONG *)args[2];
        o.haveface = 1;
    }
    o.charset = (char *)args[3];
    o.engine = (char *)args[4];
    o.sizes = (char *)args[5];
    o.to = (char *)args[6];
    o.copy = args[7] != 0 || args[6] != 0;
    o.replace = args[8] != 0;
    o.info = args[9] != 0;
    o.nocheck = args[10] != 0;
    if (args[11]) {
        /* METRICS: FILES are installed font names */
        for (files = (char **)args[0]; *files; files++)
            if (!metrics(*files)) rc = RETURN_WARN;
        FreeArgs(rd);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        if (DiskfontBase) CloseLibrary(DiskfontBase);
        return rc;
    }
    if (!o.engine) {
        /* aatext.library (from AAText) if it is there, else freetype2.library */
        o.engine = exists("LIBS:aatext.library") || !exists("LIBS:freetype2.library") ? "aatext" : "freetype2";
        if (!exists("LIBS:aatext.library") && !exists("LIBS:freetype2.library"))
            Printf((STRPTR)"note: neither LIBS:aatext.library nor LIBS:freetype2.library is installed; "
                           "copy Libs/aatext.library from the AAText archive to LIBS:\n");
    }
    for (files = (char **)args[0]; *files && rc != RETURN_FAIL; files++) {
        /* patterns (#?.ttf) name several files */
        struct AnchorPath *ap = AllocVec(sizeof(struct AnchorPath) + 512, MEMF_CLEAR);
        LONG err;
        if (!ap) break;
        ap->ap_Strlen = 512;
        ap->ap_BreakBits = SIGBREAKF_CTRL_C;
        for (err = MatchFirst((STRPTR)*files, ap); !err; err = MatchNext(ap)) {
            ULONG face = o.haveface ? (ULONG)o.face : 0, n = 1;
            if (ap->ap_Info.fib_DirEntryType > 0) continue;
            /* every face of a collection, unless FACE picks one */
            do {
                if (!install((char *)ap->ap_Buf, face, &o, &n)) rc = RETURN_WARN;
            } while (!o.haveface && ++face < n);
        }
        MatchEnd(ap);
        FreeVec(ap);
        if (err == ERROR_BREAK) {
            PrintFault(ERROR_BREAK, NULL);
            rc = RETURN_FAIL;
        } else if (err != ERROR_NO_MORE_ENTRIES) {
            PrintFault(err, (STRPTR)*files);
            rc = RETURN_WARN;
        }
    }
    FreeArgs(rd);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    (void)version;
    return rc;
}
