/*
 * term_amiga.c - AmiBash Terminal: a tabbed terminal window for AmigaOS.
 *
 * The Amiga console (CON:) has no tabs, so this is a small terminal of
 * its own: one Intuition window with a tab bar ("x" close box per tab,
 * "+" to add one), and for every tab
 *   - a character cell buffer with an ANSI / Amiga console interpreter,
 *   - a DOS console handler (its own message port) that implements the
 *     packets a console must answer: read/write, raw/cooked mode,
 *     WaitForChar, Open("*"), Ctrl-C, window bounds report ...
 *   - a bash process (a CLI process of its own) on file handles that
 *     point at that port, so every program run in the tab sees a normal
 *     interactive console.
 *
 * Shortcuts (either Amiga key): T new tab, W close tab, 1-9 select tab,
 * [ ] previous/next tab, Q quit.  Shells can ask for tabs through the
 * public port "AmiBash.Terminal" (bashwin builtin, F1/F2 keys).
 */
#ifdef AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/displayinfo.h>
#include <diskfont/diskfont.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/keymap.h>
#include <proto/diskfont.h>
#include <proto/asl.h>
#include <proto/utility.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "sh.h"

#ifdef AMIBASH_PURE
BPTR pure_seglist(void);
#endif

extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase;
struct Library *KeymapBase;
struct Library *AslBase;
extern struct Library *DiskfontBase;
struct TextFont *os_load_font(const char *fpath, int size);

#define MAXTABS 9
#define INQ 1024                 /* keyboard input queue per tab      */
#define LINEMAX 256              /* cooked mode line                  */

/* attributes */
#define A_FG(a)   ((a) & 7)
#define A_BG(a)   (((a) >> 3) & 7)
#define A_BOLD    0x40
#define A_ULINE   0x80
#define A_ITAL    0x100
#define A_INV     0x200
#define A_DEFFG   0x400          /* theme text colour, not ANSI fg     */
#define A_DEFBG   0x800          /* theme background                   */

typedef struct PktNode { struct DosPacket *dp; long deadline; struct PktNode *next; } PktNode;

typedef struct Tab {
    int used;
    int closing;                 /* closed by the user, shell exiting  */
    int hidden;                  /* removed from the bar               */
    char title[64];
    struct MsgPort *port;        /* console handler port               */
    int opencount;               /* file handles referring to us       */
    int started;
    /* screen */
    UBYTE *ch;
    UWORD *at;
    int cx, cy, sx, sy;          /* cursor, saved cursor               */
    int wrap;                    /* pending wrap (deferred, like xterm) */
    UWORD attr;
    int cursor_on;
    /* escape parser */
    int esc;                     /* 0 none, 1 ESC, 2 CSI, 3 OSC        */
    char eb[80];
    int el;
    /* input */
    int raw;
    char inq[INQ];
    int qh, qt;
    int eof;                     /* ^\ in cooked mode                  */
    char line[LINEMAX];
    int ll;
    PktNode *reads, *waits;
    struct Task *reader;         /* gets Ctrl-C                        */
    /* scrollback: a ring of lines that scrolled off the top */
    UBYTE *hch;
    UWORD *hat;
    int hw, hlines;              /* line width, capacity               */
    int hcount, hhead;           /* lines stored, next slot            */
    int hfail;                   /* no memory for it                   */
    int view;                    /* lines scrolled back (0: live)      */
} Tab;

static Tab *tabs;                /* MAXTABS of them                    */
static int ntabs_open;           /* visible tabs                       */
static int cur = -1;             /* active tab index                   */
static struct Window *win;
static struct Screen *scr;
static struct Menu *menus;
static APTR vi;
static struct DrawInfo *dri;
static struct TextFont *font;
static int cw = 8, chh = 8, base = 6;
static int cols = 80, rows = 24;
static int tabh;                 /* tab bar height                     */
static int quitting;
static struct MsgPort *pubport;  /* "AmiBash.Terminal"                 */
static struct MsgPort *timerport;
static struct timerequest *treq;
static int timer_busy;
static char self[256];
static UWORD def_attr;           /* fg/bg of the theme                 */
static int pen_text = 2, pen_back = 1, pen_hi = 3;
static LONG ansipen[16];         /* ANSI colours 0-7, bright 8-15      */
static int pens_obtained;

/* tab bar geometry */
static int plus_w;

/* scroller in the right window border */
static int sb_lines = 300;       /* scrollback lines per tab           */
static Object *sb_prop, *sb_up, *sb_down;
static Object *img_size, *img_up, *img_down;
static int sb_dirty;             /* prop needs updating                */
static int sb_held;              /* arrow held down: -1 up, +1 down    */
static LONG sb_settop = -1;      /* last top we set                    */
static int launched;             /* shells started successfully          */

/* ------------------------------------------------------------------ */
/* TrueType text through ttengine.library (Aminet util/libs/ttengine),  */
/* used when it is installed.  vbcc has no inline header for it, so the */
/* calls are declared here (offsets from ttengine_lib.sfd).             */

struct Library *TTEngineBase;
struct Library *UtilityBase;
#define TT_FontFile         0x6EDA0000
#define TT_FamilyTable      0x6EDA0002
#define TT_FontSize         0x6EDA0003
#define TT_Window           0x6EDA0007
#define TT_Antialias        0x6EDA000F
#define TT_Antialias_Off    1
#define TT_Antialias_On     2
#define TT_Encoding         0x6EDA0010
#define TT_Encoding_ISO8859_1 4
#define TT_FontBaseline     0x6EDA001E
#define TT_FontFixedWidth   0x6EDA0026
#define TT_FontHeight       0x6EDA0027
#define TT_FontWidth        0x6EDA0028
#define TT_DiskFontMetrics  0x6EDA0029
#define TTRQ_Window         0x6EDA2000
#define TTRQ_TitleText      0x6EDA2004
#define TTRQ_DoSizes        0x6EDA200B
#define TTRQ_InitialSize    0x6EDA200F
#define TTRQ_DoPreview      0x6EDA2012
#define TTRQ_FixedWidthOnly 0x6EDA2013

APTR tt_OpenFontA(__reg("a6") struct Library *, __reg("a0") struct TagItem *) = "\tjsr\t-30(a6)";
BOOL tt_SetFont(__reg("a6") struct Library *, __reg("a1") struct RastPort *, __reg("a0") APTR) = "\tjsr\t-36(a6)";
VOID tt_CloseFont(__reg("a6") struct Library *, __reg("a0") APTR) = "\tjsr\t-42(a6)";
VOID tt_Text(__reg("a6") struct Library *, __reg("a1") struct RastPort *, __reg("a0") APTR,
             __reg("d0") ULONG) = "\tjsr\t-48(a6)";
ULONG tt_SetAttrsA(__reg("a6") struct Library *, __reg("a1") struct RastPort *,
                   __reg("a0") struct TagItem *) = "\tjsr\t-54(a6)";
ULONG tt_GetAttrsA(__reg("a6") struct Library *, __reg("a1") struct RastPort *,
                   __reg("a0") struct TagItem *) = "\tjsr\t-60(a6)";
VOID tt_DoneRastPort(__reg("a6") struct Library *, __reg("a1") struct RastPort *) = "\tjsr\t-96(a6)";
APTR tt_AllocRequest(__reg("a6") struct Library *) = "\tjsr\t-102(a6)";
struct TagItem *tt_RequestA(__reg("a6") struct Library *, __reg("a0") APTR,
                            __reg("a1") struct TagItem *) = "\tjsr\t-108(a6)";
VOID tt_FreeRequest(__reg("a6") struct Library *, __reg("a0") APTR) = "\tjsr\t-114(a6)";

static APTR ttfont;              /* TrueType font in use, or NULL      */
static char tt_spec[200];        /* its file, or "tt:" + family name   */
static int tt_size, tt_default;
static int tt_cw, tt_chh, tt_base;

/* ------------------------------------------------------------------ */
/* small helpers                                                        */

static BPTR dbgfh;
static void dbg(const char *fmt, LONG a, LONG b, LONG c)
{
    char buf[400];
    if (!dbgfh) return;
    sprintf(buf, fmt, a, b, c);
    Write(dbgfh, buf, strlen(buf));
    Write(dbgfh, "\n", 1);
}

static long now_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ds.ds_Minute * 60L * 50L) + ds.ds_Tick;
}

static Tab *active(void)
{
    return cur >= 0 && tabs[cur].used && !tabs[cur].hidden ? &tabs[cur] : NULL;
}

static int term_top(void)
{
    return tabh;
}

/* ------------------------------------------------------------------ */
/* drawing                                                              */

/* pens for an attribute: the theme's, or the ANSI colour (bold: the
 * bright one) */
static LONG pen_fg(UWORD a)
{
    if (a & A_DEFFG) return pen_text;
    return ansipen[A_FG(a) + ((a & A_BOLD) ? 8 : 0)];
}

static LONG pen_bg(UWORD a)
{
    return (a & A_DEFBG) ? pen_back : ansipen[A_BG(a)];
}

static void set_pens(UWORD a)
{
    LONG fg = pen_fg(a), bg = pen_bg(a);
    if (a & A_INV) {
        LONG t = fg;
        fg = bg;
        bg = t;
    }
    SetAPen(win->RPort, fg);
    SetBPen(win->RPort, bg);
    SetDrMd(win->RPort, JAM2);
}

/* the 16 ANSI colours: exact pens where the screen has free ones (RTG,
 * 8-bit Workbench), else the nearest - but never the background */
static void obtain_pens(void)
{
    static const ULONG rgb[16] = {
        0x000000, 0xcd3131, 0x0dbc79, 0xe5e510, 0x3b78ff, 0xbc3fbc, 0x11a8cd, 0xe5e5e5,
        0x666666, 0xf14c4c, 0x23d18b, 0xf5f543, 0x5c9dff, 0xd670d6, 0x29b8db, 0xffffff};
    struct TagItem tg[2];
    int i;
    tg[0].ti_Tag = OBP_Precision;
    tg[0].ti_Data = PRECISION_IMAGE;
    tg[1].ti_Tag = TAG_END;
    for (i = 0; i < 16; i++) {
        ULONG c = rgb[i];
        LONG p = -1;
        if (GfxBase->LibNode.lib_Version >= 39)
            p = ObtainBestPenA(scr->ViewPort.ColorMap, ((c >> 16) & 255) * 0x01010101UL,
                               ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL, tg);
        if (p >= 0) pens_obtained |= 1 << i;
        else p = i == 0 ? pen_back : (i == 7 || i >= 8) ? pen_text : pen_hi;
        if (p == pen_back && i != 0) p = (i == 4 || i == 12) ? pen_hi : pen_text;
        ansipen[i] = p;
    }
}

static void release_pens(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (pens_obtained & (1 << i)) ReleasePen(scr->ViewPort.ColorMap, ansipen[i]);
    pens_obtained = 0;
}

/* Text() in the terminal's font, bitmap or TrueType */
static void txt(struct RastPort *r, const char *s, int n)
{
    if (ttfont) tt_Text(TTEngineBase, r, (APTR)s, (ULONG)n);
    else Text(r, (STRPTR)s, n);
}

