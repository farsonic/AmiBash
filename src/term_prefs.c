/*
 * term_prefs.c - AmiBash Terminal profiles and their preferences window.
 *
 * A profile is a named set of terminal settings like iTerm's: the colours
 * (foreground, background, bold, cursor, selection and the 16 ANSI ones),
 * the font, the working directory and text sent to a new tab's shell.
 * They are kept in ENV:AmiBash/terminal.prefs (and ENVARC: when saved).
 *
 * The preferences window is a GadTools window with three pages
 * (General, Colors, Text); it runs alongside the terminal, which handles
 * its messages from its own loop, so shells keep running meanwhile.
 * Changes show at once (like iTerm); Cancel puts everything back.
 */
#ifdef AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sh.h"
#include "term.h"
#include "newostheme.h"

extern struct Library *GadToolsBase, *AslBase, *UtilityBase;

Profile *profiles;
char default_profile[32];
int win_cols, win_rows, win_left = -1, win_top = -1;
static int next_id = 1;

const char *const color_names[NCOLORS] = {
    "Foreground", "Background", "Bold", "Cursor", "Cursor text", "Selection", "Selected text",
    "Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White",
    "Bright black", "Bright red", "Bright green", "Bright yellow", "Bright blue", "Bright magenta",
    "Bright cyan", "Bright white"};

/* ---- presets ------------------------------------------------------------ */

