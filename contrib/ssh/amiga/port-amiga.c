/*
 * port-amiga.c - AmigaOS 3.x (libnix) support for OpenSSH: the POSIX
 * functions libnix leaves out, or enough of them for the ssh client and
 * ssh-keygen.  config_fix.sh copies it to openbsd-compat/.
 */

#include "includes.h"

#ifdef __amigaos__

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>

#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include "bsd-sha2.h"

/*
 * OpenSSH keeps big buffers on the stack.  A Shell's default stack (4 KB)
 * overflows into free memory at once, so libnix's start-up code moves the
 * program to a stack of this size; referring to __stkinit is what pulls
 * that code in.
 */
unsigned long __stack = 262144;
extern void __stkinit(void);
void *__stkinit_ref = __stkinit;

/*
 * Signals: libnix has signal() for SIGINT and friends; there are no
 * signal masks.
 */
int
sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	_sig_func_ptr old;

	if (act == NULL) {
		old = signal(sig, SIG_DFL);
		signal(sig, old);
	} else
		old = signal(sig, act->sa_handler);
	if (old == SIG_ERR) {
		errno = EINVAL;
		return -1;
	}
	if (oact != NULL) {
		memset(oact, 0, sizeof(*oact));
		oact->sa_handler = old;
	}
	return 0;
}

int
sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	if (oset != NULL)
		memset(oset, 0, sizeof(*oset));
	return 0;
}

/*
 * No processes to start or detach.
 */
pid_t
setsid(void)
{
	errno = ENOSYS;
	return -1;
}

int
execl(const char *path, const char *arg, ...)
{
	errno = ENOSYS;
	return -1;
}

/*
 * Protection bits are set by name (chmod) on AmigaOS; files ssh-keygen
 * writes are private enough without it.
 */
int
fchmod(int fd, mode_t mode)
{
	return 0;
}

/*
 * The serial line speed of a console: there is none.
 */
speed_t cfgetispeed(const struct termios *t) { return B9600; }
speed_t cfgetospeed(const struct termios *t) { return B9600; }
int cfsetispeed(struct termios *t, speed_t s) { return 0; }
int cfsetospeed(struct termios *t, speed_t s) { return 0; }

int
readv(int fd, struct iovec *iov, int iovcnt)
{
	ssize_t n, total = 0;
	int i;

	for (i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		n = read(fd, iov[i].iov_base, iov[i].iov_len);
		if (n < 0)
			return total > 0 ? total : -1;
		total += n;
		if ((size_t)n < iov[i].iov_len)
			break;
	}
	return total;
}

/*
 * mmap() of a file for reading: a copy in memory.
 */
void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	char *m;
	size_t got = 0;
	ssize_t n;

	if ((flags & MAP_SHARED) || (prot & PROT_WRITE) ||
	    lseek(fd, off, SEEK_SET) == -1 || (m = malloc(len ? len : 1)) == NULL) {
		errno = ENODEV;
		return (void *)-1;
	}
	while (got < len && (n = read(fd, m + got, len - got)) > 0)
		got += n;
	memset(m + got, 0, len - got);
	return m;
}

int
munmap(void *addr, size_t len)
{
	free(addr);
	return 0;
}

/*
 * Character widths: ssh only sees ISO-8859-1 on the Amiga.
 */
int
wcwidth(wchar_t c)
{
	if (c == 0)
		return 0;
	if (c < 0x20 || (c >= 0x7f && c < 0xa0))
		return -1;
	return 1;
}

/*
 * The user: AmigaOS has one.  The name is $USER or $LOGNAME, the home
 * directory $HOME (as in AmiBash), else SYS:.  ~/.ssh is then SYS:.ssh.
 */
static struct passwd *
amiga_pw(void)
{
	static struct passwd pw;
	static char name[32], dir[256];
	const char *v;

	if ((v = getenv("USER")) == NULL || *v == '\0')
		if ((v = getenv("LOGNAME")) == NULL || *v == '\0')
			v = "amiga";
	strlcpy(name, v, sizeof(name));
	if ((v = getenv("HOME")) == NULL || *v == '\0')
		v = "SYS:";
	strlcpy(dir, v, sizeof(dir));
	memset(&pw, 0, sizeof(pw));
	pw.pw_name = name;
	pw.pw_passwd = "*";
	pw.pw_uid = getuid();
	pw.pw_gid = getgid();
	pw.pw_gecos = name;
	pw.pw_dir = dir;
	pw.pw_shell = "bash";
	return &pw;
}

struct passwd *
getpwuid(uid_t uid)
{
	return amiga_pw();
}

struct passwd *
getpwnam(const char *name)
{
	struct passwd *pw = amiga_pw();

	return strcmp(name, pw->pw_name) == 0 ? pw : NULL;
}

/*
 * Random numbers.  There is no /dev/urandom: getentropy() hashes timing
 * jitter (E-clock ticks against a busy loop, the video beam position,
 * exec's dispatch counters) with a seed file kept from earlier runs,
 * ENVARC:ssh/random_seed, which it then replaces.
 */