/* draw cells [x0, x1) of the line c/a at display row y */
static void draw_cells(int y, const UBYTE *c, const UWORD *at, int x0, int x1)
{
    int x = x0;
    while (x < x1) {
        UWORD a = at[x];
        int s = x;
        while (x < x1 && at[x] == a) x++;
        set_pens(a);
        Move(win->RPort, s * cw, term_top() + y * chh + base);
        txt(win->RPort, (const char *)&c[s], x - s);
        /* bold and underline drawn by hand (no algorithmic soft styles) */
        if (a & A_BOLD) {
            SetDrMd(win->RPort, JAM1);
            Move(win->RPort, s * cw + 1, term_top() + y * chh + base);
            txt(win->RPort, (const char *)&c[s], x - s);
            SetDrMd(win->RPort, JAM2);
        }
        if (a & A_ULINE) {
            Move(win->RPort, s * cw, term_top() + y * chh + base + 1);
            Draw(win->RPort, x * cw - 1, term_top() + y * chh + base + 1);
        }
    }
}

/* draw cells [x0, x1) of screen row y (not while scrolled back) */
static void draw_run(Tab *t, int y, int x0, int x1)
{
    if (!t || t != active() || !win || t->view) return;
    draw_cells(y, t->ch + y * cols, t->at + y * cols, x0, x1);
}

/* the line shown at display row y: scrollback or screen; its width */
static int disp_row(Tab *t, int y, UBYTE **c, UWORD **a)
{
    int v = t->hcount - t->view + y;
    if (v < t->hcount) {
        int idx = (t->hhead - t->hcount + v + t->hlines) % t->hlines;
        *c = t->hch + idx * t->hw;
        *a = t->hat + idx * t->hw;
        return t->hw < cols ? t->hw : cols;
    }
    v -= t->hcount;
    *c = t->ch + v * cols;
    *a = t->at + v * cols;
    return cols;
}

static void draw_cursor(Tab *t)
{
    int x, y;
    if (!t || t != active() || !t->cursor_on || !win || t->view) return;
    x = t->cx < cols ? t->cx : cols - 1;
    y = t->cy;
    SetDrMd(win->RPort, COMPLEMENT);
    SetAPen(win->RPort, pen_hi);
    RectFill(win->RPort, x * cw, term_top() + y * chh, x * cw + cw - 1, term_top() + y * chh + chh - 1);
    SetDrMd(win->RPort, JAM2);
}

static void redraw_tab(Tab *t)
{
    int y;
    if (!t || !win) return;
    SetAPen(win->RPort, pen_back);
    RectFill(win->RPort, 0, term_top(), win->GZZWidth - 1, win->GZZHeight - 1);
    for (y = 0; y < rows; y++) {
        UBYTE *c;
        UWORD *a;
        int w = disp_row(t, y, &c, &a);
        draw_cells(y, c, a, 0, w);
    }
    draw_cursor(t);
}

/* ------------------------------------------------------------------ */
/* scrollback                                                           */

/* keep screen row y, which is about to scroll off the top */
static void hist_push(Tab *t, int y)
{
    int i, w;
    UBYTE *c;
    UWORD *a;
    if (!t->hch) {
        if (!sb_lines || t->hfail) return;
        t->hw = cols;
        t->hlines = sb_lines;
        t->hch = AllocVec(t->hlines * t->hw, MEMF_ANY);
        t->hat = AllocVec(t->hlines * t->hw * sizeof(UWORD), MEMF_ANY);
        if (!t->hch || !t->hat) {
            if (t->hch) FreeVec(t->hch);
            if (t->hat) FreeVec(t->hat);
            t->hch = NULL;
            t->hat = NULL;
            t->hfail = 1;
            return;
        }
    }
    c = t->hch + t->hhead * t->hw;
    a = t->hat + t->hhead * t->hw;
    w = cols < t->hw ? cols : t->hw;
    memcpy(c, t->ch + y * cols, w);
    memcpy(a, t->at + y * cols, w * sizeof(UWORD));
    for (i = w; i < t->hw; i++) {
        c[i] = ' ';
        a[i] = def_attr;
    }
    t->hhead = (t->hhead + 1) % t->hlines;
    if (t->hcount < t->hlines) t->hcount++;
    /* scrolled back: the view stays on the same lines */
    if (t->view && t->view < t->hcount) t->view++;
    if (t == active()) sb_dirty = 1;
}

/* scroll the view back (n > 0) or forward (n < 0) */
static void scroll_view(Tab *t, int n)
{
    int v;
    if (!t) return;
    v = t->view + n;
    if (v > t->hcount) v = t->hcount;
    if (v < 0) v = 0;
    if (v == t->view) return;
    n = v - t->view;
    t->view = v;
    sb_dirty = 1;
    if (t != active() || !win) return;
    if (n < rows && -n < rows) {
        /* move what is shown and draw only the lines that come in */
        int y, y0 = n > 0 ? 0 : rows + n, y1 = n > 0 ? n : rows;
        SetBPen(win->RPort, pen_back);
        ScrollRaster(win->RPort, 0, -n * chh, 0, term_top(), cols * cw - 1, term_top() + rows * chh - 1);
        for (y = y0; y < y1; y++) {
            UBYTE *c;
            UWORD *a;
            int w = disp_row(t, y, &c, &a);
            draw_cells(y, c, a, 0, w);
        }
        if (!v) draw_cursor(t);
    } else
        redraw_tab(t);
}

/* show the scroller for the active tab */
static void update_scroller(void)
{
    Tab *t = active();
    LONG total, top;
    sb_dirty = 0;
    if (!win || !sb_prop) return;
    if (((struct Gadget *)sb_prop)->Flags & GFLG_SELECTED) {
        sb_dirty = 1;                       /* being dragged: later */
        return;
    }
    total = t ? t->hcount + rows : rows;
    top = t ? t->hcount - t->view : 0;
    sb_settop = top;
    SetGadgetAttrs((struct Gadget *)sb_prop, win, NULL, PGA_Total, total, PGA_Visible, rows,
                   PGA_Top, top, TAG_END);
}

/* the user moved the scroller */
static void scroller_moved(void)
{
    Tab *t = active();
    ULONG top = 0;
    if (!t || !sb_prop) return;
    GetAttr(PGA_Top, sb_prop, &top);
    if ((LONG)top == sb_settop) return;
    sb_settop = top;
    t->view = t->hcount - (int)top;
    if (t->view < 0) t->view = 0;
    if (t->view > t->hcount) t->view = t->hcount;
    redraw_tab(t);
}

static void make_scroller(void)
{
    ULONG sw = 18, sh = 10, uh = 11, dh = 11, top;
    ULONG size = (scr->Flags & SCREENHIRES) ? SYSISIZE_MEDRES : SYSISIZE_LOWRES;
    img_size = NewObject(NULL, (UBYTE *)"sysiclass", SYSIA_DrawInfo, (ULONG)dri, SYSIA_Which, SIZEIMAGE,
                         SYSIA_Size, size, TAG_END);
    img_up = NewObject(NULL, (UBYTE *)"sysiclass", SYSIA_DrawInfo, (ULONG)dri, SYSIA_Which, UPIMAGE,
                       SYSIA_Size, size, TAG_END);
    img_down = NewObject(NULL, (UBYTE *)"sysiclass", SYSIA_DrawInfo, (ULONG)dri, SYSIA_Which, DOWNIMAGE,
                         SYSIA_Size, size, TAG_END);
    if (!img_size || !img_up || !img_down) return;
    GetAttr(IA_Width, img_size, &sw);
    GetAttr(IA_Height, img_size, &sh);
    GetAttr(IA_Height, img_up, &uh);
    GetAttr(IA_Height, img_down, &dh);
    top = scr->WBorTop + scr->Font->ta_YSize + 1;
    sb_prop = NewObject(NULL, (UBYTE *)"propgclass",
                        GA_RelRight, -((LONG)sw - 5), GA_Top, top + 1, GA_Width, sw - 8,
                        GA_RelHeight, -(LONG)(top + sh + uh + dh + 2), GA_RightBorder, TRUE, GA_GZZGadget, TRUE,
                        GA_RelVerify, TRUE, GA_Immediate, TRUE, GA_ID, 1,
                        PGA_Freedom, FREEVERT, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
                        PGA_Total, 1, PGA_Visible, 1, PGA_Top, 0,
                        ICA_TARGET, ICTARGET_IDCMP, TAG_END);
    sb_up = NewObject(NULL, (UBYTE *)"buttongclass", GA_Image, (ULONG)img_up,
                      GA_RelRight, 1 - (LONG)sw, GA_RelBottom, 1 - (LONG)(sh + uh + dh),
                      GA_RightBorder, TRUE, GA_GZZGadget, TRUE, GA_Previous, (ULONG)sb_prop,
                      GA_RelVerify, TRUE, GA_Immediate, TRUE, GA_ID, 2, TAG_END);
    sb_down = NewObject(NULL, (UBYTE *)"buttongclass", GA_Image, (ULONG)img_down,
                        GA_RelRight, 1 - (LONG)sw, GA_RelBottom, 1 - (LONG)(sh + dh),
                        GA_RightBorder, TRUE, GA_GZZGadget, TRUE, GA_Previous, (ULONG)sb_up,
                        GA_RelVerify, TRUE, GA_Immediate, TRUE, GA_ID, 3, TAG_END);
    if (!sb_prop || !sb_up || !sb_down) {
        if (sb_down) DisposeObject(sb_down);
        if (sb_up) DisposeObject(sb_up);
        if (sb_prop) DisposeObject(sb_prop);
        sb_prop = sb_up = sb_down = NULL;
    }
}

static void free_scroller(void)
{
    if (sb_down) DisposeObject(sb_down);
    if (sb_up) DisposeObject(sb_up);
    if (sb_prop) DisposeObject(sb_prop);
    if (img_down) DisposeObject(img_down);
    if (img_up) DisposeObject(img_up);
    if (img_size) DisposeObject(img_size);
    sb_prop = sb_up = sb_down = img_size = img_up = img_down = NULL;
}

