/*
 * port-amiga-io.c - the console and the network together, for OpenSSH on
 * AmigaOS 3.x (libnix, libsocket):
 *
 *   poll()		sockets through the TCP/IP stack's WaitSelect(), a
 *			console through an ACTION_WAIT_CHAR packet whose reply
 *			signal WaitSelect() waits for as well
 *   tcgetattr()	raw mode is the console's SetMode(); libnix's own
 *   tcsetattr()	versions only hide the echo
 *   ioctl()		TIOCGWINSZ asks the console for its size
 *   readpassphrase()	reads a password from the console without echo
 *   read()		in raw mode, turns the Amiga console's keys (CSI
 *			sequences, Del) into what a Unix host expects
 *
 * It uses libnix's headers rather than OpenSSH's.  config_fix.sh copies it
 * to openbsd-compat/.
 */

#ifdef __amigaos__

#include <stdarg.h>
#include <stdio.h>		/* libnix: StdFileDes, _lx_fhfromfd() */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <socket.h>		/* libnix: SocketSettings, TCP_WaitSelect() */

#undef ioctl
int ioctl(int, unsigned long, ...);
#undef read
#undef fcntl

/* flags of readpassphrase(), as in openbsd-compat/readpassphrase.h */
#define RPP_ECHO_ON	0x01
#define RPP_REQUIRE_TTY	0x02
#define RPP_FORCELOWER	0x04
#define RPP_FORCEUPPER	0x08
#define RPP_SEVENBIT	0x10
#define RPP_STDIN	0x20

#define WAIT_US		1000000		/* one WAIT_CHAR packet at most this long */

/* the console of fd fd, or 0 */
static BPTR
con_of(int fd)
{
	StdFileDes *sfd = _lx_fhfromfd(fd);

	if (sfd == NULL || (sfd->lx_flags & LX_SOCKET) || sfd->lx_fx != NULL)
		return 0;
	return IsInteractive((BPTR)sfd->lx_fh) ? (BPTR)sfd->lx_fh : 0;
}

/*
 * The WAIT_CHAR packet.  A DOS packet can't be taken back once sent, so
 * one stays out between calls of poll() until its reply comes.
 */
static struct MsgPort *wport;
static struct StandardPacket *wpkt;
static BPTR wfh;			/* sent to this console, or 0 */
static int wready;			/* its reply said: a key is waiting */

/* keys translated by amiga_read() and not handed out yet */
static unsigned char kpend[1024];
static int kplen, kppos;

static void
wait_done(void)
{
	if (wfh != 0) {
		WaitPort(wport);
		GetMsg(wport);
		wfh = 0;
	}
	if (wpkt != NULL)
		FreeMem(wpkt, sizeof(*wpkt));
	if (wport != NULL)
		DeleteMsgPort(wport);
	wpkt = NULL;
	wport = NULL;
}

static int
wait_send(BPTR fh)
{
	struct FileHandle *h = BADDR(fh);

	if (wport == NULL) {
		if ((wport = CreateMsgPort()) == NULL ||
		    (wpkt = AllocMem(sizeof(*wpkt), MEMF_PUBLIC | MEMF_CLEAR)) == NULL)
			return -1;
		atexit(wait_done);
	}
	wpkt->sp_Msg.mn_Node.ln_Name = (char *)&wpkt->sp_Pkt;
	wpkt->sp_Msg.mn_ReplyPort = wport;
	wpkt->sp_Pkt.dp_Link = &wpkt->sp_Msg;
	wpkt->sp_Pkt.dp_Port = wport;
	wpkt->sp_Pkt.dp_Type = ACTION_WAIT_CHAR;
	wpkt->sp_Pkt.dp_Arg1 = WAIT_US;
	PutMsg(h->fh_Type, &wpkt->sp_Msg);
	wfh = fh;
	return 0;
}