#define SEED_FILE	"ENVARC:ssh/random_seed"
#define SEED_DIR	"ENVARC:ssh"
#define JITTER_SAMPLES	4096

static u_char pool[SHA512_DIGEST_LENGTH];
static int pool_ready;
static u_int64_t pool_count;

static void
gather(SHA2_CTX *ctx, int samples)
{
	struct MsgPort *port;
	struct timerequest *tr;
	struct Device *TimerBase;
	struct EClockVal e0, e1;
	struct DateStamp ds;
	volatile UWORD *vhposr = (volatile UWORD *)0xdff006;
	struct Task *me = FindTask(NULL);
	ULONG spins, i;
	UWORD beam;

	SHA512Update(ctx, (u_char *)&me, sizeof(me));
	SHA512Update(ctx, (u_char *)&SysBase->IdleCount, sizeof(SysBase->IdleCount));
	SHA512Update(ctx, (u_char *)&SysBase->DispCount, sizeof(SysBase->DispCount));
	DateStamp(&ds);
	SHA512Update(ctx, (u_char *)&ds, sizeof(ds));
	i = AvailMem(MEMF_ANY);
	SHA512Update(ctx, (u_char *)&i, sizeof(i));

	if ((port = CreateMsgPort()) == NULL)
		return;
	if ((tr = (struct timerequest *)CreateIORequest(port, sizeof(*tr))) == NULL) {
		DeleteMsgPort(port);
		return;
	}
	if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, &tr->tr_node, 0) == 0) {
		TimerBase = tr->tr_node.io_Device;
		for (i = 0; i < (ULONG)samples; i++) {
			/* how far a busy loop gets until the E-clock moves on */
			ReadEClock(&e0);
			spins = 0;
			do {
				ReadEClock(&e1);
				spins++;
			} while (e1.ev_lo == e0.ev_lo && spins < 100000);
			SHA512Update(ctx, (u_char *)&spins, sizeof(spins));
			SHA512Update(ctx, (u_char *)&e1.ev_lo, sizeof(e1.ev_lo));
			beam = *vhposr;
			SHA512Update(ctx, (u_char *)&beam, sizeof(beam));
		}
		SHA512Update(ctx, (u_char *)&SysBase->DispCount, sizeof(SysBase->DispCount));
		CloseDevice(&tr->tr_node);
	}
	DeleteIORequest((struct IORequest *)tr);
	DeleteMsgPort(port);
}

static void
pool_init(void)
{
	SHA2_CTX ctx;
	u_char seed[SHA512_DIGEST_LENGTH];
	BPTR fh;
	LONG n;

	SHA512Init(&ctx);
	if ((fh = Open((STRPTR)SEED_FILE, MODE_OLDFILE)) != 0) {
		if ((n = Read(fh, seed, sizeof(seed))) > 0)
			SHA512Update(&ctx, seed, n);
		Close(fh);
	}
	gather(&ctx, JITTER_SAMPLES);
	SHA512Final(pool, &ctx);

	/* the next run starts from this one */
	SHA512Init(&ctx);
	SHA512Update(&ctx, (u_char *)"seed", 4);
	SHA512Update(&ctx, pool, sizeof(pool));
	SHA512Final(seed, &ctx);
	if ((fh = Lock((STRPTR)SEED_DIR, ACCESS_READ)) != 0)
		UnLock(fh);
	else if ((fh = CreateDir((STRPTR)SEED_DIR)) != 0)
		UnLock(fh);
	if ((fh = Open((STRPTR)SEED_FILE, MODE_NEWFILE)) != 0) {
		Write(fh, seed, sizeof(seed));
		Close(fh);
	}
	explicit_bzero(seed, sizeof(seed));
	pool_ready = 1;
}

int
getentropy(void *buf, size_t len)
{
	SHA2_CTX ctx;
	u_char out[SHA512_DIGEST_LENGTH];
	u_char *p = buf;
	size_t n;

	if (len > 256) {
		errno = EIO;
		return -1;
	}
	if (!pool_ready)
		pool_init();
	while (len > 0) {
		SHA512Init(&ctx);
		SHA512Update(&ctx, pool, sizeof(pool));
		pool_count++;
		SHA512Update(&ctx, (u_char *)&pool_count, sizeof(pool_count));
		gather(&ctx, 64);
		SHA512Final(out, &ctx);
		n = len < sizeof(out) / 2 ? len : sizeof(out) / 2;
		memcpy(p, out, n);
		/* a new pool, so earlier output can't be worked out from it */
		SHA512Init(&ctx);
		SHA512Update(&ctx, (u_char *)"next", 4);
		SHA512Update(&ctx, pool, sizeof(pool));
		SHA512Update(&ctx, out, sizeof(out));
		SHA512Final(pool, &ctx);
		p += n;
		len -= n;
	}
	explicit_bzero(out, sizeof(out));
	return 0;
}

#endif /* __amigaos__ */