static void draw_tabbar(void)
{
    int i, n = 0, x = 0, w, k;
    int W;
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    W = win->GZZWidth;
    set_pens(def_attr);
    for (i = 0; i < MAXTABS; i++)
        if (tabs[i].used && !tabs[i].hidden) n++;
    plus_w = cw * 3 + 4;
    SetAPen(rp, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
    RectFill(rp, 0, 0, W - 1, tabh - 1);
    if (n == 0) return;
    w = (W - plus_w) / n;
    k = 0;
    for (i = 0; i < MAXTABS; i++) {
        Tab *t = &tabs[i];
        int x0, x1, tw, maxc, len;
        char label[80];
        if (!t->used || t->hidden) continue;
        k++;
        x0 = x;
        x1 = x + w - 1;
        /* active tab: filled like the terminal, others raised */
        SetAPen(rp, i == cur ? pen_back : (dri ? dri->dri_Pens[BACKGROUNDPEN] : 0));
        RectFill(rp, x0, 1, x1, tabh - 1);
        SetAPen(rp, dri ? dri->dri_Pens[SHINEPEN] : 2);
        Move(rp, x0, tabh - 1);
        Draw(rp, x0, 1);
        Draw(rp, x1, 1);
        SetAPen(rp, dri ? dri->dri_Pens[SHADOWPEN] : 1);
        Draw(rp, x1, tabh - 1);
        if (i != cur) {
            Move(rp, x0, tabh - 1);
            Draw(rp, x1, tabh - 1);
        }
        /* close box "x" at the left, shortcut at the right */
        SetAPen(rp, i == cur ? pen_text : (dri ? dri->dri_Pens[TEXTPEN] : 1));
        SetBPen(rp, i == cur ? pen_back : (dri ? dri->dri_Pens[BACKGROUNDPEN] : 0));
        SetDrMd(rp, JAM2);
        Move(rp, x0 + 4, 3 + base);
        txt(rp, "x", 1);
        sprintf(label, "A%d", k);
        Move(rp, x1 - 4 - 2 * cw, 3 + base);
        txt(rp, label, 2);
        /* title, centred */
        maxc = (w - 8 * cw) / cw;
        len = (int)strlen(t->title);
        if (len > maxc) len = maxc > 0 ? maxc : 0;
        tw = len * cw;
        Move(rp, x0 + (w - tw) / 2, 3 + base);
        if (len) txt(rp, t->title, len);
        x += w;
    }
    /* "+" button */
    SetAPen(rp, dri ? dri->dri_Pens[SHINEPEN] : 2);
    Move(rp, x, tabh - 1);
    Draw(rp, x, 1);
    Draw(rp, W - 1, 1);
    SetAPen(rp, dri ? dri->dri_Pens[SHADOWPEN] : 1);
    Draw(rp, W - 1, tabh - 1);
    Draw(rp, x, tabh - 1);
    SetAPen(rp, dri ? dri->dri_Pens[TEXTPEN] : 1);
    SetBPen(rp, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
    Move(rp, x + (W - x - cw) / 2, 3 + base);
    txt(rp, "+", 1);
}

static void update_title(void)
{
    static char wt[80];
    Tab *t = active();
    if (!win) return;
    strcpy(wt, t ? t->title : "bash");
    SetWindowTitles(win, (UBYTE *)wt, (UBYTE *)"AmiBash Terminal");
}

/* ------------------------------------------------------------------ */
/* the terminal emulator                                                */

static void clear_cells(Tab *t, int from, int to)
{
    int i;
    for (i = from; i < to; i++) {
        t->ch[i] = ' ';
        t->at[i] = (UWORD)(A_DEFFG | (t->attr & (0x38 | A_DEFBG)));
    }
}

static void scroll_up(Tab *t, int n)
{
    int k;
    if (n <= 0) return;
    if (n > rows) n = rows;
    for (k = 0; k < n; k++) hist_push(t, k);
    memmove(t->ch, t->ch + n * cols, (rows - n) * cols);
    memmove(t->at, t->at + n * cols, (rows - n) * cols * sizeof(UWORD));
    clear_cells(t, (rows - n) * cols, rows * cols);
    if (t == active() && win && !t->view) {
        SetBPen(win->RPort, pen_bg(t->attr));
        ScrollRaster(win->RPort, 0, n * chh, 0, term_top(), cols * cw - 1, term_top() + rows * chh - 1);
        if (win->GZZWidth > cols * cw || 1) {
            /* ScrollRaster fills with BPen; repaint the new lines */
            int y;
            for (y = rows - n; y < rows; y++) draw_run(t, y, 0, cols);
        }
    }
}

static void newline(Tab *t)
{
    t->cx = 0;
    t->wrap = 0;
    if (t->cy + 1 >= rows) scroll_up(t, 1);
    else t->cy++;
}

static void put_char(Tab *t, UBYTE c)
{
    if (t->wrap) newline(t);
    t->ch[t->cy * cols + t->cx] = c;
    t->at[t->cy * cols + t->cx] = t->attr;
    draw_run(t, t->cy, t->cx, t->cx + 1);
    if (t->cx + 1 >= cols) t->wrap = 1;
    else t->cx++;
}

static void queue_input(Tab *t, const char *s, int n);

static int par(Tab *t, int i, int def)
{
    /* i-th numeric parameter of the CSI sequence in t->eb */
    const char *p = t->eb;
    int k = 0, v = -1;
    while (*p && !(*p >= '0' && *p <= '9') && *p != ';') p++;
    for (;;) {
        if (k == i) {
            if (*p >= '0' && *p <= '9') {
                v = 0;
                while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            }
            return v < 0 ? def : v;
        }
        while (*p && *p != ';') p++;
        if (!*p) return def;
        p++;
        k++;
    }
}

static void sgr(Tab *t)
{
    int i, n = 1;
    const char *p;
    for (p = t->eb; *p; p++)
        if (*p == ';') n++;
    for (i = 0; i < n; i++) {
        int v = par(t, i, 0);
        if (v == 0) t->attr = def_attr;
        else if (v == 1) t->attr |= A_BOLD;
        else if (v == 3) t->attr |= A_ITAL;
        else if (v == 4) t->attr |= A_ULINE;
        else if (v == 7) t->attr |= A_INV;
        else if (v == 22) t->attr &= ~A_BOLD;
        else if (v == 23) t->attr &= ~A_ITAL;
        else if (v == 24) t->attr &= ~A_ULINE;
        else if (v == 27) t->attr &= ~A_INV;
        else if (v >= 30 && v <= 37) t->attr = (UWORD)((t->attr & ~(7 | A_DEFFG)) | (v - 30));
        else if (v == 39) t->attr = (UWORD)((t->attr & ~7) | A_DEFFG);
        else if (v >= 40 && v <= 47) t->attr = (UWORD)((t->attr & ~(0x38 | A_DEFBG)) | ((v - 40) << 3));
        else if (v == 49) t->attr = (UWORD)((t->attr & ~0x38) | A_DEFBG);
        else if (v >= 90 && v <= 97) t->attr = (UWORD)((t->attr & ~(7 | A_DEFFG)) | (v - 90)) | A_BOLD;
    }
}

static void erase(Tab *t, int from, int to)
{
    int y;
    if (from >= to) return;
    clear_cells(t, from, to);
    for (y = from / cols; y <= (to - 1) / cols; y++) draw_run(t, y, 0, cols);
}

static void csi(Tab *t, char final)
{
    int n = par(t, 0, 1), i;
    int inter_space = strchr(t->eb, ' ') != NULL;
    switch (final) {
    case 'A': t->cy -= n; break;
    case 'B': t->cy += n; break;
    case 'C': t->cx += n; break;
    case 'D': t->cx -= n; break;
    case 'E': t->cy += n; t->cx = 0; break;
    case 'F': t->cy -= n; t->cx = 0; break;
    case 'G': t->cx = n - 1; break;
    case 'd': t->cy = n - 1; break;
    case 'H':
    case 'f':
        t->cy = par(t, 0, 1) - 1;
        t->cx = par(t, 1, 1) - 1;
        break;
    case 'J': {
        int m = par(t, 0, 0);
        int pos = t->cy * cols + (t->cx < cols ? t->cx : cols - 1);
        if (m == 0) erase(t, pos, rows * cols);
        else if (m == 1) erase(t, 0, pos + 1);
        else erase(t, 0, rows * cols);
        break;
    }
    case 'K': {
        int m = par(t, 0, 0);
        int x = t->cx < cols ? t->cx : cols - 1;
        if (m == 0) erase(t, t->cy * cols + x, (t->cy + 1) * cols);
        else if (m == 1) erase(t, t->cy * cols, t->cy * cols + x + 1);
        else erase(t, t->cy * cols, (t->cy + 1) * cols);
        break;
    }
    case 'P': {                          /* delete chars */
        int y = t->cy, x = t->cx, k;
        if (n > cols - x) n = cols - x;
        for (k = x; k + n < cols; k++) {
            t->ch[y * cols + k] = t->ch[y * cols + k + n];
            t->at[y * cols + k] = t->at[y * cols + k + n];
        }
        clear_cells(t, y * cols + cols - n, (y + 1) * cols);
        draw_run(t, y, x, cols);
        break;
    }
    case '@': {                          /* insert blanks */
        int y = t->cy, x = t->cx, k;
        if (n > cols - x) n = cols - x;
        for (k = cols - 1; k >= x + n; k--) {
            t->ch[y * cols + k] = t->ch[y * cols + k - n];
            t->at[y * cols + k] = t->at[y * cols + k - n];
        }
        clear_cells(t, y * cols + x, y * cols + x + n);
        draw_run(t, y, x, cols);
        break;
    }
    case 'L':                            /* insert lines */
    case 'M': {                          /* delete lines */
        int y = t->cy, k;
        if (n > rows - y) n = rows - y;
        if (final == 'M') {
            memmove(t->ch + y * cols, t->ch + (y + n) * cols, (rows - y - n) * cols);
            memmove(t->at + y * cols, t->at + (y + n) * cols, (rows - y - n) * cols * sizeof(UWORD));
            clear_cells(t, (rows - n) * cols, rows * cols);
        } else {
            memmove(t->ch + (y + n) * cols, t->ch + y * cols, (rows - y - n) * cols);
            memmove(t->at + (y + n) * cols, t->at + y * cols, (rows - y - n) * cols * sizeof(UWORD));
            clear_cells(t, y * cols, (y + n) * cols);
        }
        for (k = y; k < rows; k++) draw_run(t, k, 0, cols);
        break;
    }
    case 'S': scroll_up(t, n); break;
    case 'm': sgr(t); break;
    case 's': t->sx = t->cx; t->sy = t->cy; break;
    case 'u': t->cx = t->sx; t->cy = t->sy; break;
    case 'n':
        if (par(t, 0, 0) == 6) {
            char r[32];
            sprintf(r, "\233%d;%dR", t->cy + 1, t->cx + 1);
            queue_input(t, r, (int)strlen(r));
        }
        break;
    case 'q':
        if (inter_space) {
            /* Amiga window status request -> bounds report */
            char r[40];
            sprintf(r, "\2331;1;%d;%d r", rows, cols);
            queue_input(t, r, (int)strlen(r));
        }
        break;
    case 'p':
        if (inter_space) t->cursor_on = strchr(t->eb, '0') == NULL;
        break;
    case 'h':
    case 'l':
        if (strcmp(t->eb, "?25") == 0) t->cursor_on = final == 'h';
        break;
    default:
        break;
    }
    (void)i;
    if (t->cx < 0) t->cx = 0;
    if (t->cy < 0) t->cy = 0;
    if (t->cx >= cols) t->cx = cols - 1;
    if (t->cy >= rows) t->cy = rows - 1;
    t->wrap = 0;
}

static void term_write(Tab *t, const UBYTE *s, long n)
{
    long i;
    if (t != active()) {
        /* still keep the buffer up to date for inactive tabs */
    }
    if (t == active()) draw_cursor(t);          /* hide (complement) */
    for (i = 0; i < n; i++) {
        UBYTE c = s[i];
        if (t->esc == 1) {
            if (c == '[') {
                t->esc = 2;
                t->el = 0;
                t->eb[0] = 0;
            } else if (c == ']') {
                t->esc = 3;
                t->el = 0;
                t->eb[0] = 0;
            } else {
                if (c == 'c') {                  /* reset */
                    t->attr = def_attr;
                    erase(t, 0, rows * cols);
                    t->cx = t->cy = 0;
                } else if (c == '7') {
                    t->sx = t->cx;
                    t->sy = t->cy;
                } else if (c == '8') {
                    t->cx = t->sx;
                    t->cy = t->sy;
                } else if (c == 'D') {
                    newline(t);
                }
                t->esc = 0;
            }
            continue;
        }
        if (t->esc == 2) {
            if (c >= 0x40 && c <= 0x7E) {
                t->eb[t->el] = 0;
                csi(t, (char)c);
                t->esc = 0;
            } else if (t->el < (int)sizeof(t->eb) - 1) {
                t->eb[t->el++] = (char)c;
                t->eb[t->el] = 0;
            }
            continue;
        }
        if (t->esc == 3) {
            /* OSC: ESC ] 0 ; title BEL  (or ESC \) */
            if (c == 7 || c == 0x9c || (c == '\\' && t->el && t->eb[t->el - 1] == 27)) {
                char *semi;
                if (c == '\\') t->el--;
                t->eb[t->el] = 0;
                semi = strchr(t->eb, ';');
                if (semi && (t->eb[0] == '0' || t->eb[0] == '2')) {
                    strncpy(t->title, semi + 1, sizeof(t->title) - 1);
                    t->title[sizeof(t->title) - 1] = 0;
                    draw_tabbar();
                    if (t == active()) update_title();
                }
                t->esc = 0;
            } else if (t->el < (int)sizeof(t->eb) - 1) {
                t->eb[t->el++] = (char)c;
            }
            continue;
        }
        switch (c) {
        case 27:
            t->esc = 1;
            break;
        case 0x9b:
            t->esc = 2;
            t->el = 0;
            t->eb[0] = 0;
            break;
        case '\n':
            newline(t);
            break;
        case '\r':
            t->cx = 0;
            t->wrap = 0;
            break;
        case '\b':
            if (t->cx > 0) t->cx--;
            t->wrap = 0;
            break;
        case '\t':
            t->cx = (t->cx + 8) & ~7;
            if (t->cx >= cols) t->cx = cols - 1;
            break;
        case 7:
            if (win) DisplayBeep(win->WScreen);
            break;
        case 12:                             /* form feed: clear */
            erase(t, 0, rows * cols);
            t->cx = t->cy = 0;
            t->wrap = 0;
            break;
        case 11:
            if (t->cy > 0) t->cy--;
            break;
        default:
            if (c >= 32 && c != 127 && !(c >= 0x80 && c < 0xA0)) put_char(t, c);
        }
    }
    if (t == active()) draw_cursor(t);          /* show */
}

/* ------------------------------------------------------------------ */
/* the console handler side                                             */

static void reply(struct DosPacket *dp, LONG r1, LONG r2)
{
    ReplyPkt(dp, r1, r2);
}

static int q_avail(Tab *t)
{
    return (t->qt - t->qh + INQ) % INQ;
}

static void serve_reads(Tab *t);

static void queue_input(Tab *t, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int nt = (t->qt + 1) % INQ;
        if (nt == t->qh) break;              /* full: drop */
        t->inq[t->qt] = s[i];
        t->qt = nt;
    }
    serve_reads(t);
}

static void serve_reads(Tab *t)
{
    /* answer WaitForChar() first */
    while (t->waits && (q_avail(t) || t->eof || t->closing)) {
        PktNode *p = t->waits;
        t->waits = p->next;
        reply(p->dp, q_avail(t) || t->closing ? DOSTRUE : DOSFALSE, 0);
        FreeVec(p);
    }
    while (t->reads) {
        PktNode *p = t->reads;
        struct DosPacket *dp = p->dp;
        UBYTE *buf = (UBYTE *)dp->dp_Arg2;
        LONG len = dp->dp_Arg3, got = 0;
        if (q_avail(t) == 0) {
            if (t->eof || t->closing) {
                t->eof = 0;
                t->reads = p->next;
                reply(dp, 0, 0);
                FreeVec(p);
                continue;
            }
            break;
        }
        if (t->raw) {
            while (got < len && q_avail(t)) {
                buf[got++] = (UBYTE)t->inq[t->qh];
                t->qh = (t->qh + 1) % INQ;
            }
        } else {
            /* cooked: deliver up to and including a newline */
            while (got < len && q_avail(t)) {
                char c = t->inq[t->qh];
                buf[got++] = (UBYTE)c;
                t->qh = (t->qh + 1) % INQ;
                if (c == '\n') break;
            }
        }
        t->reads = p->next;
        reply(dp, got, 0);
        FreeVec(p);
    }
}

static void add_pkt(PktNode **list, struct DosPacket *dp, long deadline)
{
    PktNode *p = AllocVec(sizeof(PktNode), MEMF_PUBLIC | MEMF_CLEAR), **pp;
    if (!p) {
        reply(dp, DOSFALSE, ERROR_NO_FREE_STORE);
        return;
    }
    p->dp = dp;
    p->deadline = deadline;
    for (pp = list; *pp; pp = &(*pp)->next)
        ;
    *pp = p;
}

static void start_timer(void)
{
    if (timer_busy || !treq) return;
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = 0;
    treq->tr_time.tv_micro = 100000;
    SendIO((struct IORequest *)treq);
    timer_busy = 1;
}

static void expire_waits(void)
{
    int i;
    long nowt = now_ticks();
    int any = 0;
    for (i = 0; i < MAXTABS; i++) {
        Tab *t = &tabs[i];
        PktNode **pp = &t->waits;
        if (!t->used) continue;
        while (*pp) {
            PktNode *p = *pp;
            if (nowt >= p->deadline || nowt < p->deadline - 3000000L) {
                *pp = p->next;
                reply(p->dp, DOSFALSE, 0);
                FreeVec(p);
            } else {
                pp = &p->next;
                any = 1;
            }
        }
    }
    if (any) start_timer();
}

static void tab_free(Tab *t);
static void do_item(int item);
static void zoom(int dir);
static void font_requester(void);
static void tt_requester(void);
static void drop_tt(void);

static void handle_packet(Tab *t, struct DosPacket *dp)
{
    dbg("tab %ld packet %ld arg1 %ld", (LONG)(t - tabs), dp->dp_Type, dp->dp_Arg1);
    switch (dp->dp_Type) {
    case ACTION_FINDINPUT:
    case ACTION_FINDOUTPUT:
    case ACTION_FINDUPDATE: {
        struct FileHandle *fh = (struct FileHandle *)BADDR(dp->dp_Arg1);
        fh->fh_Port = (struct MsgPort *)DOSTRUE;   /* interactive */
        fh->fh_Arg1 = (LONG)t;
        t->opencount++;
        reply(dp, DOSTRUE, 0);
        break;
    }
    case ACTION_END:
        t->opencount--;
        reply(dp, DOSTRUE, 0);
        break;
    case ACTION_READ:
        if (dp->dp_Port && dp->dp_Port->mp_SigTask) t->reader = (struct Task *)dp->dp_Port->mp_SigTask;
        add_pkt(&t->reads, dp, 0);
        serve_reads(t);
        break;
    case ACTION_WRITE:
        if (!t->closing) term_write(t, (const UBYTE *)dp->dp_Arg2, dp->dp_Arg3);
        reply(dp, dp->dp_Arg3, 0);
        break;
    case ACTION_SCREEN_MODE:
        t->raw = dp->dp_Arg1 != 0;
        reply(dp, DOSTRUE, 0);
        break;
    case ACTION_WAIT_CHAR:
        if (q_avail(t) || t->closing) {
            /* a closing tab is "ready": the Read() that follows gets EOF */
            reply(dp, q_avail(t) || t->closing ? DOSTRUE : DOSFALSE, 0);
        } else {
            long us = dp->dp_Arg1;
            add_pkt(&t->waits, dp, now_ticks() + us / 20000L);
            start_timer();
        }
        if (dp->dp_Port && dp->dp_Port->mp_SigTask) t->reader = (struct Task *)dp->dp_Port->mp_SigTask;
        break;
    case ACTION_DISK_INFO: {
        struct InfoData *id = (struct InfoData *)BADDR(dp->dp_Arg1);
        memset(id, 0, sizeof(*id));
        id->id_DiskType = ID_DOS_DISK;
        id->id_VolumeNode = (BPTR)win;
        id->id_InUse = 0;
        reply(dp, DOSTRUE, 0);
        break;
    }
    case ACTION_CHANGE_SIGNAL:
        if (dp->dp_Arg2) t->reader = (struct Task *)((struct MsgPort *)dp->dp_Arg2)->mp_SigTask;
        reply(dp, DOSTRUE, 0);
        break;
    case ACTION_FLUSH:
    case ACTION_NIL:
        reply(dp, DOSTRUE, 0);
        break;
    case ACTION_IS_FILESYSTEM:
        reply(dp, DOSFALSE, 0);
        break;
    default:
        reply(dp, DOSFALSE, ERROR_ACTION_NOT_KNOWN);
    }
}

/* ------------------------------------------------------------------ */
/* tabs                                                                 */

static void alloc_cells(Tab *t)
{
    t->ch = AllocVec(rows * cols, MEMF_PUBLIC);
    t->at = AllocVec(rows * cols * sizeof(UWORD), MEMF_PUBLIC);
    t->attr = def_attr;
    if (t->ch && t->at) clear_cells(t, 0, rows * cols);
}

static void select_tab(int i)
{
    if (i < 0 || i >= MAXTABS || !tabs[i].used || tabs[i].hidden) return;
    cur = i;
    draw_tabbar();
    redraw_tab(&tabs[i]);
    update_title();
    sb_dirty = 1;
}

static int visible_index(int k)
{
    /* k-th visible tab (0-based) -> slot */
    int i;
    for (i = 0; i < MAXTABS; i++)
        if (tabs[i].used && !tabs[i].hidden && k-- == 0) return i;
    return -1;
}

static void step_tab(int dir)
{
    int i, j;
    if (cur < 0) return;
    for (j = 1; j <= MAXTABS; j++) {
        i = (cur + dir * j + MAXTABS * 2) % MAXTABS;
        if (tabs[i].used && !tabs[i].hidden) {
            select_tab(i);
            return;
        }
    }
}

/* shell launch request, handled by a helper process */
static struct {
    BPTR in, out, path;
    struct MsgPort *port;
    Tab *tab;
    struct Task *parent;
    LONG result;
    volatile int busy;
} launch;

#ifdef AMIBASH_PURE
/* In the resident build a new process starts without our a4 (the data
 * base register): new_tab() leaves it in the process's tc_UserData. */
static void launcher_a4(void) = "\tmove.l\ta6,-(a7)\n\tmove.l\t4.w,a6\n\tsub.l\ta1,a1\n"
                               "\tjsr\t-294(a6)\n\tmove.l\td0,a0\n\tmove.l\t88(a0),a4\n"
                               "\tmove.l\t(a7)+,a6";
static APTR get_a4(void) = "\tmove.l\ta4,d0";
#else
#define launcher_a4()
#define get_a4() NULL
#endif

/* Start the tab's bash as a CLI process of its own.  The resident
 * build is re-entrant, so it runs the code this terminal is running
 * (no second copy in memory, no Shell, no resident list); otherwise the
 * program is loaded again for each tab. */
static void launcher(void)
{
    LONG r = -1;
    BPTR seg = 0;
    BOOL own = TRUE;
#ifdef AMIBASH_PURE
    seg = pure_seglist();
    if (seg) own = FALSE;
#endif
    if (!seg) seg = LoadSeg((STRPTR)NEARP(self));
    if (seg) {
        struct Process *p = CreateNewProcTags(
            NP_Seglist, seg, NP_FreeSeglist, own, NP_Name, (ULONG)"AmiBash",
            NP_Input, launch.in, NP_Output, launch.out, NP_CloseInput, TRUE, NP_CloseOutput, TRUE,
            NP_Cli, TRUE, NP_CommandName, (ULONG)"bash", NP_Arguments, (ULONG)"-i --amiterm\n",
            NP_Path, launch.path, NP_Error, launch.out, NP_CloseError, FALSE,
            NP_ConsoleTask, (ULONG)launch.port, NP_StackSize, 65536, TAG_END);
        if (p) {
            r = 0;
            launch.path = 0;                /* the new process owns it */
        } else if (own)
            UnLoadSeg(seg);
    }
    Forbid();
    launch.result = r;
    launch.busy = 2;                        /* done */
    Signal(launch.parent, SIGBREAKF_CTRL_F);
    Permit();
}

static void launcher_entry(void)
{
    launcher_a4();
    launcher();
}

/* a copy of this process's command path (Path), for a new shell */
static BPTR copy_path(void)
{
    struct CommandLineInterface *cli = Cli();
    BPTR head = 0, *link = &head;
    BPTR *pn;
    if (!cli) return 0;
    for (pn = (BPTR *)BADDR(cli->cli_CommandDir); pn; pn = (BPTR *)BADDR(pn[0])) {
        BPTR *n = AllocVec(2 * sizeof(BPTR), MEMF_PUBLIC | MEMF_CLEAR);
        if (!n) break;
        n[1] = DupLock(pn[1]);
        *link = MKBADDR(n);
        link = &n[0];
    }
    return head;
}

static void free_path(BPTR p)
{
    while (p) {
        BPTR *n = (BPTR *)BADDR(p);
        p = n[0];
        if (n[1]) UnLock(n[1]);
        FreeVec(n);
    }
}

/* the helper finished: check the result */
static void launch_done(void)
{
    Tab *t;
    if (launch.busy != 2) return;
    t = launch.tab;
    dbg("launch = %ld", launch.result, 0, 0);
    free_path(launch.path);                 /* if not handed over */
    launch.path = 0;
    if (launch.result != -1) launched++;
    else if (t->used) {
        FreeDosObject(DOS_FILEHANDLE, BADDR(launch.in));
        FreeDosObject(DOS_FILEHANDLE, BADDR(launch.out));
        t->opencount -= 2;
    }
    launch.busy = 0;
}

static int new_tab(void)
{
    int i;
    Tab *t = NULL;
    struct FileHandle *in, *out;
    struct Process *proc;
    for (i = 0; i < MAXTABS; i++)
        if (!tabs[i].used) {
            t = &tabs[i];
            break;
        }
    dbg("new_tab: slot %ld quitting %ld busy %ld", t ? (LONG)(t - tabs) : -1L, quitting, launch.busy);
    if (!t || quitting) {
        if (win) DisplayBeep(win->WScreen);
        return -1;
    }
    memset(t, 0, sizeof(*t));
    t->port = CreateMsgPort();
    if (!t->port) return -1;
    t->used = 1;
    t->cursor_on = 1;
    strcpy(t->title, "bash");
    alloc_cells(t);
    if (!t->ch || !t->at) {
        DeleteMsgPort(t->port);
        if (t->ch) FreeVec(t->ch);
        if (t->at) FreeVec(t->at);
        t->used = 0;
        return -1;
    }
    /* file handles pointing at this tab's handler port */
    in = AllocDosObject(DOS_FILEHANDLE, NULL);
    out = AllocDosObject(DOS_FILEHANDLE, NULL);
    if (!in || !out) {
        if (in) FreeDosObject(DOS_FILEHANDLE, in);
        if (out) FreeDosObject(DOS_FILEHANDLE, out);
        tab_free(t);
        return -1;
    }
    in->fh_Type = out->fh_Type = t->port;
    in->fh_Port = out->fh_Port = (struct MsgPort *)DOSTRUE;
    in->fh_Arg1 = out->fh_Arg1 = (LONG)t;
    t->opencount = 2;
    /* starting a process may talk to its console (this process), so
     * it is done by a helper process */
    if (launch.busy) {
        FreeDosObject(DOS_FILEHANDLE, in);
        FreeDosObject(DOS_FILEHANDLE, out);
        t->opencount = 0;
        tab_free(t);
        return -1;
    }
    launch.in = MKBADDR(in);
    launch.out = MKBADDR(out);
    launch.port = t->port;
    launch.path = copy_path();
    launch.tab = t;
    launch.parent = FindTask(NULL);
    launch.busy = 1;
    Forbid();               /* it must not run before tc_UserData is set */
    proc = CreateNewProcTags(NP_Entry, (ULONG)launcher_entry, NP_Name, (ULONG)"AmiBash launcher",
                             NP_StackSize, 16384, TAG_END);
    if (proc) proc->pr_Task.tc_UserData = get_a4();
    Permit();
    if (!proc) {
        launch.busy = 0;
        free_path(launch.path);
        launch.path = 0;
        FreeDosObject(DOS_FILEHANDLE, in);
        FreeDosObject(DOS_FILEHANDLE, out);
        t->opencount = 0;
        tab_free(t);
        return -1;
    }
    t->started = 1;
    ntabs_open++;
    select_tab(i);
    return i;
}

static void tab_free(Tab *t)
{
    PktNode *p, *n;
    for (p = t->reads; p; p = n) {
        n = p->next;
        reply(p->dp, 0, 0);
        FreeVec(p);
    }
    for (p = t->waits; p; p = n) {
        n = p->next;
        reply(p->dp, DOSFALSE, 0);
        FreeVec(p);
    }
    t->reads = t->waits = NULL;
    if (t->port) {
        struct Message *m;
        while ((m = GetMsg(t->port)) != NULL) reply((struct DosPacket *)m->mn_Node.ln_Name, DOSFALSE, ERROR_OBJECT_NOT_FOUND);
        DeleteMsgPort(t->port);
    }
    if (t->ch) FreeVec(t->ch);
    if (t->at) FreeVec(t->at);
    if (t->hch) FreeVec(t->hch);
    if (t->hat) FreeVec(t->hat);
    memset(t, 0, sizeof(*t));
}

/* hide a tab and let its shell finish (EOF on input, Ctrl-C) */
static void close_tab(int i)
{
    Tab *t = &tabs[i];
    if (!t->used || t->hidden) return;
    t->closing = 1;
    t->hidden = 1;
    ntabs_open--;
    if (t->reader) Signal(t->reader, SIGBREAKF_CTRL_C);
    serve_reads(t);
    if (cur == i) {
        cur = -1;
        step_tab(1);
        if (cur < 0) {
            int k;
            for (k = 0; k < MAXTABS; k++)
                if (tabs[k].used && !tabs[k].hidden) {
                    select_tab(k);
                    break;
                }
        }
    }
    draw_tabbar();
    if (cur >= 0) redraw_tab(&tabs[cur]);
}

/* ------------------------------------------------------------------ */
/* keyboard                                                             */

static void cooked_key(Tab *t, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        UBYTE c = (UBYTE)s[i];
        if (c == '\r' || c == '\n') {
            t->line[t->ll++] = '\n';
            term_write(t, (const UBYTE *)"\n", 1);
            queue_input(t, t->line, t->ll);
            t->ll = 0;
        } else if (c == 8) {
            if (t->ll > 0) {
                t->ll--;
                term_write(t, (const UBYTE *)"\b \b", 3);
            }
        } else if (c == 24 || c == 21) {        /* ^X ^U kill line */
            while (t->ll > 0) {
                t->ll--;
                term_write(t, (const UBYTE *)"\b \b", 3);
            }
        } else if (c == 28) {                   /* ^\ EOF */
            if (t->ll) {
                queue_input(t, t->line, t->ll);
                t->ll = 0;
            }
            t->eof = 1;
            serve_reads(t);
        } else if (c == 0x9b) {
            /* ignore cursor/function keys in cooked mode */
            while (i + 1 < n && !(s[i + 1] >= 0x40 && s[i + 1] <= 0x7e)) i++;
            i++;
        } else if (c >= 32 && t->ll < LINEMAX - 2) {
            t->line[t->ll++] = (char)c;
            term_write(t, &c, 1);
        }
    }
}