static const struct {
    const char *name;
    ULONG rgb[NCOLORS];
} presets[] = {
    {"AmiBash", {0xC7C7C7, 0x000000, 0xFFFFFF, 0xC7C7C7, 0x000000, 0xB5D5FF, 0x000000,
                 0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x3B78FF, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
                 0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x5C9DFF, 0xD670D6, 0x29B8DB, 0xFFFFFF}},
    {"iTerm2", {0xC7C7C7, 0x000000, 0xFFFFFF, 0xC7C7C7, 0xFFFFFF, 0xB5D5FF, 0x000000,
                0x000000, 0xC91B00, 0x00C200, 0xC7C400, 0x0225C7, 0xC930C7, 0x00C5C7, 0xC7C7C7,
                0x676767, 0xFF6D67, 0x5FF967, 0xFEFB67, 0x6871FF, 0xFF76FF, 0x5FFDFF, 0xFEFFFF}},
    {"Workbench", {0x000000, 0xAAAAAA, 0xFFFFFF, 0x6688BB, 0xFFFFFF, 0x6688BB, 0xFFFFFF,
                   0x000000, 0xAA0000, 0x006600, 0x886600, 0x0000AA, 0x880088, 0x006688, 0xDDDDDD,
                   0x555555, 0xDD3333, 0x229922, 0xBB9900, 0x3355DD, 0xBB33BB, 0x2299BB, 0xFFFFFF}},
    {"Solarized Dark", {0x839496, 0x002B36, 0x93A1A1, 0x93A1A1, 0x002B36, 0x073642, 0x93A1A1,
                        0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
                        0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
    {"Solarized Light", {0x657B83, 0xFDF6E3, 0x586E75, 0x586E75, 0xFDF6E3, 0xEEE8D5, 0x586E75,
                         0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
                         0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
    {"Green screen", {0x33FF33, 0x000000, 0x88FF88, 0x33FF33, 0x000000, 0x116611, 0x88FF88,
                      0x000000, 0x22AA22, 0x33FF33, 0x66CC33, 0x22AA22, 0x33CC66, 0x33FF99, 0x33FF33,
                      0x116611, 0x44CC44, 0x66FF66, 0x99FF66, 0x44CC44, 0x66FF99, 0x99FFCC, 0xCCFFCC}},
};
#define NPRESETS ((int)(sizeof(presets) / sizeof(presets[0])))

int preset_count(void) { return NPRESETS; }
const char *preset_name(int i) { return presets[i].name; }

void preset_apply(int i, Profile *p)
{
    if (i >= 0 && i < NPRESETS) memcpy(p->rgb, presets[i].rgb, sizeof(p->rgb));
}

/* ---- colour schemes ---------------------------------------------------------
 * The Presets page lists the built-in presets, the schemes imported from
 * iTerm2 .itermcolors files (MYSCHEMES) and the iTerm2-Color-Schemes
 * gallery (SCHEMES, made by tools/itermcolors.py, renewed by
 * "amibash-update SCHEMES").  A line of either file:
 *   NAME<TAB>RRGGBB RRGGBB ...      (NCOLORS colours, in the order of term.h)
 */
static char *read_whole(const char *name);

#define SCHEMES   "ENVARC:AmiBash/colorschemes"
#define MYSCHEMES "ENVARC:AmiBash/myschemes"

typedef struct Scheme {
    char name[40];
    ULONG rgb[NCOLORS];
} Scheme;

static Scheme *schemes;
static int nschemes, maxschemes;

static void scheme_add(const char *name, const ULONG *rgb)
{
    if (nschemes == maxschemes) {
        int m = maxschemes ? maxschemes * 2 : 64;
        Scheme *n = AllocVec(m * sizeof(Scheme), MEMF_ANY);
        if (!n) return;
        if (schemes) {
            memcpy(n, schemes, nschemes * sizeof(Scheme));
            FreeVec(schemes);
        }
        schemes = n;
        maxschemes = m;
    }
    sprintf(schemes[nschemes].name, "%.39s", name);
    memcpy(schemes[nschemes].rgb, rgb, sizeof(schemes[0].rgb));
    nschemes++;
}

static void schemes_read(const char *file)
{
    char *b = read_whole(file), *l, *e, *tab;
    ULONG rgb[NCOLORS];
    int i;
    for (l = b; l && *l; l = e) {
        for (e = l; *e && *e != '\n'; e++) ;
        if (*e) *e++ = 0;
        if (!(tab = strchr(l, '\t'))) continue;
        *tab++ = 0;
        for (i = 0; i < NCOLORS && *tab; i++) rgb[i] = strtoul(tab, &tab, 16) & 0xFFFFFF;
        if (i == NCOLORS) scheme_add(l, rgb);
    }
    if (b) FreeVec(b);
}

static void schemes_load(void)
{
    int i;
    if (schemes) return;
    for (i = 0; i < NPRESETS; i++) scheme_add(presets[i].name, presets[i].rgb);
    schemes_read(MYSCHEMES);
    schemes_read(SCHEMES);
}

static void schemes_free(void)
{
    if (schemes) FreeVec(schemes);
    schemes = NULL;
    nschemes = maxschemes = 0;
}

/* a number from a plist <real>: 0.2, 1, 6.2e-05 (no strtod in the pure
 * build's library) as 0..255 */
static int plist_real255(const char *s)
{
    long ip = 0, fp = 0, div = 1;
    int ex = 0, neg = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++)
            if (div < 100000000L) {
                fp = fp * 10 + (*s - '0');
                div *= 10;
            }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') ex = ex * 10 + (*s++ - '0');
    }
    if (neg && ex > 0) return 0;          /* tiny */
    if (ip >= 1 || (!neg && ex > 0)) return 255;
    return (int)((fp * 255 + div / 2) / div);
}

/* read an iTerm2 .itermcolors file (an XML property list) into rgb, which
 * keeps the colours the file does not have; the number of colours read */
int scheme_import(const char *file, ULONG *rgb)
{
    static const char *const keys[NCOLORS] = {
        "Foreground Color", "Background Color", "Bold Color", "Cursor Color", "Cursor Text Color",
        "Selection Color", "Selected Text Color"};
    char *b = read_whole(file), *p, *k, *ke, *d, *de;
    int n = 0;
    if (!b) return 0;
    for (p = b; (k = strstr(p, "<key>")) != NULL; p = ke) {
        int which = -1, i, c[3] = {0, 0, 0};
        char name[40];
        k += 5;
        if (!(ke = strstr(k, "</key>"))) break;
        sprintf(name, "%.*s", (int)(ke - k) < 39 ? (int)(ke - k) : 39, k);
        for (i = 0; i < C_ANSI; i++)
            if (!strcmp(name, keys[i])) which = i;
        if (!strncmp(name, "Ansi ", 5)) {
            /* exactly "Ansi N Color": not iTerm 3.5's "... (Dark)" ones */
            char want[24];
            int a = atoi(name + 5);
            sprintf(want, "Ansi %d Color", a);
            if (a >= 0 && a < 16 && !strcmp(name, want)) which = C_ANSI + a;
        }
        if (which < 0) continue;
        /* its <dict>: Red/Green/Blue Component reals */
        if (!(d = strstr(ke, "<dict>")) || !(de = strstr(d, "</dict>"))) break;
        for (i = 0; i < 3; i++) {
            static const char *const comp[3] = {"Red Component", "Green Component", "Blue Component"};
            char *q = strstr(d, comp[i]), *r;
            if (q && q < de && (r = strstr(q, "<real>")) != NULL && r < de) c[i] = plist_real255(r + 6);
            else if (q && q < de && (r = strstr(q, "<integer>")) != NULL && r < de) c[i] = atoi(r + 9) ? 255 : 0;
        }
        rgb[which] = ((ULONG)c[0] << 16) | ((ULONG)c[1] << 8) | (ULONG)c[2];
        n++;
        ke = de;
    }
    FreeVec(b);
    return n;
}

/* ---- the profiles --------------------------------------------------------- */

static void prof_defaults(Profile *p, const char *name)
{
    memset(p, 0, sizeof(*p));
    p->id = next_id++;
    sprintf(p->name, "%.31s", name);
    preset_apply(0, p);
    p->brighten = 1;
    p->boldcolor = 1;
    p->copysel = 1;
    p->dirmode = DIR_HOME;
    p->transparent = 1;                  /* NewOS glass whenever it is there */
}

Profile *prof_find(const char *name)
{
    Profile *p;
    for (p = profiles; p; p = p->next)
        if (Stricmp((STRPTR)p->name, (STRPTR)name) == 0) return p;
    return NULL;
}

Profile *prof_by_id(int id)
{
    Profile *p;
    for (p = profiles; p; p = p->next)
        if (p->id == id) return p;
    return NULL;
}

Profile *prof_new(const char *name, const Profile *copy)
{
    Profile *p = AllocVec(sizeof(Profile), MEMF_CLEAR), **pp;
    char n[32];
    int k = 2;
    if (!p) return NULL;
    /* a name not taken yet: "Name", "Name 2", ... */
    sprintf(n, "%.31s", name);
    while (prof_find(n)) sprintf(n, "%.26s %d", name, k++);
    if (copy) {
        *p = *copy;
        p->id = next_id++;
        strcpy(p->name, n);
    } else
        prof_defaults(p, n);
    p->next = NULL;
    for (pp = &profiles; *pp; pp = &(*pp)->next) ;
    *pp = p;
    return p;
}

void prof_delete(Profile *p)
{
    Profile **pp;
    for (pp = &profiles; *pp; pp = &(*pp)->next)
        if (*pp == p) {
            *pp = p->next;
            FreeVec(p);
            break;
        }
    if (!prof_find(default_profile)) strcpy(default_profile, profiles ? profiles->name : "Default");
}

void prof_free_all(void)
{
    while (profiles) {
        Profile *p = profiles;
        profiles = p->next;
        FreeVec(p);
    }
}

Profile *prof_default(void)
{
    Profile *p = prof_find(default_profile);
    if (!p) p = profiles;
    if (!p) {
        p = prof_new("Default", NULL);
        if (p) strcpy(default_profile, p->name);
    }
    return p;
}

/* ---- the file ---------------------------------------------------------------
 *   default NAME
 *   window COLUMNS ROWS LEFT TOP     (0, 0, -1, -1: automatic)
 *   profile NAME
 *   color N RRGGBB          (N: 0 .. NCOLORS-1, see enum in term.h)
 *   brighten 0|1  boldcolor 0|1  copysel 0|1  scrollback N  dirmode N
 *   shell 0|1 (bash, Amiga Shell)  transparent 0|1
 *   extras 1      (the Transparent and Amiga Shell profiles were added)
 *   font NAME/SIZE  dir PATH  cmd TEXT                     (to end of line)
 */

static char *read_whole(const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    LONG len;
    char *b = NULL;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len >= 0 && len < 200000 && (b = AllocVec(len + 1, MEMF_ANY)) != NULL) {
        if (Read(fh, b, len) != len) {
            FreeVec(b);
            b = NULL;
        } else
            b[len] = 0;
    }
    Close(fh);
    return b;
}

static int extras;                   /* see "extras" in the file */

void prof_load(void)
{
    char *b = read_whole("ENV:AmiBash/terminal.prefs"), *l, *e, *v;
    Profile *cur = NULL;
    if (!b) b = read_whole("ENVARC:AmiBash/terminal.prefs");
    prof_free_all();
    default_profile[0] = 0;
    win_cols = win_rows = 0;
    win_left = win_top = -1;
    for (l = b; l && *l; l = e) {
        for (e = l; *e && *e != '\n'; e++) ;
        if (*e) *e++ = 0;
        for (v = l; *v && *v != ' '; v++) ;
        if (*v) *v++ = 0;
        if (!strcmp(l, "default")) sprintf(default_profile, "%.31s", v);
        else if (!strcmp(l, "extras")) extras = atoi(v);
        else if (!strcmp(l, "window")) {
            char *e2;
            win_cols = (int)strtol(v, &e2, 10);
            win_rows = (int)strtol(e2, &e2, 10);
            if (*e2) win_left = (int)strtol(e2, &e2, 10);
            if (*e2) win_top = (int)strtol(e2, &e2, 10);
        }
        else if (!strcmp(l, "profile")) cur = prof_new(v, NULL);
        else if (!cur) continue;
        else if (!strcmp(l, "color")) {
            int n = atoi(v);
            char *h = strchr(v, ' ');
            if (n >= 0 && n < NCOLORS && h) cur->rgb[n] = strtoul(h + 1, NULL, 16) & 0xFFFFFF;
        } else if (!strcmp(l, "brighten")) cur->brighten = atoi(v);
        else if (!strcmp(l, "boldcolor")) cur->boldcolor = atoi(v);
        else if (!strcmp(l, "copysel")) cur->copysel = atoi(v);
        else if (!strcmp(l, "scrollback")) cur->scrollback = atoi(v);
        else if (!strcmp(l, "shell")) cur->shell = atoi(v) ? SHELL_AMIGA : SHELL_BASH;
        else if (!strcmp(l, "transparent")) cur->transparent = atoi(v) != 0;
        else if (!strcmp(l, "dirmode")) cur->dirmode = atoi(v);
        else if (!strcmp(l, "font")) sprintf(cur->font, "%.63s", v);
        else if (!strcmp(l, "dir")) sprintf(cur->dir, "%.159s", v);
        else if (!strcmp(l, "cmd")) sprintf(cur->cmd, "%.159s", v);
    }
    if (b) FreeVec(b);
    prof_default();
    if (!extras) {
        /* once, also into older prefs files: a ready-made profile for
         * the Amiga Shell */
        Profile *p;
        if (!prof_find("Amiga Shell") && (p = prof_new("Amiga Shell", NULL)) != NULL) p->shell = SHELL_AMIGA;
        extras = 1;
    }
}

static int write_prefs(const char *dir, const char *file)
{
    BPTR fh, l;
    Profile *p;
    int i;
    char line[300];
    if ((l = Lock((STRPTR)dir, SHARED_LOCK)) != 0) UnLock(l);
    else if ((l = CreateDir((STRPTR)dir)) != 0) UnLock(l);
    else return -1;
    if (!(fh = Open((STRPTR)file, MODE_NEWFILE))) return -1;
    sprintf(line, "default %s\nwindow %d %d %d %d\nextras %d\n", default_profile, win_cols, win_rows, win_left,
            win_top, extras);
    FPuts(fh, (STRPTR)line);
    for (p = profiles; p; p = p->next) {
        sprintf(line, "profile %s\n", p->name);
        FPuts(fh, (STRPTR)line);
        for (i = 0; i < NCOLORS; i++) {
            sprintf(line, "color %d %06lX\n", i, (unsigned long)p->rgb[i]);
            FPuts(fh, (STRPTR)line);
        }
        sprintf(line, "brighten %d\nboldcolor %d\ncopysel %d\nscrollback %d\ndirmode %d\nshell %d\n"
                "transparent %d\nfont %s\ndir %s\ncmd %s\n", p->brighten, p->boldcolor, p->copysel,
                p->scrollback, p->dirmode, p->shell, p->transparent, p->font, p->dir, p->cmd);
        FPuts(fh, (STRPTR)line);
    }
    Close(fh);
    return 0;
}

int prof_save(void)
{
    int rc = write_prefs("ENV:AmiBash", "ENV:AmiBash/terminal.prefs");
    BPTR l = Lock((STRPTR)"ENVARC:", SHARED_LOCK);
    if (l) {
        UnLock(l);
        if (write_prefs("ENVARC:AmiBash", "ENVARC:AmiBash/terminal.prefs")) rc = -1;
    }
    return rc;
}

/* ---- the preferences window ------------------------------------------------ */

enum {
    G_PROFILES = 1, G_NEW, G_COPY, G_DELETE, G_DEFAULT, G_PAGE,
    G_NAME, G_CMD, G_DIRMODE, G_DIR, G_DIRREQ, G_SCROLLBACK, G_COPYSEL, G_SHELL,
    G_R, G_G, G_B, G_BRIGHTEN, G_BOLDCOLOR, G_TRANSP,
    G_SCHEMES, G_SAPPLY, G_SIMPORT, G_SONLINE,
    G_SW,                                /* NCOLORS colour swatches from here */
    G_SW_END = G_SW + NCOLORS - 1,
    G_FONTNAME, G_FONTREQ, G_FONTAUTO, G_COLS, G_ROWS, G_LEFT, G_TOP, G_CURSIZE,
    G_SAVE, G_USE, G_CANCEL, G_MAX
};

typedef struct Prefs {
    struct Window *win;
    struct Screen *scr;
    APTR vi;
    struct Gadget *glist, *g[G_MAX];
    int page, ci, preset;
    Profile *sel;
    void (*apply)(Profile *);
    void (*changed)(void);
    /* the profile list for the list view */
    struct List plist;
    struct Node *pnodes;
    char (*pnames)[40];
    /* the scheme list view */
    struct List slist;
    struct Node *snodes;
    char scount[32];
    /* undo for Cancel */
    Profile *saved;
    char saved_default[32];
    int saved_cols, saved_rows, saved_left, saved_top;
    /* the Colors page: swatch rows, their size, label widths, where the
     * ANSI column and the "editing" line are; pens for the swatches */
    WORD rh, sww, llab, alab, cy0, ax, ey;
    LONG swpens[NCOLORS];
    /* the Presets page: list height, the preview box */
    WORD listh, px, py, pw_, ph;
    int fh;
    WORD left, top, width;               /* right-hand page area */
    WORD lab, lw;                        /* label column, profile list */
    char fontlabel[80];
} Prefs;

static Prefs *pw;

static STRPTR page_labels[] = {(STRPTR)"General", (STRPTR)"Colors", (STRPTR)"Presets", (STRPTR)"Text", NULL};

/* the left column of the Colors page (-1: the "Cursor Colors" heading) */
static const int left_colors[8] = {C_FG, C_BG, C_BOLD, C_SEL, C_SELTEXT, -1, C_CURSOR, C_CURTEXT};
static const char *const ansi_names[8] = {"Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White"};

/* a GENERIC_KIND gadget as a swatch: a box we draw, clicked */
static void swatch_gadget(struct Gadget *g)
{
    g->Flags |= GFLG_GADGHNONE;
    g->Activation |= GACT_RELVERIFY;
    g->GadgetType |= GTYP_BOOLGADGET;
}

static void free_slist(void)
{
    if (pw->snodes) FreeVec(pw->snodes);
    pw->snodes = NULL;
    NewList(&pw->slist);
}

static void build_slist(void)
{
    int i;
    schemes_load();
    free_slist();
    if (!nschemes || !(pw->snodes = AllocVec(nschemes * sizeof(struct Node), MEMF_CLEAR))) return;
    for (i = 0; i < nschemes; i++) {
        pw->snodes[i].ln_Name = schemes[i].name;
        AddTail(&pw->slist, &pw->snodes[i]);
    }
    if (pw->preset >= nschemes) pw->preset = 0;
}
static STRPTR shell_labels[] = {(STRPTR)"bash", (STRPTR)"Amiga Shell", NULL};
static STRPTR dir_labels[] = {(STRPTR)"Home directory", (STRPTR)"Previous tab's", (STRPTR)"This directory:", NULL};

static void free_plist(void)
{
    if (pw->pnodes) FreeVec(pw->pnodes);
    if (pw->pnames) FreeVec(pw->pnames);
    pw->pnodes = NULL;
    pw->pnames = NULL;
}

/* the profile list view's nodes: "Name", the default marked with a * */
static int build_plist(void)
{
    Profile *p;
    int n = 0, i = 0;
    free_plist();
    NewList(&pw->plist);
    for (p = profiles; p; p = p->next) n++;
    pw->pnodes = AllocVec((n + 1) * sizeof(struct Node), MEMF_CLEAR);
    pw->pnames = AllocVec((n + 1) * 40, MEMF_CLEAR);
    if (!pw->pnodes || !pw->pnames) return 0;
    for (p = profiles; p; p = p->next, i++) {
        sprintf(pw->pnames[i], "%s %s", Stricmp((STRPTR)p->name, (STRPTR)default_profile) ? " " : "*", p->name);
        pw->pnodes[i].ln_Name = (char *)pw->pnames[i];
        AddTail(&pw->plist, &pw->pnodes[i]);
    }
    return 1;
}

static int sel_index(void)
{
    Profile *p;
    int i = 0;
    for (p = profiles; p; p = p->next, i++)
        if (p == pw->sel) return i;
    return 0;
}

static struct Gadget *mk(ULONG kind, struct Gadget *prev, struct NewGadget *ng, int id, int x, int y, int w, int h,
                         const char *label, ULONG t1, ULONG d1, ULONG t2, ULONG d2, ULONG t3, ULONG d3)
{
    struct TagItem t[5];
    struct Gadget *g;
    if (!prev) return NULL;
    ng->ng_LeftEdge = (WORD)x;
    ng->ng_TopEdge = (WORD)y;
    ng->ng_Width = (WORD)w;
    ng->ng_Height = (WORD)h;
    ng->ng_GadgetText = (UBYTE *)label;
    ng->ng_GadgetID = id;
    ng->ng_Flags = (kind == BUTTON_KIND) ? PLACETEXT_IN : (kind == CHECKBOX_KIND ? PLACETEXT_RIGHT : PLACETEXT_LEFT);
    if (kind == LISTVIEW_KIND && label) ng->ng_Flags = PLACETEXT_ABOVE;
    t[0].ti_Tag = t1; t[0].ti_Data = d1;
    t[1].ti_Tag = t2; t[1].ti_Data = d2;
    t[2].ti_Tag = t3; t[2].ti_Data = d3;
    t[3].ti_Tag = GT_Underscore; t[3].ti_Data = '_';
    t[4].ti_Tag = TAG_END;
    g = CreateGadgetA(kind, prev, ng, t);
    pw->g[id] = g;
    return g;
}

/* width of a text in the screen's font */
static int tl(const char *s)
{
    return TextLength(&pw->scr->RastPort, (STRPTR)s, (LONG)strlen(s));
}

/* the widest of n texts (n < 0: up to a NULL) */
static int maxw(const char *const *v, int n)
{
    int m = 0, w;
    for (; n < 0 ? *v != NULL : n-- > 0; v++)
        if ((w = tl(*v)) > m) m = w;
    return m;
}

static void set_font_label(Profile *p)
{
    if (p->font[0]) sprintf(pw->fontlabel, "%.70s", p->font);
    else strcpy(pw->fontlabel, "Automatic");
}

/* all gadgets for the current page */
static struct Gadget *build_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    struct Screen *s = pw->scr;
    int fh = pw->fh, bh = fh + 6, x0 = s->WBorLeft + 8, y0 = s->WBorTop + fh + 1 + 6;
    int lw = pw->lw, bw = (lw - 4) / 2, rx = x0 + lw + 12, rw = pw->width;
    int lab = pw->lab, y;
    Profile *p = pw->sel;
    ULONG c = p->rgb[pw->ci];
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = s->Font;
    ng.ng_VisualInfo = pw->vi;
    pw->glist = NULL;
    memset(pw->g, 0, sizeof(pw->g));
    g = CreateContext(&pw->glist);
    /* left: the profiles */
    g = mk(LISTVIEW_KIND, g, &ng, G_PROFILES, x0, y0 + fh + 4, lw, bh * 9, "Profiles", GTLV_Labels,
           (ULONG)&pw->plist, GTLV_ShowSelected, 0, GTLV_Selected, (ULONG)sel_index());
    y = y0 + fh + 4 + bh * 9 + 4;
    g = mk(BUTTON_KIND, g, &ng, G_NEW, x0, y, bw, bh, "_New", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
    g = mk(BUTTON_KIND, g, &ng, G_COPY, x0 + bw + 4, y, bw, bh, "Cop_y", TAG_IGNORE, 0, TAG_IGNORE, 0,
           TAG_IGNORE, 0);
    y += bh + 2;
    g = mk(BUTTON_KIND, g, &ng, G_DELETE, x0, y, bw, bh, "_Delete", GA_Disabled, profiles && !profiles->next,
           TAG_IGNORE, 0, TAG_IGNORE, 0);
    g = mk(BUTTON_KIND, g, &ng, G_DEFAULT, x0 + bw + 4, y, bw, bh, "Default", TAG_IGNORE, 0, TAG_IGNORE, 0,
           TAG_IGNORE, 0);
    /* right: the page */
    g = mk(CYCLE_KIND, g, &ng, G_PAGE, rx + lab, y0, rw - lab, bh, "_Page", GTCY_Labels, (ULONG)page_labels,
           GTCY_Active, (ULONG)pw->page, TAG_IGNORE, 0);
    y = y0 + bh + 10;
    pw->left = (WORD)rx;
    pw->top = (WORD)y;
    if (pw->page == 0) {
        g = mk(STRING_KIND, g, &ng, G_NAME, rx + lab, y, rw - lab, bh, "Name", GTST_String, (ULONG)p->name,
               GTST_MaxChars, 31, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(CYCLE_KIND, g, &ng, G_SHELL, rx + lab, y, rw - lab, bh, "Shell", GTCY_Labels,
               (ULONG)shell_labels, GTCY_Active, (ULONG)p->shell, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(STRING_KIND, g, &ng, G_CMD, rx + lab, y, rw - lab, bh, "Send at start", GTST_String, (ULONG)p->cmd,
               GTST_MaxChars, 159, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(CYCLE_KIND, g, &ng, G_DIRMODE, rx + lab, y, rw - lab, bh, "Directory", GTCY_Labels,
               (ULONG)dir_labels, GTCY_Active, (ULONG)p->dirmode, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(STRING_KIND, g, &ng, G_DIR, rx + lab, y, rw - lab - bh * 2, bh, NULL, GTST_String, (ULONG)p->dir,
               GTST_MaxChars, 159, GA_Disabled, p->dirmode != DIR_FIXED);
        g = mk(BUTTON_KIND, g, &ng, G_DIRREQ, rx + rw - bh * 2 + 4, y, bh * 2 - 4, bh, "...", GA_Disabled,
               p->dirmode != DIR_FIXED, TAG_IGNORE, 0, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(INTEGER_KIND, g, &ng, G_SCROLLBACK, rx + lab, y, tl("000000") + 16, bh, "Scrollback lines",
               GTIN_Number, (ULONG)(p->scrollback ? p->scrollback : 300), GTIN_MaxChars, 6, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(CHECKBOX_KIND, g, &ng, G_COPYSEL, rx, y, 26, fh + 4, "Copy text when selected",
               GTCB_Checked, (ULONG)p->copysel, GTCB_Scaled, TRUE, TAG_IGNORE, 0);
    } else if (pw->page == 1) {
        /* like iTerm2: Basic and Cursor colours on the left, the ANSI ones
         * (normal, bright) on the right, a swatch each; the swatch picked
         * is edited with the sliders below */
        int rh = pw->rh, sww = pw->sww, i, ax, sx;
        pw->cy0 = (WORD)y;
        for (i = 0; i < 8; i++)
            if (left_colors[i] >= 0) {
                struct Gadget *sg = mk(GENERIC_KIND, g, &ng, G_SW + left_colors[i], rx + pw->llab + 6,
                                       y + (i + 1) * rh, sww, rh - 2, NULL, TAG_IGNORE, 0, TAG_IGNORE, 0,
                                       TAG_IGNORE, 0);
                if (sg) swatch_gadget(sg);
                g = sg;
            }
        ax = rx + rw - (pw->alab + 6 + 2 * sww + 6);
        pw->ax = (WORD)ax;
        for (i = 0; i < 16; i++) {
            struct Gadget *sg = mk(GENERIC_KIND, g, &ng, G_SW + C_ANSI + i, ax + pw->alab + 6 + (i / 8) * (sww + 6),
                                   y + (i % 8 + 2) * rh, sww, rh - 2, NULL, TAG_IGNORE, 0, TAG_IGNORE, 0,
                                   TAG_IGNORE, 0);
            if (sg) swatch_gadget(sg);
            g = sg;
        }
        y += 10 * rh + 2;
        pw->ey = (WORD)y;                   /* "Foreground  #RRGGBB" */
        y += fh + 4;
        sx = rx + tl("R") + 8;
        g = mk(SLIDER_KIND, g, &ng, G_R, sx, y, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 4, "R",
               GTSL_Max, 255, GTSL_Level, (c >> 16) & 255, GA_RelVerify, TRUE);
        y += bh - 2;
        g = mk(SLIDER_KIND, g, &ng, G_G, sx, y, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 4, "G",
               GTSL_Max, 255, GTSL_Level, (c >> 8) & 255, GA_RelVerify, TRUE);
        y += bh - 2;
        g = mk(SLIDER_KIND, g, &ng, G_B, sx, y, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 4, "B",
               GTSL_Max, 255, GTSL_Level, c & 255, GA_RelVerify, TRUE);
        y += bh + 2;
        g = mk(CHECKBOX_KIND, g, &ng, G_BRIGHTEN, rx, y, 26, fh + 4, "Brighten bold text", GTCB_Checked,
               (ULONG)p->brighten, GTCB_Scaled, TRUE, TAG_IGNORE, 0);
        /* NewOS glass: offered only while NewOS has its transparency on */
        g = mk(CHECKBOX_KIND, g, &ng, G_TRANSP, rx + rw / 2, y, 26, fh + 4, "Transparent background",
               GTCB_Checked, (ULONG)p->transparent, GTCB_Scaled, TRUE, GA_Disabled,
               NT_GlassMode() == NTM_OFF);
        y += fh + 6;
        g = mk(CHECKBOX_KIND, g, &ng, G_BOLDCOLOR, rx, y, 26, fh + 4, "Bold text in the Bold colour",
               GTCB_Checked, (ULONG)p->boldcolor, GTCB_Scaled, TRUE, TAG_IGNORE, 0);
    } else if (pw->page == 2) {
        /* the colour schemes: the built-in ones, imported ones and the
         * iTerm2-Color-Schemes gallery; a preview of the one picked */
        int lw = rw * 3 / 5, bw3 = (rw - 8) / 3;
        build_slist();
        g = mk(LISTVIEW_KIND, g, &ng, G_SCHEMES, rx, y, lw, pw->listh, NULL, GTLV_Labels, (ULONG)&pw->slist,
               GTLV_ShowSelected, 0, GTLV_Selected, (ULONG)pw->preset);
        if (g) GT_SetGadgetAttrs(g, NULL, NULL, GTLV_MakeVisible, (ULONG)pw->preset, TAG_END);
        pw->px = (WORD)(rx + lw + 8);
        pw->py = (WORD)y;
        pw->pw_ = (WORD)(rw - lw - 8);
        pw->ph = (WORD)pw->listh;
        y += pw->listh + 4;
        g = mk(BUTTON_KIND, g, &ng, G_SAPPLY, rx, y, bw3, bh, "_Apply", TAG_IGNORE, 0, TAG_IGNORE, 0,
               TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_SIMPORT, rx + bw3 + 4, y, bw3, bh, "_Import...", TAG_IGNORE, 0,
               TAG_IGNORE, 0, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_SONLINE, rx + 2 * (bw3 + 4), y, bw3, bh, "_Online...", TAG_IGNORE, 0,
               TAG_IGNORE, 0, TAG_IGNORE, 0);
        y += bh + 4;
        sprintf(pw->scount, "%d schemes", nschemes);
        g = mk(TEXT_KIND, g, &ng, 0, rx, y, rw, bh, NULL, GTTX_Text, (ULONG)pw->scount, TAG_IGNORE, 0,
               TAG_IGNORE, 0);
    } else {
        set_font_label(p);
        g = mk(TEXT_KIND, g, &ng, G_FONTNAME, rx + lab, y, rw - lab, bh, "Font", GTTX_Text, (ULONG)pw->fontlabel,
               GTTX_Border, TRUE, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(BUTTON_KIND, g, &ng, G_FONTREQ, rx + lab, y, (rw - lab) / 2 - 2, bh, "_Choose...", TAG_IGNORE, 0,
               TAG_IGNORE, 0, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_FONTAUTO, rx + lab + (rw - lab) / 2 + 2, y, (rw - lab) / 2 - 2, bh,
               "_Automatic", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
        /* the window, for all profiles: size and place (0 columns or rows:
         * the default size; -1 left or top: the default place) */
        {
            int nw = tl("00000") + 16;
            y += bh + 14;
            g = mk(INTEGER_KIND, g, &ng, G_COLS, rx + lab, y, nw, bh, "Window columns",
                   GTIN_Number, (ULONG)win_cols, GTIN_MaxChars, 3, TAG_IGNORE, 0);
            g = mk(BUTTON_KIND, g, &ng, G_CURSIZE, rx + lab + nw + 8, y, rw - lab - nw - 8, bh,
                   "Use current wi_ndow", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
            y += bh + 4;
            g = mk(INTEGER_KIND, g, &ng, G_ROWS, rx + lab, y, nw, bh, "Window rows",
                   GTIN_Number, (ULONG)win_rows, GTIN_MaxChars, 3, TAG_IGNORE, 0);
            y += bh + 4;
            g = mk(INTEGER_KIND, g, &ng, G_LEFT, rx + lab, y, nw, bh, "Window left",
                   GTIN_Number, (ULONG)win_left, GTIN_MaxChars, 5, TAG_IGNORE, 0);
            y += bh + 4;
            g = mk(INTEGER_KIND, g, &ng, G_TOP, rx + lab, y, nw, bh, "Window top",
                   GTIN_Number, (ULONG)win_top, GTIN_MaxChars, 5, TAG_IGNORE, 0);
            y += bh + 4;
            g = mk(TEXT_KIND, g, &ng, 0, rx + lab, y, rw - lab, bh, NULL, GTTX_Text,
                   (ULONG)"Size 0, place -1: the default", TAG_IGNORE, 0, TAG_IGNORE, 0);
        }
    }
    /* bottom */
    y = pw->win ? pw->win->Height - s->WBorBottom - bh - 6 : 0;
    if (pw->win) {
        int w3 = (pw->win->Width - s->WBorLeft - s->WBorRight - 32) / 3;
        g = mk(BUTTON_KIND, g, &ng, G_SAVE, x0, y, w3, bh, "_Save", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_USE, x0 + w3 + 8, y, w3, bh, "_Use", TAG_IGNORE, 0, TAG_IGNORE, 0,
               TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_CANCEL, x0 + 2 * (w3 + 8), y, w3, bh, "Cancel", TAG_IGNORE, 0, TAG_IGNORE,
               0, TAG_IGNORE, 0);
    }
    return g;
}

static void release_swpens(void)
{
    int i;
    for (i = 0; i < NCOLORS; i++) {
        if (pw->swpens[i] >= 0) ReleasePen(pw->scr->ViewPort.ColorMap, pw->swpens[i]);
        pw->swpens[i] = -1;
    }
}

/* the best pen for colour c, kept in slot i until the next drawing */
static LONG swpen(int i, ULONG c)
{
    LONG pen = ObtainBestPenA(pw->scr->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL,
                              ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL, NULL);
    pw->swpens[i] = pen;
    return pen >= 0 ? pen : 1;
}

static void text_at(int x, int y, const char *s, LONG pen)
{
    struct RastPort *rp = pw->win->RPort;
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, (LONG)strlen(s));
}

static void bevel(int x, int y, int w, int h, int recessed)
{
    struct TagItem t[3];
    t[0].ti_Tag = GT_VisualInfo;
    t[0].ti_Data = (ULONG)pw->vi;
    t[1].ti_Tag = recessed ? GTBB_Recessed : TAG_IGNORE;
    t[1].ti_Data = TRUE;
    t[2].ti_Tag = TAG_END;
    DrawBevelBoxA(pw->win->RPort, x, y, w, h, t);
}

/* colour swatch i (a colour of the profile) where its gadget is; the one
 * being edited framed */
static void draw_sw(int i)
{
    struct Gadget *g = pw->g[G_SW + i];
    struct RastPort *rp = pw->win->RPort;
    int x, y, w, h;
    if (!g) return;
    x = g->LeftEdge;
    y = g->TopEdge;
    w = g->Width;
    h = g->Height;
    SetAPen(rp, i == pw->ci ? 1 : 0);
    Move(rp, x - 2, y - 2);
    Draw(rp, x + w + 1, y - 2);
    Draw(rp, x + w + 1, y + h + 1);
    Draw(rp, x - 2, y + h + 1);
    Draw(rp, x - 2, y - 2);
    SetAPen(rp, swpen(i, pw->sel->rgb[i]));
    RectFill(rp, x + 2, y + 1, x + w - 3, y + h - 2);
    bevel(x, y, w, h, 1);
}

/* what the pages draw themselves: the Colors page's headings, labels and
 * swatches, the Presets page's preview */
static void draw_extras(void)
{
    struct RastPort *rp;
    int i, rx = pw->left, rh = pw->rh, y0 = pw->cy0;
    char line[64];
    if (!pw->win) return;
    rp = pw->win->RPort;
    release_swpens();
    if (pw->page == 1) {
        text_at(rx, y0, "Basic Colors", 1);
        for (i = 0; i < 8; i++) {
            int c = left_colors[i];
            struct Gadget *g = c >= 0 ? pw->g[G_SW + c] : NULL;
            if (c < 0) text_at(rx, y0 + (i + 1) * rh, "Cursor Colors", 1);
            else if (g) text_at(g->LeftEdge - 6 - tl(color_names[c]), g->TopEdge, color_names[c], 1);
        }
        text_at(pw->ax, y0, "ANSI Colors", 1);
        text_at(pw->ax + pw->alab + 6 + (pw->sww - tl("Normal")) / 2, y0 + rh, "Normal", 1);
        text_at(pw->ax + pw->alab + 12 + pw->sww + (pw->sww - tl("Bright")) / 2, y0 + rh, "Bright", 1);
        for (i = 0; i < 8; i++)
            text_at(pw->ax + pw->alab - tl(ansi_names[i]), y0 + (i + 2) * rh, ansi_names[i], 1);
        for (i = 0; i < NCOLORS; i++) draw_sw(i);
        SetAPen(rp, 0);
        RectFill(rp, rx, pw->ey, rx + pw->width - 1, pw->ey + pw->fh);
        sprintf(line, "%s   #%06lX", color_names[pw->ci], (unsigned long)pw->sel->rgb[pw->ci]);
        text_at(rx, pw->ey, line, 1);
    } else if (pw->page == 2 && pw->preset < nschemes && pw->pw_ > 20) {
        /* a small terminal in the scheme's colours */
        const ULONG *c = schemes[pw->preset].rgb;
        int x = pw->px, y = pw->py, w = pw->pw_, h = pw->ph, fh = pw->fh, bw, k;
        SetAPen(rp, swpen(C_BG, c[C_BG]));
        RectFill(rp, x + 2, y + 1, x + w - 3, y + h - 2);
        bevel(x, y, w, h, 1);
        text_at(x + 6, y + 4, "$ ls -l", swpen(C_FG, c[C_FG]));
        text_at(x + 6, y + 4 + fh + 2, "bold text", swpen(C_BOLD, c[C_BOLD]));
        SetAPen(rp, swpen(C_SEL, c[C_SEL]));
        RectFill(rp, x + 6, y + 4 + 2 * (fh + 2), x + 6 + tl("selected") - 1, y + 4 + 2 * (fh + 2) + fh);
        text_at(x + 6, y + 4 + 2 * (fh + 2), "selected", swpen(C_SELTEXT, c[C_SELTEXT]));
        SetAPen(rp, swpen(C_CURSOR, c[C_CURSOR]));
        RectFill(rp, x + 6 + tl("$ ls -l "), y + 4, x + 6 + tl("$ ls -l ") + tl("m") - 1, y + 4 + fh);
        /* the 16 ANSI colours, two rows */
        bw = (w - 12) / 8;
        for (k = 0; k < 16 && bw > 2; k++) {
            int bx = x + 6 + (k % 8) * bw, by = y + h - 6 - (2 - k / 8) * (fh + 2);
            SetAPen(rp, swpen(C_ANSI + k, c[C_ANSI + k]));
            RectFill(rp, bx, by, bx + bw - 2, by + fh);
        }
    }
}

/* put the gadgets of the current page up (again) */
static void show_page(void)
{
    struct Window *w = pw->win;
    if (pw->glist) {
        RemoveGList(w, pw->glist, -1);
        FreeGadgets(pw->glist);
        pw->glist = NULL;
    }
    SetAPen(w->RPort, 0);
    RectFill(w->RPort, w->BorderLeft, w->BorderTop, w->Width - w->BorderRight - 1, w->Height - w->BorderBottom - 1);
    build_plist();
    if (!build_gadgets()) return;
    AddGList(w, pw->glist, (UWORD)-1, -1, NULL);
    RefreshGList(pw->glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
    draw_extras();
}

static void changed(int list)
{
    if (list && pw->changed) pw->changed();
    if (pw->apply) pw->apply(pw->sel);
}

static Profile *copy_all(char *defname)
{
    Profile *head = NULL, **pp = &head, *p, *c;
    strcpy(defname, default_profile);
    for (p = profiles; p; p = p->next) {
        if (!(c = AllocVec(sizeof(Profile), MEMF_ANY))) break;
        *c = *p;
        c->next = NULL;
        *pp = c;
        pp = &c->next;
    }
    return head;
}

int prefs_open(struct Screen *scr, Profile *current, void (*apply)(Profile *), void (*profiles_changed)(void))
{
    struct TagItem wt[12];
    int i, w, h;
    if (pw) {
        WindowToFront(pw->win);
        ActivateWindow(pw->win);
        return 0;
    }
    if (!GadToolsBase || !(pw = AllocVec(sizeof(Prefs), MEMF_CLEAR))) return -1;
    pw->scr = scr;
    pw->sel = current ? current : prof_default();
    pw->apply = apply;
    pw->changed = profiles_changed;
    for (i = 0; i < NCOLORS; i++) pw->swpens[i] = -1;
    NewList(&pw->slist);
    pw->fh = scr->Font->ta_YSize;
    pw->saved = copy_all(pw->saved_default);
    pw->saved_cols = win_cols;
    pw->saved_rows = win_rows;
    pw->saved_left = win_left;
    pw->saved_top = win_top;
    if (!(pw->vi = GetVisualInfoA(scr, NULL))) {
        prefs_close();
        return -1;
    }
    {
        /* sizes from the screen font: labels, the lists and the widest
         * line of each page */
        static const char *const labels[] = {"Page", "Name", "Shell", "Send at start", "Directory", "Scrollback lines",
                                             "Presets", "Font", "Window columns", "Window rows", "Window left",
                                             "Window top", NULL};
        static const char *const pnames[] = {"* Solarized Light 2", "Delete Default", NULL};
        int box = pw->fh + 10, wgen, wcol, wtxt;
        pw->lab = (WORD)(maxw(labels, -1) + 12);
        pw->lw = (WORD)(maxw(pnames, -1) + 28);
        static const char *const lnames[] = {"Foreground", "Background", "Bold", "Selection", "Selected text",
                                             "Cursor Colors", "Cursor", "Cursor text", NULL};
        int half;
        pw->rh = (WORD)(pw->fh + 2);
        pw->sww = (WORD)(pw->fh * 2 + 6);
        if (tl("Normal") + 4 > pw->sww) pw->sww = (WORD)(tl("Normal") + 4);
        if (tl("Bright") + 4 > pw->sww) pw->sww = (WORD)(tl("Bright") + 4);
        pw->llab = (WORD)maxw(lnames, -1);
        pw->alab = (WORD)maxw(ansi_names, 8);
        if (tl("ANSI Colors") > pw->alab + 6 + 2 * pw->sww + 6) pw->alab = (WORD)(tl("ANSI Colors") - 2 * pw->sww - 12);
        pw->listh = (WORD)((pw->fh + 2) * 13);
        wgen = pw->lab + tl("Previous tab's") + 40 + 2 * box;
        if (tl("Copy text when selected") + 40 > wgen) wgen = tl("Copy text when selected") + 40;
        /* Colors: the two columns side by side; two check boxes a row */
        wcol = pw->llab + 6 + pw->sww + 24 + pw->alab + 6 + 2 * pw->sww + 6;
        half = tl("Brighten bold text") > tl("Transparent background") ? tl("Brighten bold text")
                                                                       : tl("Transparent background");
        if (2 * (half + 40) > wcol) wcol = 2 * (half + 40);
        if (tl("Bold text in the Bold colour") + 40 > wcol) wcol = tl("Bold text in the Bold colour") + 40;
        /* Presets: list, preview, three buttons */
        if (3 * (tl("Import...") + 24) + 8 > wcol) wcol = 3 * (tl("Import...") + 24) + 8;
        if (tl("Solarized Light") * 5 / 3 + 60 > wcol) wcol = tl("Solarized Light") * 5 / 3 + 60;
        wtxt = pw->lab + 2 * (tl("Automatic") + 24);
        if (pw->lab + tl("00000") + 24 + tl("Use current window") + 24 > wtxt)
            wtxt = pw->lab + tl("00000") + 24 + tl("Use current window") + 24;
        if (pw->lab + tl("Size 0, place -1: the default") + 8 > wtxt)
            wtxt = pw->lab + tl("Size 0, place -1: the default") + 8;
        pw->width = (WORD)(wgen > wcol ? (wgen > wtxt ? wgen : wtxt) : (wcol > wtxt ? wcol : wtxt));
    }
    w = scr->WBorLeft + scr->WBorRight + 8 + pw->lw + 12 + pw->width + 8;
    h = scr->WBorTop + pw->fh + 1 + 6 + (pw->fh + 6) * 15 + 40 + scr->WBorBottom;
    if (w > scr->Width) {
        /* the gadgets have to fit the narrower window too */
        pw->width -= (WORD)(w - scr->Width);
        w = scr->Width;
    }
    if (h > scr->Height) h = scr->Height;
    i = 0;
    wt[i].ti_Tag = WA_Left; wt[i++].ti_Data = (scr->Width - w) / 2;
    wt[i].ti_Tag = WA_Top; wt[i++].ti_Data = (scr->Height - h) / 2;
    wt[i].ti_Tag = WA_Width; wt[i++].ti_Data = w;
    wt[i].ti_Tag = WA_Height; wt[i++].ti_Data = h;
    wt[i].ti_Tag = WA_Title; wt[i++].ti_Data = (ULONG)"AmiBash Terminal Preferences";
    wt[i].ti_Tag = WA_CustomScreen; wt[i++].ti_Data = (ULONG)scr;
    wt[i].ti_Tag = WA_IDCMP;
    wt[i++].ti_Data = IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | LISTVIEWIDCMP | BUTTONIDCMP | STRINGIDCMP |
                      SLIDERIDCMP | CYCLEIDCMP | CHECKBOXIDCMP | INTEGERIDCMP | IDCMP_VANILLAKEY | IDCMP_RAWKEY;
    wt[i].ti_Tag = WA_Flags;
    wt[i++].ti_Data = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH;
    wt[i].ti_Tag = WA_AutoAdjust; wt[i++].ti_Data = TRUE;
    wt[i].ti_Tag = TAG_END;
    if (!(pw->win = OpenWindowTagList(NULL, wt))) {
        prefs_close();
        return -1;
    }
    show_page();
    return 0;
}

int prefs_is_open(void) { return pw != NULL; }

void prefs_glass_changed(void)
{
    if (pw && pw->win && pw->page == 1) show_page();
}

ULONG prefs_signal(void)
{
    return pw && pw->win ? 1UL << pw->win->UserPort->mp_SigBit : 0;
}

void prefs_close(void)
{
    if (!pw) return;
    if (pw->win) {
        if (pw->glist) RemoveGList(pw->win, pw->glist, -1);
        CloseWindow(pw->win);
    }
    if (pw->glist) FreeGadgets(pw->glist);
    release_swpens();
    free_slist();
    schemes_free();
    if (pw->vi) FreeVisualInfo(pw->vi);
    free_plist();
    while (pw->saved) {
        Profile *p = pw->saved;
        pw->saved = p->next;
        FreeVec(p);
    }
    FreeVec(pw);
    pw = NULL;
}

/* Cancel: everything as it was when the window opened */
static void revert(void)
{
    prof_free_all();
    profiles = pw->saved;
    pw->saved = NULL;
    strcpy(default_profile, pw->saved_default);
    win_cols = pw->saved_cols;
    win_rows = pw->saved_rows;
    win_left = pw->saved_left;
    win_top = pw->saved_top;
    pw->sel = prof_default();
}

static void set_rgb(int which, int v)
{
    ULONG *c = &pw->sel->rgb[pw->ci];
    int sh = which == G_R ? 16 : which == G_G ? 8 : 0;
    *c = (*c & ~(0xFFUL << sh)) | ((ULONG)(v & 255) << sh);
    draw_extras();
    changed(0);
}

/* Presets > Import...: an iTerm2 .itermcolors file, put in the profile
 * and kept as a scheme (MYSCHEMES) under the file's name */
static void import_scheme(void)
{
    struct FileRequester *fr;
    struct TagItem t[7];
    char path[300], name[40], *e;
    ULONG rgb[NCOLORS];
    if (!AslBase) return;
    t[0].ti_Tag = ASLFR_Window; t[0].ti_Data = (ULONG)pw->win;
    t[1].ti_Tag = ASLFR_TitleText; t[1].ti_Data = (ULONG)"Import an iTerm2 colour scheme";
    t[2].ti_Tag = ASLFR_InitialPattern; t[2].ti_Data = (ULONG)"#?.itermcolors";
    t[3].ti_Tag = ASLFR_DoPatterns; t[3].ti_Data = TRUE;
    t[4].ti_Tag = ASLFR_SleepWindow; t[4].ti_Data = TRUE;
    t[5].ti_Tag = ASLFR_RejectIcons; t[5].ti_Data = TRUE;
    t[6].ti_Tag = TAG_END;
    if (!(fr = AllocAslRequest(ASL_FileRequest, t))) return;
    if (AslRequest(fr, NULL) && fr->fr_File[0]) {
        strncpy(path, (char *)fr->fr_Drawer, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        AddPart((STRPTR)path, fr->fr_File, sizeof(path));
        memcpy(rgb, pw->sel->rgb, sizeof(rgb));
        if (scheme_import(path, rgb) < 3) DisplayBeep(pw->scr);
        else {
            BPTR fh;
            sprintf(name, "%.39s", (char *)fr->fr_File);
            if ((e = strstr(name, ".itermcolors")) != NULL) *e = 0;
            memcpy(pw->sel->rgb, rgb, sizeof(rgb));
            /* remember it: MYSCHEMES gets a line, the list the scheme */
            if ((fh = Lock((STRPTR)"ENVARC:AmiBash", SHARED_LOCK)) != 0) UnLock(fh);
            else if ((fh = CreateDir((STRPTR)"ENVARC:AmiBash")) != 0) UnLock(fh);
            if ((fh = Open((STRPTR)MYSCHEMES, MODE_READWRITE)) != 0) {
                char line[24];
                int i;
                Seek(fh, 0, OFFSET_END);
                FPuts(fh, (STRPTR)name);
                for (i = 0; i < NCOLORS; i++) {
                    sprintf(line, "%c%06lX", i ? ' ' : '\t', (unsigned long)rgb[i]);
                    FPuts(fh, (STRPTR)line);
                }
                FPuts(fh, (STRPTR)"\n");
                Close(fh);
            }
            schemes_free();
            schemes_load();
            changed(0);
            show_page();
        }
    }
    FreeAslRequest(fr);
}

/* Presets > Online...: the newest iTerm2-Color-Schemes gallery from
 * GitHub, by amibash-update (it has the HTTPS) */
static void online_schemes(void)
{
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    LONG rc;
    SetWindowPointer(pw->win, WA_BusyPointer, TRUE, TAG_END);
    rc = SystemTags((STRPTR)"amibash-update SCHEMES", SYS_Input, 0, SYS_Output, nil, TAG_END);
    SetWindowPointer(pw->win, TAG_END);
    if (nil) Close(nil);
    if (rc != 0) {
        struct EasyStruct es;
        es.es_StructSize = sizeof(es);
        es.es_Flags = 0;
        es.es_Title = (UBYTE *)"AmiBash Terminal";
        es.es_TextFormat = (UBYTE *)"Could not get the colour schemes from GitHub.\n"
                                    "amibash-update needs AmiSSL and a TCP/IP stack.";
        es.es_GadgetFormat = (UBYTE *)"OK";
        EasyRequestArgs(pw->win, &es, NULL, NULL);
        return;
    }
    schemes_free();
    schemes_load();
    show_page();
}

static void choose_dir(void)
{
    struct FileRequester *fr;
    struct TagItem t[6];
    if (!AslBase) return;
    t[0].ti_Tag = ASLFR_Window; t[0].ti_Data = (ULONG)pw->win;
    t[1].ti_Tag = ASLFR_TitleText; t[1].ti_Data = (ULONG)"Directory for new tabs";
    t[2].ti_Tag = ASLFR_DrawersOnly; t[2].ti_Data = TRUE;
    t[3].ti_Tag = ASLFR_InitialDrawer; t[3].ti_Data = (ULONG)pw->sel->dir;
    t[4].ti_Tag = ASLFR_SleepWindow; t[4].ti_Data = TRUE;
    t[5].ti_Tag = TAG_END;
    if ((fr = AllocAslRequest(ASL_FileRequest, t)) != NULL) {
        if (AslRequest(fr, NULL)) {
            sprintf(pw->sel->dir, "%.159s", (char *)fr->fr_Drawer);
            show_page();
        }
        FreeAslRequest(fr);
    }
}

static void choose_font(void)
{
    struct FontRequester *fr;
    struct TagItem t[7];
    if (!AslBase) return;
    t[0].ti_Tag = ASLFO_Window; t[0].ti_Data = (ULONG)pw->win;
    t[1].ti_Tag = ASLFO_TitleText; t[1].ti_Data = (ULONG)"Terminal font";
    t[2].ti_Tag = ASLFO_FixedWidthOnly; t[2].ti_Data = TRUE;
    t[3].ti_Tag = ASLFO_MaxHeight; t[3].ti_Data = 72;
    t[4].ti_Tag = ASLFO_SleepWindow; t[4].ti_Data = TRUE;
    t[5].ti_Tag = TAG_END;
    if ((fr = AllocAslRequest(ASL_FontRequest, t)) != NULL) {
        if (AslRequest(fr, NULL)) {
            char n[64];
            sprintf(n, "%.50s", (char *)fr->fo_Attr.ta_Name);
            if (strlen(n) > 5 && !strcmp(n + strlen(n) - 5, ".font")) n[strlen(n) - 5] = 0;
            sprintf(pw->sel->font, "%s/%d", n, (int)fr->fo_Attr.ta_YSize);
            show_page();
            changed(0);
        }
        FreeAslRequest(fr);
    }
}

/* the window gadgets (on the Text page) */
static void read_size(void)
{
    LONG v;
    if (pw->page != 3 || !pw->g[G_COLS] || !pw->g[G_ROWS] || !pw->g[G_LEFT] || !pw->g[G_TOP]) return;
    v = ((struct StringInfo *)pw->g[G_COLS]->SpecialInfo)->LongInt;
    win_cols = v < 0 ? 0 : v > 500 ? 500 : (int)v;
    v = ((struct StringInfo *)pw->g[G_ROWS]->SpecialInfo)->LongInt;
    win_rows = v < 0 ? 0 : v > 300 ? 300 : (int)v;
    v = ((struct StringInfo *)pw->g[G_LEFT]->SpecialInfo)->LongInt;
    win_left = v < -1 ? -1 : v > 9999 ? 9999 : (int)v;
    v = ((struct StringInfo *)pw->g[G_TOP]->SpecialInfo)->LongInt;
    win_top = v < -1 ? -1 : v > 9999 ? 9999 : (int)v;
}

void prefs_handle(void)
{
    struct IntuiMessage *m;
    int close = 0, save = 0, cancel = 0;
    if (!pw) return;
    while (!close && (m = GT_GetIMsg(pw->win->UserPort)) != NULL) {
        ULONG cl = m->Class;
        UWORD code = m->Code, m_qual = m->Qualifier;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        switch (cl) {
        case IDCMP_CLOSEWINDOW:
            close = 1;                   /* like Use */
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(pw->win);
            GT_EndRefresh(pw->win, TRUE);
            draw_extras();
            break;
        case IDCMP_VANILLAKEY:
            if (code == 27) close = cancel = 1;
            else if (code == 'p' || code == 'P') {
                /* P / Shift-P: the next / previous page */
                pw->page = (pw->page + (code == 'p' ? 1 : 3)) % 4;
                show_page();
            } else if ((code == 13 || code == 'a') && pw->page == 2 && pw->preset < nschemes) {
                /* Return: apply the scheme picked */
                memcpy(pw->sel->rgb, schemes[pw->preset].rgb, sizeof(pw->sel->rgb));
                changed(0);
            }
            break;
        case IDCMP_RAWKEY:
            /* cursor up / down (Shift: a page) through the schemes */
            if (pw->page == 2 && nschemes && (code == 0x4C || code == 0x4D) && pw->g[G_SCHEMES]) {
                int step = (m_qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? 12 : 1;
                int n = pw->preset + (code == 0x4C ? -step : step);
                pw->preset = n < 0 ? 0 : n >= nschemes ? nschemes - 1 : n;
                GT_SetGadgetAttrs(pw->g[G_SCHEMES], pw->win, NULL, GTLV_Selected, (ULONG)pw->preset,
                                  GTLV_MakeVisible, (ULONG)pw->preset, TAG_END);
                draw_extras();
            }
            break;
        case IDCMP_MOUSEMOVE:
        case IDCMP_GADGETUP:
        case IDCMP_GADGETDOWN:
            switch (g->GadgetID) {
            case G_PROFILES: {
                Profile *p = profiles;
                int i;
                for (i = 0; p && i < code; i++) p = p->next;
                if (p) {
                    pw->sel = p;
                    show_page();
                }
                break;
            }
            case G_NEW:
            case G_COPY: {
                Profile *p = prof_new(g->GadgetID == G_NEW ? "New profile" : pw->sel->name,
                                      g->GadgetID == G_COPY ? pw->sel : NULL);
                if (p) {
                    pw->sel = p;
                    pw->page = 0;
                    show_page();
                    changed(1);
                }
                break;
            }
            case G_DELETE:
                if (profiles && profiles->next) {
                    Profile *n = pw->sel->next ? pw->sel->next : profiles;
                    if (n == pw->sel) n = profiles->next;
                    prof_delete(pw->sel);
                    pw->sel = n;
                    show_page();
                    changed(1);
                }
                break;
            case G_DEFAULT:
                strcpy(default_profile, pw->sel->name);
                show_page();
                changed(1);
                break;
            case G_PAGE:
                read_size();
                pw->page = code;
                show_page();
                break;
            case G_NAME: {
                char *s = (char *)((struct StringInfo *)g->SpecialInfo)->Buffer;
                Profile *o = prof_find(s);
                if (*s && (!o || o == pw->sel)) {
                    int wasdef = !Stricmp((STRPTR)default_profile, (STRPTR)pw->sel->name);
                    sprintf(pw->sel->name, "%.31s", s);
                    if (wasdef) strcpy(default_profile, pw->sel->name);
                    changed(1);
                }
                show_page();
                break;
            }
            case G_CMD:
                sprintf(pw->sel->cmd, "%.159s", (char *)((struct StringInfo *)g->SpecialInfo)->Buffer);
                break;
            case G_DIRMODE:
                pw->sel->dirmode = code;
                show_page();
                break;
            case G_DIR:
                sprintf(pw->sel->dir, "%.159s", (char *)((struct StringInfo *)g->SpecialInfo)->Buffer);
                break;
            case G_DIRREQ:
                choose_dir();
                break;
            case G_SCROLLBACK: {
                LONG v = ((struct StringInfo *)g->SpecialInfo)->LongInt;
                pw->sel->scrollback = v < 0 ? 0 : v > 20000 ? 20000 : (int)v;
                break;
            }
            case G_COPYSEL:
                pw->sel->copysel = (g->Flags & GFLG_SELECTED) != 0;
                break;
            case G_SHELL:
                pw->sel->shell = code ? SHELL_AMIGA : SHELL_BASH;
                break;
            case G_TRANSP:
                pw->sel->transparent = (g->Flags & GFLG_SELECTED) != 0;
                changed(0);
                break;
            case G_R:
            case G_G:
            case G_B:
                set_rgb(g->GadgetID, (WORD)code);
                break;
            case G_BRIGHTEN:
                pw->sel->brighten = (g->Flags & GFLG_SELECTED) != 0;
                changed(0);
                break;
            case G_BOLDCOLOR:
                pw->sel->boldcolor = (g->Flags & GFLG_SELECTED) != 0;
                changed(0);
                break;
            case G_SCHEMES:
                pw->preset = code;
                draw_extras();
                break;
            case G_SAPPLY:
                if (pw->preset < nschemes) {
                    memcpy(pw->sel->rgb, schemes[pw->preset].rgb, sizeof(pw->sel->rgb));
                    changed(0);
                }
                break;
            case G_SIMPORT:
                import_scheme();
                break;
            case G_SONLINE:
                online_schemes();
                break;
            default:
                if (g->GadgetID >= G_SW && g->GadgetID <= G_SW_END) {
                    /* a swatch: edit that colour */
                    ULONG c;
                    pw->ci = g->GadgetID - G_SW;
                    c = pw->sel->rgb[pw->ci];
                    GT_SetGadgetAttrs(pw->g[G_R], pw->win, NULL, GTSL_Level, (c >> 16) & 255, TAG_END);
                    GT_SetGadgetAttrs(pw->g[G_G], pw->win, NULL, GTSL_Level, (c >> 8) & 255, TAG_END);
                    GT_SetGadgetAttrs(pw->g[G_B], pw->win, NULL, GTSL_Level, c & 255, TAG_END);
                    draw_extras();
                }
                break;
            case G_FONTREQ:
                choose_font();
                break;
            case G_COLS:
            case G_ROWS:
            case G_LEFT:
            case G_TOP:
                read_size();
                break;
            case G_CURSIZE:
                term_current_box(&win_cols, &win_rows, &win_left, &win_top);
                show_page();
                break;
            case G_FONTAUTO:
                pw->sel->font[0] = 0;
                show_page();
                changed(0);
                break;
            case G_SAVE:
                close = save = 1;
                break;
            case G_USE:
                close = 1;
                break;
            case G_CANCEL:
                close = cancel = 1;
                break;
            }
            break;
        }
    }
    if (!close) return;
    if (cancel) revert();
    else read_size();                     /* typed, but no Return pressed */
    if (save && prof_save()) DisplayBeep(pw->scr);
    {
        void (*ap)(Profile *) = pw->apply, (*ch)(void) = pw->changed;
        Profile *s = pw->sel;
        prefs_close();
        if (ch) ch();
        if (ap) ap(s);
    }
}

#endif /* AMIGA */