/* collect the reply, if it has come */
static void
wait_check(void)
{
	if (wfh != 0 && GetMsg(wport) != NULL) {
		if (wpkt->sp_Pkt.dp_Res1 != DOSFALSE)
			wready = 1;
		wfh = 0;
	}
}

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct SocketSettings *lss = _lx_get_socket_settings();
	StdFileDes *sfd;
	fd_set rd, wr;
	struct timeval tv, *tvp;
	ULONG sigs;
	BPTR con = 0;
	int ready = 0, maxs = -1, r;
	nfds_t i;

	wready = 0;		/* the last key poll() saw has been read since */
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	for (i = 0; i < nfds; i++) {
		struct pollfd *p = &fds[i];

		p->revents = 0;
		if (p->fd < 0)
			continue;
		if ((sfd = _lx_fhfromfd(p->fd)) == NULL) {
			p->revents = POLLNVAL;
		} else if (sfd->lx_flags & LX_SOCKET) {
			if (p->events & (POLLIN | POLLPRI))
				FD_SET(sfd->lx_sock, &rd);
			if (p->events & POLLOUT)
				FD_SET(sfd->lx_sock, &wr);
			if (sfd->lx_sock > maxs)
				maxs = sfd->lx_sock;
		} else if ((p->events & POLLIN) && (con = con_of(p->fd)) != 0) {
			wait_check();
			if (wready || kppos < kplen)
				p->revents |= POLLIN;
		} else {
			/* files, pipes and NIL: never keep anyone waiting */
			p->revents |= p->events & (POLLIN | POLLOUT);
		}
		if (p->revents)
			ready++;
	}

	for (;;) {
		if (ready > 0) {
			tv.tv_sec = tv.tv_usec = 0;
			tvp = &tv;
		} else if (timeout >= 0) {
			tv.tv_sec = timeout / 1000;
			tv.tv_usec = (timeout % 1000) * 1000;
			tvp = &tv;
		} else
			tvp = NULL;
		sigs = SIGBREAKF_CTRL_C;
		if (con != 0 && ready == 0) {
			if (wfh == 0 && !wready && wait_send(con) == -1)
				return -1;
			sigs |= 1UL << wport->mp_SigBit;
		}
		if (lss->lx_network_type == LX_AMITCP)
			r = TCP_WaitSelect(maxs + 1, &rd, &wr, NULL, tvp, &sigs);
		else if (con != 0 && ready == 0) {
			sigs = Wait(sigs);	/* no network: the packet's own timeout */
			r = 0;
		} else
			r = sigs = 0;
		if (r < 0)
			return -1;

		if (con != 0) {
			wait_check();
			if (wready) {
				for (i = 0; i < nfds; i++)
					if (fds[i].fd >= 0 && (fds[i].events & POLLIN) &&
					    con_of(fds[i].fd) == con && !(fds[i].revents & POLLIN)) {
						fds[i].revents |= POLLIN;
						ready++;
					}
			}
		}
		if (r > 0) {
			for (i = 0; i < nfds; i++) {
				if (fds[i].fd < 0 || (sfd = _lx_fhfromfd(fds[i].fd)) == NULL ||
				    !(sfd->lx_flags & LX_SOCKET))
					continue;
				if (FD_ISSET(sfd->lx_sock, &rd))
					fds[i].revents |= fds[i].events & (POLLIN | POLLPRI);
				if (FD_ISSET(sfd->lx_sock, &wr))
					fds[i].revents |= fds[i].events & POLLOUT;
				if (fds[i].revents)
					ready++;
			}
		}
		if (ready > 0 || timeout == 0)
			return ready;
		if (sigs & SIGBREAKF_CTRL_C) {
			errno = EINTR;
			raise(SIGINT);
			return -1;
		}
		/* a timeout, or the WAIT_CHAR packet's: the caller polls again
		 * when it has no time limit of its own */
		if (tvp != NULL)
			return 0;
		FD_ZERO(&rd);
		FD_ZERO(&wr);
		for (i = 0; i < nfds; i++) {
			if (fds[i].fd < 0 || (sfd = _lx_fhfromfd(fds[i].fd)) == NULL ||
			    !(sfd->lx_flags & LX_SOCKET))
				continue;
			if (fds[i].events & (POLLIN | POLLPRI))
				FD_SET(sfd->lx_sock, &rd);
			if (fds[i].events & POLLOUT)
				FD_SET(sfd->lx_sock, &wr);
		}
	}
}