static void key_event(struct IntuiMessage *im)
{
    struct InputEvent ie;
    char buf[32];
    WORD n;
    Tab *t = active();
    UWORD q = im->Qualifier;
    if (im->Code & IECODE_UP_PREFIX) return;
    /* Right-Amiga shortcuts that are not menu items */
    /* mouse wheel (NewMouse codes) scrolls the view */
    if (im->Code == 0x7A || im->Code == 0x7B) {
        scroll_view(t, im->Code == 0x7A ? 3 : -3);
        return;
    }
    /* Shift-Up / Shift-Down: a page back / forward (Amiga+cursor keys
     * belong to Intuition: they move the mouse pointer) */
    if ((q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) && (im->Code == 0x4C || im->Code == 0x4D)) {
        scroll_view(t, im->Code == 0x4C ? rows - 1 : -(rows - 1));
        return;
    }
    if (q & IEQUALIFIER_RCOMMAND) {
        if (im->Code >= 0x01 && im->Code <= 0x09) {       /* 1..9 */
            int s = visible_index(im->Code - 1);
            if (s >= 0) select_tab(s);
        } else if (KeymapBase) {
            /* Amiga-= (the + key without Shift) also makes text bigger */
            memset(&ie, 0, sizeof(ie));
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = im->Code;
            ie.ie_EventAddress = (APTR) * ((ULONG *)im->IAddress);
            if (MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL) == 1 && buf[0] == '=') zoom(1);
        }
        return;
    }
    if (!KeymapBase) return;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = im->Code;
    ie.ie_EventAddress = (APTR) * ((ULONG *)im->IAddress);
    if (q & IEQUALIFIER_LCOMMAND) {
        /* Left-Amiga works like Right-Amiga for the tab shortcuts (it is
         * where Command is on a Mac keyboard under an emulator) */
        static const char keys[] = "tw\0[]\0+-0f\0\0q";
        n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL);
        if (n == 1 && buf[0]) {
            const char *k = memchr(keys, buf[0] == '=' ? '+' : buf[0] | 0x20, sizeof(keys) - 1);
            if (k) do_item((int)(k - keys));
            else if (buf[0] >= '1' && buf[0] <= '9') {
                int s = visible_index(buf[0] - '1');
                if (s >= 0) select_tab(s);
            }
        }
        return;
    }
    if (!t) return;
    ie.ie_Qualifier = q;
    n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL);
    if (n <= 0) return;
    if (t->view) scroll_view(t, -t->view);      /* typing: back to the bottom */
    if (n == 1 && buf[0] == 3) {
        /* Ctrl-C: break signal; raw-mode programs also get the char */
        if (t->raw) queue_input(t, buf, 1);
        else if (t->reader) Signal(t->reader, SIGBREAKF_CTRL_C);
        return;
    }
    if (n == 1 && (buf[0] == 4 || buf[0] == 5 || buf[0] == 6) && (q & IEQUALIFIER_CONTROL) == 0) {
        /* nothing special */
    }
    if (t->raw) queue_input(t, buf, n);
    else cooked_key(t, buf, n);
}

