/*
 * newostheme.h - let your program use NewOS themes and window transparency ("glass").
 *
 * NewOSDrawer publishes a public SignalSemaphore named "NewOS.Theme" while it runs.
 * It describes what the machine can do (nt_Caps), what the user chose in NewOS
 * Preferences (nt_Mode, nt_Opacity, nt_Theme) and the theme colours.
 * The same information is written as text to ENV:NewOS/Theme on every change.
 *
 * Rules:
 *  - Offer a "transparency" option in your program only if NT_GlassMode() != NTM_OFF.
 *    (NewOS not running, the user switched glass off, or the screen cannot do it -> hide it.)
 *  - Never keep pointers from the structure after NT_Unlock().
 *  - Do not call NewOS (ports, ARexx) while you hold the lock.
 *  - To show through NewOS glass yourself, open your windows SMART_REFRESH
 *    (WA_SmartRefresh, TRUE): simple-refresh windows keep no hidden pixels.
 *
 * Free to use in your own programs.
 */
#ifndef NEWOS_THEME_H
#define NEWOS_THEME_H

#include <exec/semaphores.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>

#define NEWOS_THEME_NAME    "NewOS.Theme"
#define NEWOS_THEME_VERSION 1

/* nt_Caps: what this machine/screen can do */
#define NTC_RUNNING    0x0001   /* NewOS is running (always set in a valid structure)      */
#define NTC_DITHER     0x0002   /* dithered glass possible (any screen)                    */
#define NTC_RGB        0x0004   /* true-colour RTG screen (15/16/24/32 bit, Picasso96)     */
#define NTC_ALPHA      0x0008   /* real alpha blending possible                            */
#define NTC_COMPOSITE  0x0010   /* fast CPU (68040/68060/PiStorm): blurred "frosted" glass */
#define NTC_BACKDROP   0x0020   /* a Workbench backdrop picture is loaded (nt_Backdrop)    */

/* nt_Mode: transparency the user chose, already limited to what the screen can do */
#define NTM_OFF        0
#define NTM_DITHER     1
#define NTM_ALPHA      2
#define NTM_COMPOSITE  3

/* nt_Source */
#define NTS_DESKTOP    0        /* only the Workbench backdrop picture        */
#define NTS_BEHIND     1        /* the windows really behind (and the backdrop) */

/* nt_Theme */
#define NTT_CLASSIC    0
#define NTT_GRADIENT   1
#define NTT_FLAT       2
#define NTT_DARK       3

/* nt_Colors[] index, each 0x00RRGGBB */
#define NTCOL_BG       0
#define NTCOL_TEXT     1
#define NTCOL_LINE     2
#define NTCOL_ACCENT   3
#define NTCOL_SELTEXT  4
#define NTCOL_STRIPE   5
#define NTCOL_BARTOP   6
#define NTCOL_BARBOT   7
#define NTCOL_COUNT    8

struct NewOSTheme {
    struct SignalSemaphore nt_Sem;     /* ln_Name = NEWOS_THEME_NAME; lock SHARED to read      */
    UWORD   nt_Version;                /* NEWOS_THEME_VERSION                                  */
    UWORD   nt_Size;                   /* sizeof(struct NewOSTheme) of the publisher           */
    ULONG   nt_Valid;                  /* 0 once NewOS has quit (structure is left in memory)  */
    ULONG   nt_Caps;                   /* NTC_*                                                */
    ULONG   nt_Mode;                   /* NTM_*                                                */
    ULONG   nt_Gen;                    /* incremented on every change (theme, mode, backdrop)  */
    UBYTE   nt_Opacity;                /* 30..100 %                                            */
    UBYTE   nt_Theme;                  /* NTT_*                                                */
    UBYTE   nt_AdaptiveText;           /* user wants light/dark text chosen by the background  */
    UBYTE   nt_Source;                 /* NTS_*: what the glass shows (user setting)           */
    ULONG   nt_Colors[NTCOL_COUNT];    /* theme colours                                        */
    struct Screen *nt_Screen;          /* the screen this applies to (Workbench)               */
    struct BitMap *nt_Backdrop;        /* backdrop picture, friend of nt_Screen's bitmap, or NULL */
    LONG    nt_BackdropW, nt_BackdropH;/* tiled from the screen origin, like Workbench         */
    ULONG   nt_Reserved[8];
};

/* helper (newostheme.c) */
BOOL  NT_Init(void);                                   /* call once (opens Picasso96API if present) */
void  NT_Exit(void);
struct NewOSTheme *NT_Lock(void);                      /* NULL if NewOS is not running; else locked shared */
void  NT_Unlock(struct NewOSTheme *nt);
LONG  NT_GlassMode(void);                              /* NTM_*: what your program may offer/use right now */
ULONG NT_Generation(void);                             /* changes whenever you should repaint your glass    */

/* paint rectangle x0,y0..x1,y1 of win->RPort as glass: backdrop + the windows really behind
   + blur (composite) + tint at opacity (opacity < 0: the user's NewOS setting). Returns FALSE if nothing was done. */
BOOL  NT_FillGlass(struct Window *win, LONG x0, LONG y0, LONG x1, LONG y1, ULONG tintRGB, LONG opacity);

/* pick readable text over glass: returns 0x00FFFFFF or 0x00101010 for the area */
ULONG NT_TextColor(struct Window *win, LONG x0, LONG y0, LONG x1, LONG y1);

#endif