/* there are no signal masks to apply while waiting */
int
ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo,
    const sigset_t *sigmask)
{
	int ms = -1;

	if (tmo != NULL) {
		if (tmo->tv_sec > 1000000)
			ms = 1000000000;
		else
			ms = tmo->tv_sec * 1000 + (tmo->tv_nsec + 999999) / 1000000;
	}
	return poll(fds, nfds, ms);
}

/*
 * Raw mode.  A console only knows two: cooked (line editing, echo) and
 * raw.
 */
static struct termios con_tio = {
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = CS8 | CREAD,
	.c_lflag = ICANON | ECHO | ECHOE | ISIG | IEXTEN,
	/* sent to the server for the remote terminal: Backspace is ^H */
	.c_cc = {
		[VINTR] = 3, [VQUIT] = 0x1c, [VERASE] = 8, [VKILL] = 0x15,
		[VEOF] = 4, [VSTART] = 0x11, [VSTOP] = 0x13, [VSUSP] = 0x1a,
		[VREPRINT] = 0x12, [VDISCARD] = 0x0f, [VWERASE] = 0x17,
		[VLNEXT] = 0x16, [VMIN] = 1, [VTIME] = 0,
	},
};

int
tcgetattr(int fd, struct termios *t)
{
	if (con_of(fd) == 0) {
		errno = ENOTTY;
		return -1;
	}
	*t = con_tio;
	return 0;
}

int
tcsetattr(int fd, int how, const struct termios *t)
{
	BPTR con = con_of(fd);
	int raw = !(t->c_lflag & ICANON);

	if (con == 0) {
		errno = ENOTTY;
		return -1;
	}
	if (raw != !(con_tio.c_lflag & ICANON))
		SetMode(con, raw ? 1 : 0);
	con_tio = *t;
	return 0;
}

/*
 * Keys.  An Amiga console sends CSI (0x9b) sequences for the cursor and
 * function keys, and 0x7f for Del; a Unix host expects ESC [ sequences
 * and ESC [ 3 ~.  In raw mode, which an ssh session uses, read() from a
 * console translates them.  Cursor keys in application mode (ESC O A)
 * come from the AmiBash Terminal as they are.
 */
static const char *const keys[][2] = {
	{ "A", "\033[A" }, { "B", "\033[B" }, { "C", "\033[C" }, { "D", "\033[D" },
	{ " A", "\033[1;2D" }, { " @", "\033[1;2C" },	/* Shift-Left, -Right */
	{ "T", "\033[1;2A" }, { "S", "\033[1;2B" },	/* Shift-Up, -Down */
	{ "0~", "\033OP" }, { "1~", "\033OQ" }, { "2~", "\033OR" }, { "3~", "\033OS" },
	{ "4~", "\033[15~" }, { "5~", "\033[17~" }, { "6~", "\033[18~" },
	{ "7~", "\033[19~" }, { "8~", "\033[20~" }, { "9~", "\033[21~" },
	{ "10~", "\033[1;2P" }, { "11~", "\033[1;2Q" }, { "12~", "\033[1;2R" },
	{ "13~", "\033[1;2S" }, { "14~", "\033[15;2~" }, { "15~", "\033[17;2~" },
	{ "16~", "\033[18;2~" }, { "17~", "\033[19;2~" }, { "18~", "\033[20;2~" },
	{ "19~", "\033[21;2~" }, { "20~", "\033[23~" }, { "21~", "\033[24~" },
	{ "?~", "\033[28~" },				/* Help */
	{ "40~", "\033[2~" }, { "41~", "\033[5~" }, { "42~", "\033[6~" },	/* PC keys */
	{ "44~", "\033[H" }, { "45~", "\033[F" },
};