/* ------------------------------------------------------------------ */
/* window / layout                                                      */

static void layout(void)
{
    int i, ncols, nrows;
    struct TextFont *f = win->RPort->Font;
    if (f != font) font = f;
    if (ttfont) {
        cw = tt_cw;
        chh = tt_chh;
        base = tt_base;
    } else {
        cw = font->tf_XSize;
        chh = font->tf_YSize;
        base = font->tf_Baseline;
    }
    tabh = chh + 6;
    ncols = win->GZZWidth / cw;
    nrows = (win->GZZHeight - tabh) / chh;
    if (ncols < 10) ncols = 10;
    if (nrows < 2) nrows = 2;
    if (ncols != cols || nrows != rows) {
        for (i = 0; i < MAXTABS; i++) {
            Tab *t = &tabs[i];
            UBYTE *nc;
            UWORD *na;
            int y, x, shift;
            if (!t->used || !t->ch) continue;
            nc = AllocVec(nrows * ncols, MEMF_PUBLIC);
            na = AllocVec(nrows * ncols * sizeof(UWORD), MEMF_PUBLIC);
            if (!nc || !na) {
                if (nc) FreeVec(nc);
                if (na) FreeVec(na);
                continue;
            }
            /* keep the bottom part of the screen that contains the cursor */
            shift = t->cy >= nrows ? t->cy - nrows + 1 : 0;
            for (y = 0; y < nrows; y++)
                for (x = 0; x < ncols; x++) {
                    int oy = y + shift;
                    if (oy < rows && x < cols) {
                        nc[y * ncols + x] = t->ch[oy * cols + x];
                        na[y * ncols + x] = t->at[oy * cols + x];
                    } else {
                        nc[y * ncols + x] = ' ';
                        na[y * ncols + x] = def_attr;
                    }
                }
            FreeVec(t->ch);
            FreeVec(t->at);
            t->ch = nc;
            t->at = na;
            t->cy -= shift;
            if (t->cx >= ncols) t->cx = ncols - 1;
        }
        cols = ncols;
        rows = nrows;
    }
    sb_dirty = 1;
    draw_tabbar();
    if (active()) redraw_tab(active());
    else {
        SetAPen(win->RPort, pen_back);
        RectFill(win->RPort, 0, tabh, win->GZZWidth - 1, win->GZZHeight - 1);
    }
}

