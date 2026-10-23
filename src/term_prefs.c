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

extern struct Library *GadToolsBase, *AslBase, *UtilityBase;

Profile *profiles;
char default_profile[32];
int win_cols, win_rows;
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
 *   window COLUMNS ROWS     (0: automatic)
 *   profile NAME
 *   color N RRGGBB          (N: 0 .. NCOLORS-1, see enum in term.h)
 *   brighten 0|1  boldcolor 0|1  copysel 0|1  scrollback N  dirmode N
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

void prof_load(void)
{
    char *b = read_whole("ENV:AmiBash/terminal.prefs"), *l, *e, *v;
    Profile *cur = NULL;
    if (!b) b = read_whole("ENVARC:AmiBash/terminal.prefs");
    prof_free_all();
    default_profile[0] = 0;
    win_cols = win_rows = 0;
    for (l = b; l && *l; l = e) {
        for (e = l; *e && *e != '\n'; e++) ;
        if (*e) *e++ = 0;
        for (v = l; *v && *v != ' '; v++) ;
        if (*v) *v++ = 0;
        if (!strcmp(l, "default")) sprintf(default_profile, "%.31s", v);
        else if (!strcmp(l, "window")) {
            char *r = strchr(v, ' ');
            win_cols = atoi(v);
            win_rows = r ? atoi(r + 1) : 0;
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
        else if (!strcmp(l, "dirmode")) cur->dirmode = atoi(v);
        else if (!strcmp(l, "font")) sprintf(cur->font, "%.63s", v);
        else if (!strcmp(l, "dir")) sprintf(cur->dir, "%.159s", v);
        else if (!strcmp(l, "cmd")) sprintf(cur->cmd, "%.159s", v);
    }
    if (b) FreeVec(b);
    prof_default();
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
    sprintf(line, "default %s\nwindow %d %d\n", default_profile, win_cols, win_rows);
    FPuts(fh, (STRPTR)line);
    for (p = profiles; p; p = p->next) {
        sprintf(line, "profile %s\n", p->name);
        FPuts(fh, (STRPTR)line);
        for (i = 0; i < NCOLORS; i++) {
            sprintf(line, "color %d %06lX\n", i, (unsigned long)p->rgb[i]);
            FPuts(fh, (STRPTR)line);
        }
        sprintf(line, "brighten %d\nboldcolor %d\ncopysel %d\nscrollback %d\ndirmode %d\nfont %s\ndir %s\ncmd %s\n",
                p->brighten, p->boldcolor, p->copysel, p->scrollback, p->dirmode, p->font, p->dir, p->cmd);
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
    G_NAME, G_CMD, G_DIRMODE, G_DIR, G_DIRREQ, G_SCROLLBACK, G_COPYSEL,
    G_COLORS, G_R, G_G, G_B, G_BRIGHTEN, G_BOLDCOLOR, G_PRESET, G_APPLYPRESET,
    G_FONTNAME, G_FONTREQ, G_FONTAUTO, G_COLS, G_ROWS, G_CURSIZE,
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
    struct List clist;
    struct Node cnodes[NCOLORS];
    /* undo for Cancel */
    Profile *saved;
    char saved_default[32];
    int saved_cols, saved_rows;
    /* the colour swatch */
    WORD sx, sy, sw, sh;
    LONG swpen;
    int fh;
    WORD left, top, width;               /* right-hand page area */
    WORD lab, lw, cl;                    /* label column, profile list, colour list */
    char fontlabel[80];
} Prefs;

static Prefs *pw;

static STRPTR page_labels[] = {(STRPTR)"General", (STRPTR)"Colors", (STRPTR)"Text", NULL};
static STRPTR dir_labels[] = {(STRPTR)"Home directory", (STRPTR)"Previous tab's", (STRPTR)"This directory:", NULL};
static STRPTR preset_labels[8];

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
    g = mk(CYCLE_KIND, g, &ng, G_PAGE, rx + lab, y0, rw - lab, bh, "Page", GTCY_Labels, (ULONG)page_labels,
           GTCY_Active, (ULONG)pw->page, TAG_IGNORE, 0);
    y = y0 + bh + 10;
    pw->left = (WORD)rx;
    pw->top = (WORD)y;
    pw->sw = 0;
    if (pw->page == 0) {
        g = mk(STRING_KIND, g, &ng, G_NAME, rx + lab, y, rw - lab, bh, "Name", GTST_String, (ULONG)p->name,
               GTST_MaxChars, 31, TAG_IGNORE, 0);
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
        int cl = pw->cl, sl = tl("R") + 8, sx = rx + cl + 10 + sl;
        g = mk(LISTVIEW_KIND, g, &ng, G_COLORS, rx, y, cl, bh * 8, NULL, GTLV_Labels, (ULONG)&pw->clist,
               GTLV_ShowSelected, 0, GTLV_Selected, (ULONG)pw->ci);
        pw->sx = (WORD)(rx + cl + 10);
        pw->sy = (WORD)y;
        pw->sw = (WORD)(rw - cl - 10);
        pw->sh = (WORD)(bh * 2);
        g = mk(SLIDER_KIND, g, &ng, G_R, sx, y + bh * 2 + 6, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 2, "R",
               GTSL_Max, 255, GTSL_Level, (c >> 16) & 255, GA_RelVerify, TRUE);
        g = mk(SLIDER_KIND, g, &ng, G_G, sx, y + bh * 3 + 8, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 2, "G",
               GTSL_Max, 255, GTSL_Level, (c >> 8) & 255, GA_RelVerify, TRUE);
        g = mk(SLIDER_KIND, g, &ng, G_B, sx, y + bh * 4 + 10, rw - (sx - rx) - 4 * (fh * 3 / 4), bh - 2, "B",
               GTSL_Max, 255, GTSL_Level, c & 255, GA_RelVerify, TRUE);
        y += bh * 8 + 6;
        g = mk(CHECKBOX_KIND, g, &ng, G_BRIGHTEN, rx, y, 26, fh + 4, "Brighten bold text", GTCB_Checked,
               (ULONG)p->brighten, GTCB_Scaled, TRUE, TAG_IGNORE, 0);
        y += fh + 8;
        g = mk(CHECKBOX_KIND, g, &ng, G_BOLDCOLOR, rx, y, 26, fh + 4, "Bold text in the Bold colour",
               GTCB_Checked, (ULONG)p->boldcolor, GTCB_Scaled, TRUE, TAG_IGNORE, 0);
        y += fh + 10;
        g = mk(CYCLE_KIND, g, &ng, G_PRESET, rx + lab, y, rw - lab - tl("Apply") - 28, bh, "Presets",
               GTCY_Labels, (ULONG)preset_labels, GTCY_Active, (ULONG)pw->preset, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_APPLYPRESET, rx + rw - tl("Apply") - 24, y, tl("Apply") + 24, bh, "Apply",
               TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
    } else {
        set_font_label(p);
        g = mk(TEXT_KIND, g, &ng, G_FONTNAME, rx + lab, y, rw - lab, bh, "Font", GTTX_Text, (ULONG)pw->fontlabel,
               GTTX_Border, TRUE, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(BUTTON_KIND, g, &ng, G_FONTREQ, rx + lab, y, (rw - lab) / 2 - 2, bh, "_Choose...", TAG_IGNORE, 0,
               TAG_IGNORE, 0, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_FONTAUTO, rx + lab + (rw - lab) / 2 + 2, y, (rw - lab) / 2 - 2, bh,
               "_Automatic", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
        /* the window, for all profiles; 0 is the default size */
        y += bh + 14;
        g = mk(INTEGER_KIND, g, &ng, G_COLS, rx + lab, y, tl("0000") + 16, bh, "Window columns",
               GTIN_Number, (ULONG)win_cols, GTIN_MaxChars, 3, TAG_IGNORE, 0);
        g = mk(BUTTON_KIND, g, &ng, G_CURSIZE, rx + lab + tl("0000") + 24, y, rw - lab - tl("0000") - 24,
               bh, "Current si_ze", TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(INTEGER_KIND, g, &ng, G_ROWS, rx + lab, y, tl("0000") + 16, bh, "Window rows",
               GTIN_Number, (ULONG)win_rows, GTIN_MaxChars, 3, TAG_IGNORE, 0);
        y += bh + 4;
        g = mk(TEXT_KIND, g, &ng, 0, rx + lab, y, rw - lab, bh, NULL, GTTX_Text,
               (ULONG)"0: the default size", TAG_IGNORE, 0, TAG_IGNORE, 0);
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

static void draw_swatch(void)
{
    struct RastPort *rp;
    ULONG c;
    LONG pen;
    if (!pw->win || pw->page != 1 || pw->sw <= 0) return;
    rp = pw->win->RPort;
    c = pw->sel->rgb[pw->ci];
    if (pw->swpen >= 0) ReleasePen(pw->scr->ViewPort.ColorMap, pw->swpen);
    pen = ObtainBestPenA(pw->scr->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL,
                         ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL, NULL);
    pw->swpen = pen;
    SetAPen(rp, pen >= 0 ? pen : 1);
    RectFill(rp, pw->sx + 2, pw->sy + 2, pw->sx + pw->sw - 3, pw->sy + pw->sh - 3);
    {
        /* recessed box needs tags: draw it with the visual info */
        struct TagItem t[3];
        t[0].ti_Tag = GT_VisualInfo;
        t[0].ti_Data = (ULONG)pw->vi;
        t[1].ti_Tag = GTBB_Recessed;
        t[1].ti_Data = TRUE;
        t[2].ti_Tag = TAG_END;
        DrawBevelBoxA(rp, pw->sx, pw->sy, pw->sw, pw->sh, t);
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
    draw_swatch();
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
    pw->swpen = -1;
    pw->fh = scr->Font->ta_YSize;
    pw->saved = copy_all(pw->saved_default);
    pw->saved_cols = win_cols;
    pw->saved_rows = win_rows;
    for (i = 0; i < NPRESETS && i < 7; i++) preset_labels[i] = (STRPTR)presets[i].name;
    preset_labels[i] = NULL;
    NewList(&pw->clist);
    for (i = 0; i < NCOLORS; i++) {
        pw->cnodes[i].ln_Name = (char *)color_names[i];
        AddTail(&pw->clist, &pw->cnodes[i]);
    }
    if (!(pw->vi = GetVisualInfoA(scr, NULL))) {
        prefs_close();
        return -1;
    }
    {
        /* sizes from the screen font: labels, the lists and the widest
         * line of each page */
        static const char *const labels[] = {"Page", "Name", "Send at start", "Directory", "Scrollback lines",
                                             "Presets", "Font", "Window columns", "Window rows", NULL};
        static const char *const pnames[] = {"* Solarized Light 2", "Delete Default", NULL};
        int box = pw->fh + 10, wgen, wcol, wtxt;
        pw->lab = (WORD)(maxw(labels, -1) + 12);
        pw->lw = (WORD)(maxw(pnames, -1) + 28);
        pw->cl = (WORD)(maxw(color_names, NCOLORS) + 28);   /* no NULL at its end */
        wgen = pw->lab + tl("Previous tab's") + 40 + 2 * box;
        if (tl("Copy text when selected") + 40 > wgen) wgen = tl("Copy text when selected") + 40;
        wcol = pw->cl + 10 + tl("R") + 8 + 160;
        if (tl("Bold text in the Bold colour") + 40 > wcol) wcol = tl("Bold text in the Bold colour") + 40;
        if (pw->lab + maxw((const char *const *)preset_labels, -1) + 40 + tl("Apply") + 24 > wcol)
            wcol = pw->lab + maxw((const char *const *)preset_labels, -1) + 40 + tl("Apply") + 24;
        wtxt = pw->lab + 2 * (tl("Automatic") + 24);
        if (pw->lab + tl("0000") + 24 + tl("Current size") + 24 > wtxt)
            wtxt = pw->lab + tl("0000") + 24 + tl("Current size") + 24;
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
                      SLIDERIDCMP | CYCLEIDCMP | CHECKBOXIDCMP | INTEGERIDCMP | IDCMP_VANILLAKEY;
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
    if (pw->swpen >= 0) ReleasePen(pw->scr->ViewPort.ColorMap, pw->swpen);
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
    pw->sel = prof_default();
}

static void set_rgb(int which, int v)
{
    ULONG *c = &pw->sel->rgb[pw->ci];
    int sh = which == G_R ? 16 : which == G_G ? 8 : 0;
    *c = (*c & ~(0xFFUL << sh)) | ((ULONG)(v & 255) << sh);
    draw_swatch();
    changed(0);
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

/* the window size gadgets (on the Text page) */
static void read_size(void)
{
    LONG v;
    if (pw->page != 2 || !pw->g[G_COLS] || !pw->g[G_ROWS]) return;
    v = ((struct StringInfo *)pw->g[G_COLS]->SpecialInfo)->LongInt;
    win_cols = v < 0 ? 0 : v > 500 ? 500 : (int)v;
    v = ((struct StringInfo *)pw->g[G_ROWS]->SpecialInfo)->LongInt;
    win_rows = v < 0 ? 0 : v > 300 ? 300 : (int)v;
}

void prefs_handle(void)
{
    struct IntuiMessage *m;
    int close = 0, save = 0, cancel = 0;
    if (!pw) return;
    while (!close && (m = GT_GetIMsg(pw->win->UserPort)) != NULL) {
        ULONG cl = m->Class;
        UWORD code = m->Code;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        switch (cl) {
        case IDCMP_CLOSEWINDOW:
            close = 1;                   /* like Use */
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(pw->win);
            GT_EndRefresh(pw->win, TRUE);
            draw_swatch();
            break;
        case IDCMP_VANILLAKEY:
            if (code == 27) close = cancel = 1;
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
            case G_COLORS:
                pw->ci = code;
                show_page();
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
            case G_PRESET:
                pw->preset = code;
                break;
            case G_APPLYPRESET:
                preset_apply(pw->preset, pw->sel);
                show_page();
                changed(0);
                break;
            case G_FONTREQ:
                choose_font();
                break;
            case G_COLS:
            case G_ROWS:
                read_size();
                break;
            case G_CURSIZE:
                term_current_size(&win_cols, &win_rows);
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
