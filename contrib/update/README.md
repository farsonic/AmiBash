# amibash-update

Checks GitHub for a newer AmiBash and, with `INSTALL`, installs it.

```
amibash-update [INSTALL] [FORCE] [REPO=owner/name] [CAFILE=file] [BASH=path]
```

1. Asks `https://api.github.com/repos/farsonic/AmiBash/releases/latest`
   (`REPO=` for another) for the latest release: its tag (`v0.2`), date,
   notes and files.
2. Reads the installed version from `C:bash`'s `$VER` string (`BASH=` for
   another file) and compares the two. Return code 0: up to date,
   5 (WARN): a newer release is there, 10: something failed.
3. With `INSTALL`, if the release is newer (or with `FORCE`): downloads
   the release's `AmiBash*.lha` to `T:`, unpacks it with LhA to
   `T:AmiBash-update` and starts its `Install-AmiBash` with the Installer,
   which asks what to install as usual. Without LhA it leaves the archive
   in `T:` and says so.

It needs AmiSSL v5 (Aminet `util/libs/AmiSSL-v5-OS3`), which in turn needs a
68020 or better, and a running TCP/IP stack. Certificates are checked against
AmiSSL's store (`AmiSSL:Certs`); `CAFILE=` adds certificates in a PEM file,
for networks that inspect HTTPS.

## Publishing a release

The tag is the version, with a `v`: `v0.2` for `AMIBASH_SHORTVER "0.2"` in
`src/sh.h`. Attach the archive as `AmiBash-0.2.lha` (any name starting with
`AmiBash` and ending in `.lha`; `dist/make_release.sh` writes
`dist/AmiBash.lha`).

## Building

With bebbo's m68k-amigaos-gcc (see `toolchain/README.md`) and the AmiSSL SDK,
which `make` fetches from Aminet and checks:

```
make
```

Aminet keeps one `AmiSSL-v5-SDK.lha`; when a new version replaces it, update
`SHA256` in the Makefile.
