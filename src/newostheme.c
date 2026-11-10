/*
 * newostheme.c - helper for programs that want NewOS themes / window transparency ("glass").
 * See newostheme.h and Transparency.txt. Free to use in your own programs.
 *
 * Your program must have these library bases open (as usual):
 *   GfxBase (graphics.library V39+), LayersBase (layers.library), IntuitionBase.
 * Picasso96API.library is opened here if present (needed for alpha / composite glass).
 *
 * AmiBash changes, marked "AmiBash" below:
 *  - the dither patterns are local arrays, not static ones: the resident (pure)
 *    build must not store the address of a data object (see NEARP in sh.h);
 *  - below() also skips a GimmeZeroZero window's border layer;
 *  - the dithered tint is drawn in JAM1.
 */
#include <exec/memory.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <string.h>
#include "newostheme.h"

static struct Library *ntP96;

/* P96 glue (vbcc) */
#define P96Base ntP96
struct RenderInfo; struct TrueColorInfo;
typedef ULONG RGBFTYPE;
#include <inline/Picasso96_protos.h>
struct NTRenderInfo { APTR Memory; WORD BytesPerRow; WORD pad; ULONG RGBFormat; };
#define NT_RGBFB_R8G8B8 2

BOOL NT_Init(void) { if (!ntP96) ntP96 = OpenLibrary((STRPTR)"Picasso96API.library", 2); return TRUE; }
void NT_Exit(void) { if (ntP96) CloseLibrary(ntP96); ntP96 = NULL; }

struct NewOSTheme *NT_Lock(void)
{
    struct NewOSTheme *nt;
    Forbid();
    nt = (struct NewOSTheme *)FindSemaphore((STRPTR)NEWOS_THEME_NAME);
    if (nt) ObtainSemaphoreShared(&nt->nt_Sem);
    Permit();
    if (nt && (!nt->nt_Valid || nt->nt_Version < 1)) { ReleaseSemaphore(&nt->nt_Sem); nt = NULL; }
    return nt;
}

void NT_Unlock(struct NewOSTheme *nt) { if (nt) ReleaseSemaphore(&nt->nt_Sem); }

static BOOL rgbScreen(struct Screen *s) { return (BOOL)(ntP96 && GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH) >= 15); }

LONG NT_GlassMode(void)
{
    struct NewOSTheme *nt = NT_Lock();
    LONG m = NTM_OFF;
    if (nt) {
        m = (LONG)nt->nt_Mode;
        if (m >= NTM_ALPHA && !rgbScreen(nt->nt_Screen)) m = NTM_DITHER;
        NT_Unlock(nt);
    }
    return m;
}

ULONG NT_Generation(void) { struct NewOSTheme *nt = NT_Lock(); ULONG g = nt ? nt->nt_Gen : 0; NT_Unlock(nt); return g; }

/* the windows below `win' (front to back).  AmiBash: a GimmeZeroZero
 * window has a border layer of its own (BorderRPort) behind its inner
 * one; it is the window, not something behind it (its hidden part would
 * be painted over what really is behind) */
static LONG below(struct Window *win, struct Layer **out, LONG max)
{
    struct Layer *l, *m1 = win->WLayer, *m2 = win->RPort->Layer;
    struct Layer *m3 = win->BorderRPort ? win->BorderRPort->Layer : NULL;
    LONG n = 0, seen = 0, need = 1;
    if (m2 != m1) need++;
    if (m3 && m3 != m1 && m3 != m2) need++;
    else m3 = NULL;
    for (l = win->WScreen->LayerInfo.top_layer; l; l = l->back) {
        if (l == m1 || l == m2 || l == m3) { seen++; continue; }
        if (seen >= need && n < max) out[n++] = l;
    }
    return n;
}

/* paint the hidden pixels of the windows below into an RGB buffer (ri) or a bitmap (dst), bottom first */
static void behind(struct Window *win, struct NTRenderInfo *ri, struct BitMap *dst, LONG sx, LONG sy, LONG w, LONG h)
{
    struct Layer *ls[64]; LONG n, i; struct RastPort crp;
    InitRastPort(&crp);
    LockLayerInfo(&win->WScreen->LayerInfo);
    n = below(win, ls, 64);
    for (i = n - 1; i >= 0; i--) {
        struct ClipRect *cr;
        for (cr = ls[i]->ClipRect; cr; cr = cr->Next) {
            LONG x0 = cr->bounds.MinX > sx ? cr->bounds.MinX : sx, y0 = cr->bounds.MinY > sy ? cr->bounds.MinY : sy;
            LONG x1 = cr->bounds.MaxX < sx + w - 1 ? cr->bounds.MaxX : sx + w - 1, y1 = cr->bounds.MaxY < sy + h - 1 ? cr->bounds.MaxY : sy + h - 1;
            LONG bx, by;
            if (x1 < x0 || y1 < y0 || !cr->obscured || !cr->BitMap) continue;
            bx = x0 - cr->bounds.MinX + (cr->bounds.MinX & 15); by = y0 - cr->bounds.MinY;
            if (ri) { crp.BitMap = cr->BitMap; p96ReadPixelArray((struct RenderInfo *)ri, x0 - sx, y0 - sy, &crp, bx, by, x1 - x0 + 1, y1 - y0 + 1); }
            else BltBitMap(cr->BitMap, bx, by, dst, x0 - sx, y0 - sy, x1 - x0 + 1, y1 - y0 + 1, 0xC0, 0xFF, NULL);
        }
    }
    UnlockLayerInfo(&win->WScreen->LayerInfo);
}