static void
kput(const char *s, size_t len)
{
	if (kplen + len <= sizeof(kpend)) {
		memcpy(kpend + kplen, s, len);
		kplen += len;
	}
}

ssize_t
amiga_read(int fd, void *buf, size_t n)
{
	unsigned char in[128];
	ssize_t got;
	int i, j;
	size_t k;

	if ((con_tio.c_lflag & ICANON) || con_of(fd) == 0)
		return read(fd, buf, n);
	if (kppos >= kplen) {
		kppos = kplen = 0;
		if ((got = read(fd, in, sizeof(in))) <= 0)
			return got;
		for (i = 0; i < got; i++) {
			if (in[i] == 0x7f) {
				kput("\033[3~", 4);
				continue;
			}
			if (in[i] != 0x9b) {
				kput((char *)&in[i], 1);
				continue;
			}
			/* CSI: parameters, then a final byte in 0x40..0x7e */
			for (j = i + 1; j < got && !(in[j] >= 0x40 && in[j] <= 0x7e); j++)
				;
			if (j >= got) {			/* incomplete: as ESC [ */
				kput("\033[", 2);
				continue;
			}
			for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
				if (strlen(keys[k][0]) == (size_t)(j - i) &&
				    memcmp(keys[k][0], in + i + 1, j - i) == 0)
					break;
			if (k < sizeof(keys) / sizeof(keys[0]))
				kput(keys[k][1], strlen(keys[k][1]));
			else {				/* anything else: ESC [ ... */
				kput("\033[", 2);
				kput((char *)in + i + 1, j - i);
			}
			i = j;
		}
	}
	if (n > (size_t)(kplen - kppos))
		n = kplen - kppos;
	memcpy(buf, kpend + kppos, n);
	kppos += n;
	return n;
}

/*
 * The size of a console: ask it (CSI 0 SPACE q) and read the window
 * bounds report, CSI 1;1;<rows>;<columns> SPACE r.
 */
static int
con_size(BPTR in, BPTR out, int *rows, int *cols)
{
	char buf[32];
	int n = 0, raw = !(con_tio.c_lflag & ICANON), ok = 0;
	long a[4], k = 0;
	char *p;

	if (!raw)
		SetMode(in, 1);
	Write(out, "\2330 q", 4);
	while (n < (int)sizeof(buf) - 1 && WaitForChar(in, 500000)) {
		if (Read(in, buf + n, 1) != 1)
			break;
		if (buf[n++] == 'r')
			break;
	}
	buf[n] = '\0';
	if (!raw)
		SetMode(in, 0);
	if ((p = strchr(buf, 0x9b)) != NULL)
		p++;
	else if ((p = strstr(buf, "\033[")) != NULL)
		p += 2;
	while (p != NULL && k < 4) {
		a[k++] = strtol(p, &p, 10);
		if (*p != ';')
			break;
		p++;
	}
	if (k == 4 && a[2] > 0 && a[3] > 0) {
		*rows = a[2];
		*cols = a[3];
		ok = 1;
	}
	return ok;
}

int
amiga_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	BPTR con;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (req == TIOCGWINSZ) {
		struct winsize *ws = arg;
		int rows = 0, cols = 0;
		const char *v;

		if ((con = con_of(fd)) == 0) {
			errno = ENOTTY;
			return -1;
		}
		if (!con_size(con, Output() ? Output() : con, &rows, &cols)) {
			rows = (v = getenv("LINES")) ? atoi(v) : 24;
			cols = (v = getenv("COLUMNS")) ? atoi(v) : 80;
		}
		memset(ws, 0, sizeof(*ws));
		ws->ws_row = rows;
		ws->ws_col = cols;
		return 0;
	}
	return ioctl(fd, req, arg);
}

/*
 * Non-blocking mode is only remembered: the descriptors stay blocking.
 * ssh reads only what poll() said is there, and an AmiTCP-style stack
 * reports EWOULDBLOCK with its own number, which newlib's errno.h doesn't
 * know.
 */
