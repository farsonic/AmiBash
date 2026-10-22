/*
 * amiga_compat.h - included first in every OpenSSH source file for the
 * AmigaOS 3.x build (bebbo's gcc, libnix, libsocket).  It declares what
 * libnix's headers leave out; amiga_compat.c has the code.
 */
#ifndef AMIGA_COMPAT_H
#define AMIGA_COMPAT_H

#include <sys/types.h>

int wcwidth(wchar_t);
pid_t setsid(void);

/* AmigaDOS names (config_fix.sh keeps libnix's <paths.h> out) */
#define _PATH_DEVNULL	"NIL:"
#define _PATH_TTY	"*"

/* ppoll() on top of poll(): port-amiga-io.c */
#include <signal.h>
#include <poll.h>
#include <time.h>
int ppoll(struct pollfd *, nfds_t, const struct timespec *, const sigset_t *);

/* TIOCGWINSZ on a console: port-amiga-io.c */
int amiga_ioctl(int, unsigned long, ...);
#define ioctl amiga_ioctl

/* O_NONBLOCK on sockets; libnix's fcntl() fails there: port-amiga-io.c */
int amiga_fcntl(int, int, ...);
#define fcntl amiga_fcntl

#endif