static void blur(UBYTE *buf, LONG w, LONG h, LONG r)
{
    UBYTE *tmp = AllocVec((w > h ? w : h) * 3, MEMF_ANY);
    LONG x, y, c, n = 2 * r + 1;
    if (!tmp) return;
    for (y = 0; y < h; y++) {
        UBYTE *row = buf + y * w * 3;
        for (c = 0; c < 3; c++) {
            LONG sum = 0, i;
            for (i = -r; i <= r; i++) sum += row[(i < 0 ? 0 : i >= w ? w - 1 : i) * 3 + c];
            for (x = 0; x < w; x++) { LONG a = x - r, b = x + r + 1; tmp[x * 3 + c] = (UBYTE)(sum / n); sum += row[(b >= w ? w - 1 : b) * 3 + c] - row[(a < 0 ? 0 : a) * 3 + c]; }
        }
        CopyMem(tmp, row, w * 3);
    }
    for (x = 0; x < w; x++) {
        for (c = 0; c < 3; c++) {
            LONG sum = 0, i;
            for (i = -r; i <= r; i++) sum += buf[((i < 0 ? 0 : i >= h ? h - 1 : i) * w + x) * 3 + c];
            for (y = 0; y < h; y++) { LONG a = y - r, b = y + r + 1; tmp[y * 3 + c] = (UBYTE)(sum / n); sum += buf[((b >= h ? h - 1 : b) * w + x) * 3 + c] - buf[((a < 0 ? 0 : a) * w + x) * 3 + c]; }
        }
        for (y = 0; y < h; y++) { buf[(y * w + x) * 3] = tmp[y * 3]; buf[(y * w + x) * 3 + 1] = tmp[y * 3 + 1]; buf[(y * w + x) * 3 + 2] = tmp[y * 3 + 2]; }
    }
    FreeVec(tmp);
}