static void click(int x, int y)
{
    int n = 0, i, w, k = 0;
    if (y >= tabh) return;
    for (i = 0; i < MAXTABS; i++)
        if (tabs[i].used && !tabs[i].hidden) n++;
    if (x >= win->GZZWidth - plus_w || n == 0) {
        new_tab();
        return;
    }
    w = (win->GZZWidth - plus_w) / n;
    for (i = 0; i < MAXTABS; i++) {
        if (!tabs[i].used || tabs[i].hidden) continue;
        if (x >= k * w && x < (k + 1) * w) {
            if (x < k * w + 4 + cw * 2) close_tab(i);    /* the "x" */
            else select_tab(i);
            return;
        }
        k++;
    }
}

/* ------------------------------------------------------------------ */
/* fonts                                                                */

static char font_path[160];      /* "FONTS:AmiBashMono.font"           */
static UWORD font_sizes[40];     /* its designed sizes, ascending      */
static int nsizes;               /* 0: scalable (outline) font         */
static UWORD font_default;       /* size for Amiga-0                   */
static struct TextFont *ownfont; /* opened by us, closed on change     */

/* screens whose pixels are twice as tall as wide (non-interlaced) */
static int tall_pixels(void)
{
    struct DisplayInfo di;
    ULONG id = GetVPModeID(&scr->ViewPort);
    if (id == INVALID_ID || !GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di), DTAG_DISP, id)) return 0;
    return di.Resolution.y >= di.Resolution.x * 3 / 2;
}

/* read the sizes listed in a .font file; 0 if it cannot be read */
static int read_sizes(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    UWORD hdr[2];
    int i, n = 0;
    if (!fh) return 0;
    nsizes = 0;
    if (Read(fh, hdr, 4) == 4 && (hdr[0] == FCH_ID || hdr[0] == TFCH_ID || hdr[0] == OFCH_ID)) {
        n = 1;
        for (i = 0; i < hdr[1] && nsizes < 40; i++) {
            UBYTE e[260];
            UWORD y;
            int k;
            if (Read(fh, e, sizeof(e)) != sizeof(e)) break;
            y = (UWORD)((e[256] << 8) | e[257]);
            if ((e[259] & FPF_REMOVED) || !y) continue;
            for (k = nsizes; k > 0 && font_sizes[k - 1] > y; k--) font_sizes[k] = font_sizes[k - 1];
            if (k > 0 && font_sizes[k - 1] == y) {        /* duplicate */
                memmove(&font_sizes[k], &font_sizes[k + 1], (nsizes - k) * sizeof(UWORD));
                continue;
            }
            font_sizes[k] = y;
            nsizes++;
        }
    }
    Close(fh);
    return n;
}

/* find NAME.font in FONTS: or next to the program (PROGDIR:Fonts) */
static int find_font(const char *name)
{
    static const char *const dirs[] = {"FONTS:", "PROGDIR:Fonts/"};
    char p[160];
    int i;
    for (i = 0; i < 2; i++) {
        sprintf(p, "%s%s.font", dirs[i], name);
        if (read_sizes(p)) {
            strcpy(font_path, p);
            return 1;
        }
    }
    return 0;
}

/* after a font change: resize the window to keep the columns and rows
 * (like iTerm), as far as the screen allows */
static void fit_window(LONG ncw, LONG nch, int resize)
{
    LONG ww, wh, l, tp;
    if (!resize) {
        layout();
        return;
    }
    ww = win->Width - win->GZZWidth + cols * ncw;
    wh = win->Height - win->GZZHeight + (nch + 6) + rows * nch;
    if (ww > scr->Width) ww = scr->Width;
    if (wh > scr->Height - scr->BarHeight - 1) wh = scr->Height - scr->BarHeight - 1;
    l = win->LeftEdge;
    tp = win->TopEdge;
    if (l + ww > scr->Width) l = scr->Width - ww;
    if (tp + wh > scr->Height) tp = scr->Height - wh;
    ChangeWindowBox(win, l, tp, ww, wh);
    layout();                                     /* NEWSIZE follows if it changed */
}

static void save_font(const char *spec, int size)
{
    /* remembered for the next terminal (ENV: and ENVARC:) */
    char v[220];
    sprintf(v, "%.200s/%d", spec, size);
    SetVar((STRPTR)"AMIBASH_FONT", (STRPTR)NEARP(v), -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

static void drop_tt(void)
{
    if (!ttfont) return;
    if (win) tt_DoneRastPort(TTEngineBase, win->RPort);
    tt_CloseFont(TTEngineBase, ttfont);
    ttfont = NULL;
}

/* use font_path (a bitmap font) at this size */
static int set_font(UWORD size, int save, int resize)
{
    struct TextAttr ta;
    struct TextFont *f;
    char v[64];
    const char *fp;
    if (!font_path[0]) return 0;
    ta.ta_Name = (STRPTR)NEARP(font_path);
    ta.ta_YSize = size;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    /* without diskfont.library, designed sizes can still be loaded */
    f = DiskfontBase ? OpenDiskFont(&ta) : os_load_font(font_path, size);
    if (!f) return 0;
    if (f->tf_Flags & FPF_PROPORTIONAL) {       /* a terminal needs a grid */
        CloseFont(f);
        DisplayBeep(scr);
        return 0;
    }
    drop_tt();
    if (win) {
        SetFont(win->RPort, f);
        font = f;
        fit_window(f->tf_XSize, f->tf_YSize, resize);
    }
    if (ownfont) CloseFont(ownfont);
    ownfont = f;
    font = f;
    if (save) {
        fp = FilePart((STRPTR)NEARP(font_path));
        sprintf(v, "%.*s", (int)(strlen(fp) - 5), fp);
        save_font(v, size);
    }
    return 1;
}

/* a TrueType font: a .ttf/.otf file, or "tt:Family Name" from the
 * ttengine font database */
static int is_tt(const char *spec)
{
    size_t n = strlen(spec);
    if (!strncmp(spec, "tt:", 3)) return 1;
    return n > 4 && (Stricmp((STRPTR)spec + n - 4, (STRPTR)".ttf") == 0 ||
                     Stricmp((STRPTR)spec + n - 4, (STRPTR)".otf") == 0);
}

static APTR tt_open(const char *spec, int size)
{
    struct TagItem tg[4];
    STRPTR fam[3];
    if (!TTEngineBase) return NULL;
    if (!strncmp(spec, "tt:", 3)) {
        fam[0] = (STRPTR)spec + 3;
        fam[1] = (STRPTR)"monospaced";
        fam[2] = NULL;
        tg[0].ti_Tag = TT_FamilyTable;
        tg[0].ti_Data = (ULONG)fam;
    } else {
        tg[0].ti_Tag = TT_FontFile;
        tg[0].ti_Data = (ULONG)spec;
    }
    tg[1].ti_Tag = TT_FontSize;
    tg[1].ti_Data = (ULONG)size;
    tg[2].ti_Tag = TT_FontFixedWidth;
    tg[2].ti_Data = TRUE;
    tg[3].ti_Tag = TAG_END;
    return tt_OpenFontA(TTEngineBase, tg);
}

/* draw with a TrueType font from now on; anti-aliased on 15-bit and
 * deeper (RTG) screens */
static int use_tt(const char *spec, int size, int save, int resize)
{
    struct TagItem tg[5];
    ULONG w = 0, h = 0, b = 0;
    APTR f;
    if (!win || size < 4 || size > 200) return 0;
    f = tt_open(spec, size);
    if (!f) return 0;
    if (!tt_SetFont(TTEngineBase, win->RPort, f)) {
        tt_CloseFont(TTEngineBase, f);
        return 0;
    }
    tg[0].ti_Tag = TT_Window;
    tg[0].ti_Data = (ULONG)win;
    tg[1].ti_Tag = TT_Antialias;
    tg[1].ti_Data = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) >= 15 ? TT_Antialias_On : TT_Antialias_Off;
    tg[2].ti_Tag = TT_DiskFontMetrics;
    tg[2].ti_Data = TRUE;
    tg[3].ti_Tag = TT_Encoding;
    tg[3].ti_Data = TT_Encoding_ISO8859_1;
    tg[4].ti_Tag = TAG_END;
    tt_SetAttrsA(TTEngineBase, win->RPort, tg);
    tg[0].ti_Tag = TT_FontWidth;
    tg[0].ti_Data = (ULONG)&w;
    tg[1].ti_Tag = TT_FontHeight;
    tg[1].ti_Data = (ULONG)&h;
    tg[2].ti_Tag = TT_FontBaseline;
    tg[2].ti_Data = (ULONG)&b;
    tg[3].ti_Tag = TAG_END;
    tt_GetAttrsA(TTEngineBase, win->RPort, tg);
    if (!w || !h) {
        /* not usable on a grid: back to what was there */
        if (ttfont) tt_SetFont(TTEngineBase, win->RPort, ttfont);
        tt_CloseFont(TTEngineBase, f);
        return 0;
    }
    if (ttfont) tt_CloseFont(TTEngineBase, ttfont);
    ttfont = f;
    if (strcmp(spec, tt_spec)) sprintf(tt_spec, "%.199s", spec);
    tt_size = size;
    tt_cw = (int)w;
    tt_chh = (int)h;
    tt_base = (int)b;
    font = win->RPort->Font;                  /* unchanged: not a setfont */
    fit_window(w, h, resize);
    if (save) save_font(spec, size);
    return 1;
}

/* the designed size nearest below about 50 lines on the screen */
static UWORD pick_default(void)
{
    int i, want = scr->Height / 50;
    UWORD d = nsizes ? font_sizes[0] : (UWORD)want;
    for (i = 0; i < nsizes; i++)
        if (font_sizes[i] <= want) d = font_sizes[i];
    return d;
}

/* The font to start with: $AMIBASH_FONT ("name/size", a TrueType file
 * or "tt:family" too), else on an RTG screen with ttengine.library
 * DejaVu Sans Mono, else AmiBashMono (its tall-pixel cut on
 * non-interlaced screens), else the system font. */
static void choose_font(void)
{
    const char *fam = tall_pixels() ? "AmiBashMonoNL" : "AmiBashMono";
    char v[220], *sl;
    UWORD size = 0;
    int i;
    tt_default = scr->Height / 45;
    if (tt_default < 10) tt_default = 10;
    if (GetVar((STRPTR)"AMIBASH_FONT", (STRPTR)NEARP(v), sizeof(v), 0) > 0 && (sl = strrchr(v, '/')) != NULL) {
        *sl = 0;
        if (is_tt(v)) {
            if (use_tt(v, atoi(sl + 1), 0, 0)) return;
        } else if (find_font(v))
            size = (UWORD)atoi(sl + 1);
    }
    if (!size && TTEngineBase && GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) >= 15) {
        for (i = 0; i < 5; i++) {
            const char *p = i == 0 ? "PROGDIR:Fonts/DejaVuSansMono.ttf" :
                            i == 1 ? "FONTS:DejaVuSansMono.ttf" :
                            i == 2 ? "FONTS:_TrueType/DejaVuSansMono.ttf" :
                            i == 3 ? "FONTS:TrueType/DejaVuSansMono.ttf" : "tt:DejaVu Sans Mono";
            if (use_tt(p, tt_default, 0, 0)) return;
        }
    }
    if (!size && !find_font(fam)) return;         /* keep the system font */
    font_default = pick_default();
    if (set_font(size ? size : font_default, 0, 0)) return;
    if (find_font(fam)) {
        font_default = pick_default();
        set_font(font_default, 0, 0);
    }
}