static unsigned char nonblock[64];	/* per descriptor: O_NONBLOCK set */

int
amiga_fcntl(int fd, int cmd, ...)
{
	StdFileDes *sfd = _lx_fhfromfd(fd);
	va_list ap;
	long arg;

	va_start(ap, cmd);
	arg = va_arg(ap, long);
	va_end(ap);
	if (sfd == NULL) {
		errno = EBADF;
		return -1;
	}
	switch (cmd) {
	case F_GETFL:
		return O_RDWR | (fd < (int)sizeof(nonblock) && nonblock[fd] ? O_NONBLOCK : 0);
	case F_SETFL:
		if (fd < (int)sizeof(nonblock))
			nonblock[fd] = (arg & O_NONBLOCK) != 0;
		return 0;
	case F_GETFD:
	case F_SETFD:
		return 0;
	}
	errno = EINVAL;
	return -1;
}

/*
 * A password from the console, without echo: raw mode, with Backspace,
 * Ctrl-U (clear) and Ctrl-C (give up).
 */
char *
readpassphrase(const char *prompt, char *buf, size_t bufsiz, int flags)
{
	BPTR in, out, own = 0;
	size_t n = 0;
	int esc = 0;
	unsigned char c;

	if (bufsiz == 0) {
		errno = EINVAL;
		return NULL;
	}
	if (!(flags & RPP_STDIN) && (own = Open((STRPTR)"*", MODE_OLDFILE)) != 0)
		in = out = own;
	else {
		in = Input();
		out = Output();
		if ((flags & RPP_REQUIRE_TTY) && !IsInteractive(in)) {
			errno = ENOTTY;
			return NULL;
		}
	}
	if (prompt != NULL)
		Write(out, (APTR)prompt, strlen(prompt));
	if (IsInteractive(in))
		SetMode(in, 1);
	for (;;) {
		if (Read(in, &c, 1) != 1) {
			if (n == 0) {
				buf[0] = '\0';
				goto fail;
			}
			break;
		}
		if (esc) {			/* skip CSI sequences: cursor keys etc. */
			if (esc == 1 && c == '[') {
				esc = 2;
				continue;
			}
			if (c >= 0x40 && c <= 0x7e)
				esc = 0;
			else if (esc == 1)
				esc = 0;
			continue;
		}
		if (c == '\r' || c == '\n')
			break;
		if (c == 3) {			/* Ctrl-C */
			n = 0;
			goto fail;
		}
		if (c == 0x9b) {
			esc = 2;
			continue;
		}
		if (c == 0x1b) {
			esc = 1;
			continue;
		}
		if (c == 8 || c == 0x7f) {
			if (n > 0) {
				n--;
				if (flags & RPP_ECHO_ON)
					Write(out, "\b \b", 3);
			}
			continue;
		}
		if (c == 0x15) {		/* Ctrl-U */
			while (flags & RPP_ECHO_ON && n > 0) {
				Write(out, "\b \b", 3);
				n--;
			}
			n = 0;
			continue;
		}
		if (c < 0x20)
			continue;
		if (flags & RPP_SEVENBIT)
			c &= 0x7f;
		if (flags & RPP_FORCELOWER && c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		if (flags & RPP_FORCEUPPER && c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (n < bufsiz - 1) {
			buf[n++] = c;
			if (flags & RPP_ECHO_ON)
				Write(out, &c, 1);
		}
	}
	buf[n] = '\0';
	Write(out, "\n", 1);
	if (IsInteractive(in))
		SetMode(in, !(con_tio.c_lflag & ICANON));
	if (own)
		Close(own);
	return buf;

 fail:
	memset(buf, 0, bufsiz);
	Write(out, "\n", 1);
	if (IsInteractive(in))
		SetMode(in, !(con_tio.c_lflag & ICANON));
	if (own)
		Close(own);
	errno = EINTR;
	return NULL;
}

#endif /* __amigaos__ */
