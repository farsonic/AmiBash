/*
 * term.h - AmiBash Terminal: profiles and the preferences window
 * (term_prefs.c), used by the terminal itself (term_amiga.c).
 */
#ifndef AMIBASH_TERM_H
#define AMIBASH_TERM_H

#include <exec/types.h>
#include <intuition/intuition.h>

/* the colours of a profile, as 0xRRGGBB */
enum {
    C_FG, C_BG, C_BOLD, C_CURSOR, C_CURTEXT, C_SEL, C_SELTEXT,
    C_ANSI,                              /* 16 of them: 0-7 normal, 8-15 bright */
    NCOLORS = C_ANSI + 16
};

/* working directory of a new tab */
enum { DIR_HOME, DIR_PREVIOUS, DIR_FIXED };

typedef struct Profile {
    struct Profile *next;
    int id;                              /* unique for this session; tabs keep it */
    char name[32];
    ULONG rgb[NCOLORS];
    int brighten;                        /* bold ANSI colours use the bright ones */
    int boldcolor;                       /* bold default text in the Bold colour */
    int copysel;                         /* copy to the clipboard when selecting */
    char font[64];                       /* "name/size", or "" for automatic */
    int dirmode;                         /* DIR_* */
    char dir[160];
    char cmd[160];                       /* text sent to the shell at start */
    int scrollback;                      /* lines kept per tab */
} Profile;

extern Profile *profiles;                /* all of them, in order */
extern char default_profile[32];
extern int win_cols, win_rows;           /* the window's size; 0: automatic */
extern int win_left, win_top;            /* where it opens; -1: automatic */
void term_current_box(int *cols, int *rows, int *left, int *top);   /* now */

void     prof_load(void);                /* ENV:/ENVARC:AmiBash/terminal.prefs */
int      prof_save(void);                /* 0 ok */
Profile *prof_find(const char *name);
Profile *prof_by_id(int id);
Profile *prof_default(void);             /* never NULL once loaded */
Profile *prof_new(const char *name, const Profile *copy);
void     prof_delete(Profile *p);
void     prof_free_all(void);
extern const char *const color_names[NCOLORS];

/* the colour presets */
int  preset_count(void);
const char *preset_name(int i);
void preset_apply(int i, Profile *p);

/* the preferences window; it runs alongside the terminal window.
 * apply(p) is called when a profile changed (live) and when the window
 * closes; profiles_changed() when the list of profiles changed. */
int   prefs_open(struct Screen *scr, Profile *current, void (*apply)(Profile *), void (*profiles_changed)(void));
ULONG prefs_signal(void);                /* 0 when closed */
void  prefs_handle(void);
void  prefs_close(void);
int   prefs_is_open(void);

#endif