/* Amiga-+ / Amiga-- / Amiga-0: the next designed size up or down */
static void zoom(int dir)
{
    UWORD cur_size = font ? font->tf_YSize : 8, size = cur_size;
    int i;
    if (ttfont) {
        char spec[200];
        int z = dir == 0 ? tt_default : tt_size + dir * (tt_size < 16 ? 1 : tt_size < 32 ? 2 : 4);
        strcpy(spec, tt_spec);
        if (z == tt_size || !use_tt(spec, z, 1, 1)) DisplayBeep(scr);
        return;
    }
    if (!font_path[0]) {
        /* system font: zoom within its family */
        struct TextFont *f = font;
        char p[64];
        if (!f) return;
        sprintf(p, "%.50s", f->tf_Message.mn_Node.ln_Name);
        if (strlen(p) > 5 && !strcmp(p + strlen(p) - 5, ".font")) p[strlen(p) - 5] = 0;
        if (!find_font(p)) return;
        font_default = cur_size;
    }
    if (dir == 0) size = font_default;
    else if (nsizes) {
        if (dir > 0) {
            for (i = 0; i < nsizes; i++)
                if (font_sizes[i] > cur_size) {
                    size = font_sizes[i];
                    break;
                }
        } else {
            for (i = nsizes - 1; i >= 0; i--)
                if (font_sizes[i] < cur_size) {
                    size = font_sizes[i];
                    break;
                }
        }
    } else {
        size = (UWORD)(cur_size + dir * (cur_size >= 16 ? 2 : 1));   /* outline font */
        if (size < 6) size = 6;
        if (size > 72) size = 72;
    }
    if (size == cur_size || !set_font(size, 1, 1)) DisplayBeep(scr);
}

/* "Font...": the ASL font requester, fixed-width fonts only */
static void font_requester(void)
{
    struct FontRequester *fr;
    char name[64];
    if (!AslBase || !win) {
        DisplayBeep(scr);
        return;
    }
    fr = AllocAslRequestTags(ASL_FontRequest, ASLFO_Window, (ULONG)win, ASLFO_TitleText,
                             (ULONG)"Terminal font", ASLFO_FixedWidthOnly, TRUE, ASLFO_MaxHeight, 64,
                             ASLFO_InitialName, (ULONG)(font ? font->tf_Message.mn_Node.ln_Name : "topaz.font"),
                             ASLFO_InitialSize, font ? font->tf_YSize : 8, TAG_END);
    if (!fr) return;
    if (AslRequest(fr, NULL)) {
        sprintf(name, "%.50s", fr->fo_Attr.ta_Name);
        if (strlen(name) > 5 && !strcmp(name + strlen(name) - 5, ".font")) name[strlen(name) - 5] = 0;
        if (find_font(name)) {
            font_default = fr->fo_Attr.ta_YSize;
            if (!set_font(fr->fo_Attr.ta_YSize, 1, 1)) DisplayBeep(scr);
        }
    }
    FreeAslRequest(fr);
}

/* "TrueType Font...": ttengine's requester, monospaced fonts only */
static void tt_requester(void)
{
    struct TagItem tg[6], *res;
    APTR rq;
    if (!TTEngineBase || !UtilityBase || !win || !(rq = tt_AllocRequest(TTEngineBase))) {
        DisplayBeep(scr);
        return;
    }
    tg[0].ti_Tag = TTRQ_Window;
    tg[0].ti_Data = (ULONG)win;
    tg[1].ti_Tag = TTRQ_TitleText;
    tg[1].ti_Data = (ULONG)"Terminal font (TrueType)";
    tg[2].ti_Tag = TTRQ_FixedWidthOnly;
    tg[2].ti_Data = TRUE;
    tg[3].ti_Tag = TTRQ_DoPreview;
    tg[3].ti_Data = TRUE;
    tg[4].ti_Tag = TTRQ_InitialSize;
    tg[4].ti_Data = ttfont ? tt_size : tt_default;
    tg[5].ti_Tag = TAG_END;
    res = tt_RequestA(TTEngineBase, rq, tg);
    if (res) {
        STRPTR file = (STRPTR)GetTagData(TT_FontFile, 0, res);
        STRPTR *fam = (STRPTR *)GetTagData(TT_FamilyTable, 0, res);
        int size = (int)GetTagData(TT_FontSize, tt_default, res);
        char spec[200];
        spec[0] = 0;
        if (file) sprintf(spec, "%.199s", (char *)file);
        else if (fam && fam[0]) sprintf(spec, "tt:%.190s", (char *)fam[0]);
        if (spec[0]) {
            tt_default = size;
            if (!use_tt(spec, size, 1, 1)) DisplayBeep(scr);
        }
    }
    tt_FreeRequest(TTEngineBase, rq);
}

static struct NewMenu nm[] = {
    {NM_TITLE, (STRPTR)"Terminal", 0, 0, 0, 0},
    {NM_ITEM, (STRPTR)"New Tab", (STRPTR)"T", 0, 0, 0},
    {NM_ITEM, (STRPTR)"Close Tab", (STRPTR)"W", 0, 0, 0},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR)"Previous Tab", (STRPTR)"[", 0, 0, 0},
    {NM_ITEM, (STRPTR)"Next Tab", (STRPTR)"]", 0, 0, 0},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR)"Bigger Text", (STRPTR)"+", 0, 0, 0},
    {NM_ITEM, (STRPTR)"Smaller Text", (STRPTR)"-", 0, 0, 0},
    {NM_ITEM, (STRPTR)"Default Size", (STRPTR)"0", 0, 0, 0},
    {NM_ITEM, (STRPTR)"Font...", (STRPTR)"F", 0, 0, 0},
    {NM_ITEM, (STRPTR)"TrueType Font...", 0, 0, 0, 0},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, 0},
    {NM_END, 0, 0, 0, 0, 0}
};

/* menu item number -> action */
static void do_item(int item)
{
    switch (item) {
    case 0: new_tab(); break;
    case 1: if (cur >= 0) close_tab(cur); break;
    case 3: step_tab(-1); break;
    case 4: step_tab(1); break;
    case 6: zoom(1); break;
    case 7: zoom(-1); break;
    case 8: zoom(0); break;
    case 9: font_requester(); break;
    case 10: tt_requester(); break;
    case 12: quitting = 1; break;
    }
}

static void menu_pick(UWORD code)
{
    while (code != MENUNULL) {
        struct MenuItem *it = ItemAddress(menus, code);
        do_item(ITEMNUM(code));
        if (!it) break;
        code = it->NextSelect;
    }
}

/* message from a shell: "new" / "next" / "prev" */
typedef struct TermMsg { struct Message m; char cmd[8]; LONG result; } TermMsg;

static void handle_pubmsg(void)
{
    TermMsg *m;
    while ((m = (TermMsg *)GetMsg(pubport)) != NULL) {
        dbg("pubmsg %s", (LONG)m->cmd, 0, 0);
        if (strcmp(m->cmd, "new") == 0) m->result = new_tab() >= 0 ? 0 : -1;
        else if (strcmp(m->cmd, "next") == 0) {
            step_tab(1);
            m->result = 0;
        } else if (strcmp(m->cmd, "prev") == 0) {
            step_tab(-1);
            m->result = 0;
        } else m->result = -1;
        ReplyMsg(&m->m);
    }
}