BOOL NT_FillGlass(struct Window *win, LONG x0, LONG y0, LONG x1, LONG y1, ULONG tint, LONG opacity)
{
    struct NewOSTheme *nt;
    struct RastPort *rp = win->RPort;
    struct Screen *scr = win->WScreen;
    LONG w = x1 - x0 + 1, h = y1 - y0 + 1, sx, sy, mode, depth;
    BOOL done = FALSE;
    if (w < 1 || h < 1 || !rp->Layer) return FALSE;
    if (!(nt = NT_Lock())) return FALSE;
    if (nt->nt_Mode == NTM_OFF || scr != nt->nt_Screen) { NT_Unlock(nt); return FALSE; }
    if (opacity < 0) opacity = nt->nt_Opacity;
    sx = rp->Layer->bounds.MinX - rp->Layer->Scroll_X + x0;     /* where the rectangle is on the screen */
    sy = rp->Layer->bounds.MinY - rp->Layer->Scroll_Y + y0;
    depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    mode = (LONG)nt->nt_Mode;
    if (mode >= NTM_ALPHA && rgbScreen(scr)) {
        UBYTE *buf = AllocVec(w * h * 3, MEMF_ANY | MEMF_CLEAR);
        if (buf) {
            struct NTRenderInfo ri; LONG a = opacity * 256 / 100, i;
            UBYTE cr = (UBYTE)(tint >> 16), cg = (UBYTE)(tint >> 8), cb = (UBYTE)tint;
            ri.Memory = buf; ri.BytesPerRow = (WORD)(w * 3); ri.pad = 0; ri.RGBFormat = NT_RGBFB_R8G8B8;
            if (nt->nt_Backdrop) {
                struct RastPort wrp; LONG tx, ty, bw = nt->nt_BackdropW, bh = nt->nt_BackdropH;
                InitRastPort(&wrp); wrp.BitMap = nt->nt_Backdrop;
                for (ty = (sy / bh) * bh; ty < sy + h; ty += bh)
                    for (tx = (sx / bw) * bw; tx < sx + w; tx += bw) {
                        LONG ax = tx < sx ? sx : tx, ay = ty < sy ? sy : ty;
                        LONG bx = tx + bw > sx + w ? sx + w : tx + bw, by = ty + bh > sy + h ? sy + h : ty + bh;
                        if (bx > ax && by > ay) p96ReadPixelArray((struct RenderInfo *)&ri, ax - sx, ay - sy, &wrp, ax - tx, ay - ty, bx - ax, by - ay);
                    }
            }
            if (nt->nt_Source == NTS_BEHIND) behind(win, &ri, NULL, sx, sy, w, h);
            if (mode == NTM_COMPOSITE) { blur(buf, w, h, 6); blur(buf, w, h, 6); }
            for (i = 0; i < w * h; i++) {
                UBYTE *p = buf + i * 3;
                p[0] = (UBYTE)((cr * a + p[0] * (256 - a)) >> 8); p[1] = (UBYTE)((cg * a + p[1] * (256 - a)) >> 8); p[2] = (UBYTE)((cb * a + p[2] * (256 - a)) >> 8);
            }
            p96WritePixelArray((struct RenderInfo *)&ri, 0, 0, rp, x0, y0, w, h);
            FreeVec(buf);
            done = TRUE;
        }
    } else {                                                /* dithered glass on any screen */
        struct BitMap *tmp = AllocBitMap(w, h, depth, BMF_CLEAR | BMF_MINPLANES, scr->RastPort.BitMap);
        if (tmp) {
            /* AmiBash: local, see the top of the file */
            UWORD p25[4], p50[4], p75[4];
            struct RastPort trp; LONG pen;
            struct TagItem t[2];
            p25[0] = 0x8888; p25[1] = 0x0000; p25[2] = 0x2222; p25[3] = 0x0000;
            p50[0] = 0xAAAA; p50[1] = 0x5555; p50[2] = 0xAAAA; p50[3] = 0x5555;
            p75[0] = 0x7777; p75[1] = 0xFFFF; p75[2] = 0xDDDD; p75[3] = 0xFFFF;
            t[0].ti_Tag = OBP_Precision; t[0].ti_Data = PRECISION_GUI;
            t[1].ti_Tag = TAG_DONE; t[1].ti_Data = 0;
            if (nt->nt_Backdrop) {
                LONG tx, ty, bw = nt->nt_BackdropW, bh = nt->nt_BackdropH;
                for (ty = (sy / bh) * bh; ty < sy + h; ty += bh)
                    for (tx = (sx / bw) * bw; tx < sx + w; tx += bw) {
                        LONG ax = tx < sx ? sx : tx, ay = ty < sy ? sy : ty;
                        LONG bx = tx + bw > sx + w ? sx + w : tx + bw, by = ty + bh > sy + h ? sy + h : ty + bh;
                        if (bx > ax && by > ay) BltBitMap(nt->nt_Backdrop, ax - tx, ay - ty, tmp, ax - sx, ay - sy, bx - ax, by - ay, 0xC0, 0xFF, NULL);
                    }
            }
            if (nt->nt_Source == NTS_BEHIND) behind(win, NULL, tmp, sx, sy, w, h);
            InitRastPort(&trp); trp.BitMap = tmp;
            /* AmiBash: JAM1, so that only the pattern's 1 bits get the tint;
             * InitRastPort() leaves JAM2, which fills the 0 bits with BPen
             * too and covers all that is behind */
            SetDrMd(&trp, JAM1);
            pen = ObtainBestPenA(scr->ViewPort.ColorMap, ((tint >> 16) & 255) * 0x01010101UL, ((tint >> 8) & 255) * 0x01010101UL, (tint & 255) * 0x01010101UL, t);
            SetAPen(&trp, pen >= 0 ? pen : 0);
            SetAfPt(&trp, opacity < 55 ? p25 : opacity < 80 ? p50 : p75, 2);
            RectFill(&trp, 0, 0, w - 1, h - 1);
            SetAfPt(&trp, NULL, 0);
            if (pen >= 0) ReleasePen(scr->ViewPort.ColorMap, pen);
            WaitBlit();
            BltBitMapRastPort(tmp, 0, 0, rp, x0, y0, w, h, 0xC0);
            WaitBlit();
            FreeBitMap(tmp);
            done = TRUE;
        }
    }
    NT_Unlock(nt);
    return done;
}

ULONG NT_TextColor(struct Window *win, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG i, sum = 0;
    if (!rgbScreen(win->WScreen)) {
        struct NewOSTheme *nt = NT_Lock(); ULONG c = nt ? nt->nt_Colors[NTCOL_BG] : 0xAAAAAA; NT_Unlock(nt);
        sum = (((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8;
    } else {
        for (i = 0; i < 5; i++) {
            ULONG c = p96ReadPixel(win->RPort, x0 + (x1 - x0) * (i + 1) / 6, y0 + (y1 - y0) * ((i * 3) % 5 + 1) / 6);
            sum += (((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8;
        }
        sum /= 5;
    }
    return sum < 128 ? 0x00FFFFFF : 0x00101010;
}
