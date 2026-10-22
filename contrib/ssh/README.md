# ssh and ssh-keygen for AmigaOS

OpenSSH 10.6p1 (portable), the client and the key generator, for
AmigaOS 3.x on a 68020 or better. It is built without OpenSSL, using
OpenSSH's own algorithms:

* **Key exchange:** sntrup761x25519 or mlkem768x25519, both post-quantum,
  and curve25519.
* **Keys:** ed25519.
* **Ciphers:** chacha20-poly1305 and AES-CTR.

That's enough for any current OpenSSH server. It can't connect to servers or
devices that only offer RSA host keys or older key exchanges.

## Using it

```
ssh user@host                     # log in; works best in the AmiBash Terminal
ssh -p 2222 user@host 'uname -a'  # run a command
ssh-keygen -t ed25519             # make a key: $HOME/.ssh/id_ed25519
```

* **TCP/IP stack:** `ssh` needs one that provides `bsdsocket.library`: Roadshow (OS 3.2),
  AmiTCP, Miami or Genesis.
* **Home directory:** `~` is `$HOME`, or `SYS:` when `HOME` isn't set, so keys and
  `known_hosts` live in `SYS:.ssh` by default.
* **System files:** the system-wide `ssh_config` and `ssh_known_hosts` live in
  `ENVARC:ssh`.
* **Random numbers:** these come from timing jitter (the E-clock against a busy
  loop, the video beam position, exec's task-switch counters), hashed with
  SHA-512 together with a seed file, `ENVARC:ssh/random_seed`, which every
  run replaces. The Amiga has no hardware random number generator, so the
  seed file matters: it carries what earlier runs gathered.
* **TERM:** the remote side gets `TERM=ansi` when `TERM` isn't set. The AmiBash
  Terminal understands what the `ansi` terminfo entry uses.

**Not supported:** everything that needs `fork()` or Unix sockets, namely:

* ProxyCommand and ProxyJump
* connection sharing (ControlMaster)
* `ssh -f`
* ssh-agent
* X11 forwarding
* `scp` and `sftp`

Port forwarding with `-L` and `-R` is part of the client, but hasn't been
tested.

## Building

You need bebbo's m68k-amigaos-gcc with libnix (see `toolchain/README.md`):

```
make            # downloads OpenSSH, checks its SHA-256, patches and builds it
```

`AMIGCC=/path/to/toolchain` overrides where the toolchain is.

The port is in three parts:

| File | What it does |
|---|---|
| `openssh-amiga.patch` | small changes to OpenSSH itself, all marked `__amigaos__` |
| `amiga/port-amiga.c` | the missing POSIX bits, the user (from `$USER` and `$HOME`), and `getentropy()` |
| `amiga/port-amiga-io.c` | `poll()` over sockets and the console together, raw mode, window size, password input |
| `amiga/amiga_compat.h` | included first in every file |
| `amiga/config_fix.sh` | corrects `config.h` after `configure` |
| `amiga/build.sh` | `configure`, the fix-ups, `make` and `strip` |

Things in libnix that the port works around:

* **`__stack`:** setting it does nothing unless something also refers to
  `__stkinit`. Without the bigger stack, OpenSSH overflows a Shell's 4 KB
  stack into free memory at once.
* **`getaddrinfo()` and friends:** these call `bsdsocket.library` 4 (Roadshow)
  functions without checking that they exist. On other stacks they jump past
  the library's vectors. OpenSSH's own versions, built on `gethostbyname()`,
  are used instead.
* **`dup2()`:** it copies a descriptor without reference counting, so the
  handle is closed twice.
* **`fopen("a+")`:** it can neither seek nor write.
* **`select()` and `tcsetattr()`:** `select()` can't wait for the console,
  and `tcsetattr()` has no raw mode.