/* called from a shell running in a tab */
int term_request(const char *cmd)
{
    struct MsgPort *rp, *tp;
    TermMsg *m;
    LONG r = -1;
    rp = CreateMsgPort();
    m = AllocVec(sizeof(TermMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (!rp || !m) {
        if (rp) DeleteMsgPort(rp);
        if (m) FreeVec(m);
        return -1;
    }
    m->m.mn_ReplyPort = rp;
    m->m.mn_Length = sizeof(TermMsg);
    strncpy(m->cmd, cmd, sizeof(m->cmd) - 1);
    Forbid();
    tp = FindPort((STRPTR)"AmiBash.Terminal");
    if (tp) PutMsg(tp, &m->m);
    Permit();
    if (tp) {
        WaitPort(rp);
        GetMsg(rp);
        r = m->result;
    }
    FreeVec(m);
    DeleteMsgPort(rp);
    return tp ? (int)r : -2;
}

/* ------------------------------------------------------------------ */
/* main loop                                                            */

static int open_libs(void)
{
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37);
    if (!GadToolsBase) GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
    if (!KeymapBase) KeymapBase = OpenLibrary((STRPTR)"keymap.library", 37);
    if (!DiskfontBase) DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    if (!AslBase) AslBase = OpenLibrary((STRPTR)"asl.library", 38);     /* optional */
    if (!UtilityBase) UtilityBase = OpenLibrary((STRPTR)"utility.library", 37);
    if (!TTEngineBase) TTEngineBase = OpenLibrary((STRPTR)"ttengine.library", 7);  /* optional */
    return IntuitionBase && GfxBase && GadToolsBase && KeymapBase;
}

/* the terminal could not start: say why on the Shell, if there is one
 * (from Workbench the caller falls back to a console window) */
static int term_fail(const char *why)
{
    BPTR out = Output();
    if (out) {
        Write(out, (APTR)"bash: terminal: ", 16);
        Write(out, (APTR)why, strlen(why));
        Write(out, (APTR)"\n", 1);
    }
    if (tabs) FreeVec(tabs);
    tabs = NULL;
    return 20;
}

int term_main(const char *selfpath)
{
    struct TextAttr ta;
    int W, H, i;
    ULONG wsig, tsig, psig, all;
    {
        /* AMIBASH_TERMDEBUG=1 logs to T:AmiBash-term.log, or give a file */
        char b[108];
        if (GetVar((STRPTR)"AMIBASH_TERMDEBUG", (STRPTR)NEARP(b), sizeof(b), 0) >= 0)
            dbgfh = Open((STRPTR)(strchr(b, ':') ? b : "T:AmiBash-term.log"), MODE_NEWFILE);
    }
    if (!open_libs()) return term_fail("needs gadtools.library and keymap.library V37");
    strncpy(self, selfpath, sizeof(self) - 1);
    if (FindPort((STRPTR)"AmiBash.Terminal")) {
        /* a terminal is already running: just open a tab there */
        return term_request("new") == 0 ? 0 : 10;
    }
    tabs = AllocVec(MAXTABS * sizeof(Tab), MEMF_ANY | MEMF_CLEAR);
    if (!tabs) return term_fail("out of memory");
    scr = LockPubScreen(NULL);
    if (!scr) return term_fail("cannot lock the screen");
    dri = GetScreenDrawInfo(scr);
    vi = GetVisualInfo(scr, TAG_END);
    /* fixed-width system font (Prefs/Font "System default text") until
     * choose_font() finds a better one */
    ta.ta_Name = GfxBase->DefaultFont->tf_Message.mn_Node.ln_Name;
    ta.ta_YSize = GfxBase->DefaultFont->tf_YSize;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    font = OpenFont(&ta);
    if (!font) font = GfxBase->DefaultFont;
    /* dark theme by default (like most terminals): text pen 2 on pen 1 */
    def_attr = A_DEFFG | A_DEFBG;
    {
        char b[12];
        if (GetVar((STRPTR)"AMIBASH_SCROLLBACK", (STRPTR)b, sizeof(b), 0) > 0) {
            sb_lines = atoi(b);
            if (sb_lines < 0) sb_lines = 0;
            if (sb_lines > 20000) sb_lines = 20000;
        }
    }
    make_scroller();
    W = scr->Width - 40;
    H = (scr->Height - scr->BarHeight - 1) * 4 / 5;
    win = OpenWindowTags(NULL,
                         WA_Left, 20, WA_Top, scr->BarHeight + 1,
                         WA_Width, W, WA_Height, H,
                         WA_MinWidth, 200, WA_MinHeight, 80, WA_MaxWidth, -1, WA_MaxHeight, -1,
                         WA_Title, (ULONG)"bash", WA_ScreenTitle, (ULONG)"AmiBash Terminal",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_SizeGadget, TRUE, WA_SizeBRight, TRUE, WA_GimmeZeroZero, TRUE,
                         WA_Gadgets, (ULONG)sb_prop,
                         WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_PubScreen, (ULONG)scr,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS |
                                       IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW | IDCMP_GADGETDOWN |
                                       IDCMP_GADGETUP | IDCMP_IDCMPUPDATE,
                         TAG_END);
    UnlockPubScreen(NULL, scr);
    if (!win) {
        free_scroller();
        return term_fail("cannot open the window (out of memory?)");
    }
    SetFont(win->RPort, font);
    obtain_pens();
    choose_font();
    if (!TTEngineBase) nm[11].nm_Flags = NM_ITEMDISABLED;
    menus = CreateMenus(nm, TAG_END);
    if (menus && LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_END)) SetMenuStrip(win, menus);
    pubport = CreateMsgPort();
    if (pubport) {
        pubport->mp_Node.ln_Name = (char *)"AmiBash.Terminal";
        pubport->mp_Node.ln_Pri = 0;
        AddPort(pubport);
    }
    timerport = CreateMsgPort();
    if (timerport) {
        treq = (struct timerequest *)CreateIORequest(timerport, sizeof(struct timerequest));
        if (treq && OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0) != 0) {
            DeleteIORequest((struct IORequest *)treq);
            treq = NULL;
        }
    }
    /* tell shells they run inside the terminal (inherited local var) */
    SetVar((STRPTR)"AMIBASH_TERM", (STRPTR)"1", -1, GVF_LOCAL_ONLY);
    SetVar((STRPTR)"TERM_PROGRAM", (STRPTR)"AmiBash", -1, GVF_LOCAL_ONLY);
    SetVar((STRPTR)"AMIBASH_SELF", (STRPTR)NEARP(self), -1, GVF_LOCAL_ONLY);
    layout();
    new_tab();

    while (!quitting || 1) {
        int alive = 0, visible = 0;
        for (i = 0; i < MAXTABS; i++) {
            if (!tabs[i].used) continue;
            if (tabs[i].started && tabs[i].opencount <= 0) {
                int was = !tabs[i].hidden;
                tab_free(&tabs[i]);
                if (was) {
                    ntabs_open--;
                    if (cur == i) {
                        cur = -1;
                        step_tab(1);
                        if (cur < 0) {
                            int k;
                            for (k = 0; k < MAXTABS; k++)
                                if (tabs[k].used && !tabs[k].hidden) {
                                    select_tab(k);
                                    break;
                                }
                        }
                    }
                    if (win) {
                        draw_tabbar();
                        if (cur >= 0) redraw_tab(&tabs[cur]);
                        update_title();
                    }
                }
                continue;
            }
            alive++;
            if (!tabs[i].hidden) visible++;
        }
        if (quitting) {
            for (i = 0; i < MAXTABS; i++)
                if (tabs[i].used && !tabs[i].hidden) close_tab(i);
            visible = 0;
        }
        if (visible == 0 && win) {
            /* last tab gone: the window goes away; keep serving until
             * every shell has closed its handles */
            ClearMenuStrip(win);
            if (ttfont) drop_tt();
            release_pens();
            CloseWindow(win);
            win = NULL;
        }
        if (alive == 0) break;
        wsig = win ? 1UL << win->UserPort->mp_SigBit : 0;
        tsig = timerport ? 1UL << timerport->mp_SigBit : 0;
        psig = pubport ? 1UL << pubport->mp_SigBit : 0;
        all = wsig | tsig | psig | SIGBREAKF_CTRL_F;
        for (i = 0; i < MAXTABS; i++)
            if (tabs[i].used) all |= 1UL << tabs[i].port->mp_SigBit;
        if (Wait(all) & SIGBREAKF_CTRL_F) launch_done();
        for (i = 0; i < MAXTABS; i++) {
            struct Message *m;
            if (!tabs[i].used) continue;
            while (tabs[i].used && (m = GetMsg(tabs[i].port)) != NULL)
                handle_packet(&tabs[i], (struct DosPacket *)m->mn_Node.ln_Name);
        }
        if (timerport && GetMsg(timerport)) {
            timer_busy = 0;
            expire_waits();
            if (sb_held && win) {
                /* a scroller arrow held down: keep scrolling */
                if (((struct Gadget *)(sb_held > 0 ? sb_up : sb_down))->Flags & GFLG_SELECTED) {
                    scroll_view(active(), sb_held);
                    start_timer();
                } else
                    sb_held = 0;
            }
        }
        if (pubport) handle_pubmsg();
        if (win) {
            struct IntuiMessage *im;
            while (win && (im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
                ULONG cl = im->Class;
                UWORD code = im->Code;
                int mx = im->MouseX, my = im->MouseY;
                if (cl == IDCMP_RAWKEY) {
                    key_event(im);
                    ReplyMsg(&im->ExecMessage);
                    continue;
                }
                {
                    UWORD gid = (cl == IDCMP_GADGETDOWN || cl == IDCMP_GADGETUP)
                                    ? ((struct Gadget *)im->IAddress)->GadgetID : 0;
                    ReplyMsg(&im->ExecMessage);
                    if (cl == IDCMP_GADGETDOWN && (gid == 2 || gid == 3)) {
                        sb_held = gid == 2 ? 1 : -1;
                        scroll_view(active(), sb_held);
                        start_timer();              /* repeats while held */
                    } else if (cl == IDCMP_GADGETUP) {
                        sb_held = 0;
                        if (gid == 1) scroller_moved();
                    }
                }
                switch (cl) {
                case IDCMP_IDCMPUPDATE: scroller_moved(); break;
                case IDCMP_CLOSEWINDOW: quitting = 1; break;
                case IDCMP_NEWSIZE: layout(); break;
                case IDCMP_MENUPICK: menu_pick(code); break;
                case IDCMP_MOUSEBUTTONS:
                    if (code == SELECTDOWN) click(mx - win->BorderLeft, my - win->BorderTop);
                    break;
                }
            }
            if (win && win->RPort->Font != font) {
                /* setfont in a tab: that bitmap font replaces TrueType */
                if (ttfont) drop_tt();
                layout();
            }
            if (win && sb_dirty) update_scroller();
        }
    }
    if (pubport) {
        RemPort(pubport);
        DeleteMsgPort(pubport);
    }
    if (treq) {
        if (timer_busy) {
            AbortIO((struct IORequest *)treq);
            WaitIO((struct IORequest *)treq);
        }
        CloseDevice((struct IORequest *)treq);
        DeleteIORequest((struct IORequest *)treq);
    }
    if (timerport) DeleteMsgPort(timerport);
    if (menus) FreeMenus(menus);
    if (vi) FreeVisualInfo(vi);
    free_scroller();
    if (ownfont) CloseFont(ownfont);
    if (dri) FreeScreenDrawInfo(scr, dri);
    FreeVec(tabs);
    tabs = NULL;
    /* the last shells have closed their handles, so they are past our
     * code; give them a moment to finish exiting before it is unloaded */
    Delay(10);
    if (!launched) return term_fail("cannot start a shell (out of memory?)");
    return 0;
}
#endif
