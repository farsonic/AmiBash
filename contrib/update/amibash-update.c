/*
 * amibash-update - is there a newer AmiBash on GitHub?
 *
 *   amibash-update            compare C:bash with the latest release
 *   amibash-update INSTALL    download that release and start its installer
 *
 * It asks GitHub's API for the latest release of farsonic/AmiBash (or
 * REPO=owner/name), compares its tag (v0.2) with the version in C:bash's
 * $VER string, and with INSTALL downloads the release's AmiBash*.lha to
 * T:, unpacks it with LhA and runs Install-AmiBash with the Installer.
 * FORCE installs even when C:bash is up to date. CAFILE=file adds
 * trusted certificates (PEM), for networks that inspect HTTPS; API=url
 * asks another server instead of https://api.github.com (for testing).
 *
 * GitHub only answers over HTTPS, so this needs AmiSSL v5 (Aminet
 * util/libs/AmiSSL-v5-OS3) and a TCP/IP stack; built with bebbo's
 * m68k-amigaos-gcc and the AmiSSL SDK (see Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <dos/dostags.h>

#define VERSION "0.2"
static const char ver[] = "$VER: amibash-update " VERSION " (23.10.2026)";
const char stack_size[] = "$STACK:32768";

struct Library *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase, *SocketBase;
static SSL_CTX *ctx;

#define UA "amibash-update/" VERSION " (AmigaOS)"
#define MAXREDIR 5
#define DLFILE "T:AmiBash-update.lha"
#define DLDIR "T:AmiBash-update"

/* ---- AmiSSL ----------------------------------------------------------------- */

static int ssl_open(const char *cafile)
{
    if (!(SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4))) {
        printf("No TCP/IP stack is running (bsdsocket.library).\n");
        return 0;
    }
    if (!(AmiSSLMasterBase = OpenLibrary((STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        printf("This needs AmiSSL v5 (Aminet util/libs/AmiSSL-v5-OS3).\n");
        return 0;
    }
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION, AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase, AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase, AmiSSL_ErrNoPtr, (ULONG)&errno, TAG_DONE) != 0) {
        printf("Can't initialise AmiSSL (is it v5 or newer?).\n");
        AmiSSLBase = NULL;
        return 0;
    }
    if (!(ctx = SSL_CTX_new(TLS_client_method()))) {
        printf("Can't create an SSL context.\n");
        return 0;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    /* AmiSSL:Certs, and CAFILE if given; servers must prove who they are */
    SSL_CTX_set_default_verify_paths(ctx);
    if (cafile && !SSL_CTX_load_verify_locations(ctx, cafile, NULL))
        printf("Can't read the certificates in %s.\n", cafile);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    return 1;
}

static void ssl_close(void)
{
    if (AmiSSLBase) {
        if (ctx) SSL_CTX_free(ctx);
        CloseAmiSSL();
    }
    if (AmiSSLMasterBase) CloseLibrary(AmiSSLMasterBase);
    if (SocketBase) CloseLibrary(SocketBase);
}

/* ---- HTTPS GET ---------------------------------------------------------------- */

/* where a response's body goes: memory (fh 0) or a file */
typedef struct Sink {
    char *buf;
    long len, cap;
    BPTR fh;
    long total;                         /* Content-Length, -1 unknown */
    long shown;                         /* progress shown up to here  */
} Sink;

static int sink_put(Sink *s, const char *p, long n)
{
    if (n <= 0) return 1;
    if (s->fh) {
        if (Write(s->fh, (APTR)p, n) != n) return 0;
        s->len += n;
        if (s->len - s->shown >= 32768 || (s->total > 0 && s->len == s->total)) {
            s->shown = s->len;
            if (s->total > 0) printf("\r  %ld of %ld KB", s->len >> 10, s->total >> 10);
            else printf("\r  %ld KB", s->len >> 10);
            fflush(stdout);
        }
        return 1;
    }
    if (s->len + n + 1 > s->cap) {
        long c = (s->len + n + 1) * 2;
        char *b = realloc(s->buf, c);
        if (!b) return 0;
        s->buf = b;
        s->cap = c;
    }
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = 0;
    return 1;
}

static void ssl_error(const char *what, const char *host)
{
    unsigned long e = ERR_get_error();
    char b[200];
    printf("%s %s failed", what, host);
    if (e) {
        ERR_error_string_n(e, b, sizeof(b));
        printf(": %s", b);
    }
    printf("\n");
}

/* read the rest of a chunked body into s */
static int read_chunked(BIO *b, char *pend, long npend, Sink *s)
{
    static char buf[8192];
    long have = npend, size;
    memmove(buf, pend, npend);
    for (;;) {
        char *nl;
        int n;
        /* the chunk size line */
        while (!(nl = memchr(buf, '\n', have))) {
            if (have >= (long)sizeof(buf) - 1 || (n = BIO_read(b, buf + have, sizeof(buf) - 1 - have)) <= 0) return 0;
            have += n;
        }
        size = strtol(buf, NULL, 16);
        have -= nl + 1 - buf;
        memmove(buf, nl + 1, have);
        if (size == 0) return 1;
        while (size > 0) {
            long take;
            if (have == 0) {
                if ((n = BIO_read(b, buf, sizeof(buf))) <= 0) return 0;
                have = n;
            }
            take = have < size ? have : size;
            if (!sink_put(s, buf, take)) return 0;
            size -= take;
            have -= take;
            memmove(buf, buf + take, have);
        }
        /* the CRLF after the chunk */
        while (have < 2) {
            if ((n = BIO_read(b, buf + have, sizeof(buf) - have)) <= 0) return 0;
            have += n;
        }
        have -= 2;
        memmove(buf, buf + 2, have);
    }
}

/* GET url (https only) into s, following redirects; the HTTP status,
 * or 0 if it could not be fetched */
static int https_get(const char *url, const char *accept, Sink *s)
{
    char cur[1024];
    int hop;
    strncpy(cur, url, sizeof(cur) - 1);
    cur[sizeof(cur) - 1] = 0;
    for (hop = 0; hop <= MAXREDIR; hop++) {
        static char hdr[8192];
        char host[256], conn[270], name[256], *path, *p, *body, *loc = NULL;
        long hl = 0, clen = -1;
        int n, status, chunked = 0, ok;
        BIO *b;
        SSL *ssl = NULL;
        if (strncmp(cur, "https://", 8)) {
            printf("Not an https address: %s\n", cur);
            return 0;
        }
        p = cur + 8;
        path = strchr(p, '/');
        n = path ? path - p : (int)strlen(p);
        if (n >= (int)sizeof(host)) return 0;
        memcpy(host, p, n);
        host[n] = 0;
        if (!path) path = "/";
        /* host or host:port; the certificate must name the host */
        strcpy(name, host);
        if ((p = strchr(name, ':')) != NULL) *p = 0;
        if (strchr(host, ':')) strcpy(conn, host);
        else sprintf(conn, "%s:443", host);
        if (!(b = BIO_new_ssl_connect(ctx))) return 0;
        BIO_get_ssl(b, &ssl);
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
        SSL_set_tlsext_host_name(ssl, name);    /* SNI */
        SSL_set1_host(ssl, name);
        BIO_set_conn_hostname(b, conn);
        if (BIO_do_connect(b) <= 0 || BIO_do_handshake(b) <= 0) {
            ssl_error("Connecting to", host);
            BIO_free_all(b);
            return 0;
        }
        sprintf(hdr, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " UA "\r\nAccept: %s\r\nConnection: close\r\n\r\n",
                path, host, accept);
        if (BIO_puts(b, hdr) <= 0) {
            ssl_error("Talking to", host);
            BIO_free_all(b);
            return 0;
        }
        /* the response's headers */
        body = NULL;
        while (!body) {
            if (hl >= (long)sizeof(hdr) - 1 || (n = BIO_read(b, hdr + hl, sizeof(hdr) - 1 - hl)) <= 0) {
                printf("No answer from %s.\n", host);
                BIO_free_all(b);
                return 0;
            }
            hl += n;
            hdr[hl] = 0;
            if ((body = strstr(hdr, "\r\n\r\n")) != NULL) body += 4;
        }
        status = atoi(hdr + 9);
        for (p = strstr(hdr, "\r\n") + 2; p < body - 2;) {
            char *e = strstr(p, "\r\n");
            *e = 0;
            if (!strncasecmp(p, "Location:", 9)) loc = p + 9 + strspn(p + 9, " ");
            else if (!strncasecmp(p, "Content-Length:", 15)) clen = atol(p + 15);
            else if (!strncasecmp(p, "Transfer-Encoding:", 18) && strstr(p, "chunked")) chunked = 1;
            p = e + 2;
        }
        if (status >= 300 && status < 400 && loc) {
            char next[1024];
            strncpy(next, loc, sizeof(next) - 1);
            next[sizeof(next) - 1] = 0;
            if (next[0] == '/') sprintf(cur, "https://%s%.990s", host, next);
            else strcpy(cur, next);
            BIO_free_all(b);
            continue;
        }
        s->total = clen;
        if (chunked) ok = read_chunked(b, body, hdr + hl - body, s);
        else {
            static char buf[8192];
            ok = sink_put(s, body, hdr + hl - body);
            while (ok && (clen < 0 || s->len < clen) && (n = BIO_read(b, buf, sizeof(buf))) > 0)
                ok = sink_put(s, buf, n);
            if (clen >= 0 && s->len < clen) ok = 0;
        }
        BIO_free_all(b);
        if (!ok) {
            printf("\nThe transfer from %s broke off.\n", host);
            return 0;
        }
        return status;
    }
    printf("Too many redirections.\n");
    return 0;
}

/* ---- JSON, just enough ---------------------------------------------------------- */

/* the string value of "key" at or after from; malloc'd, unescaped */
static char *json_str(const char *from, const char *key, const char **after)
{
    char pat[64], *r, *o;
    const char *p, *q;
    sprintf(pat, "\"%.50s\"", key);
    if (!from || !(p = strstr(from, pat))) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return NULL;
    for (q = ++p; *q && *q != '"'; q++)
        if (*q == '\\' && q[1]) q++;
    if (!(r = o = malloc(q - p + 1))) return NULL;
    for (; p < q; p++) {
        if (*p != '\\') {
            *o++ = *p;
            continue;
        }
        switch (*++p) {
        case 'n': *o++ = '\n'; break;
        case 'r': break;
        case 't': *o++ = '\t'; break;
        case 'u': *o++ = '?'; p += 4; break;  /* no Unicode on a plain Amiga font */
        default: *o++ = *p;
        }
    }
    *o = 0;
    if (after) *after = q + 1;
    return r;
}

/* ---- versions ------------------------------------------------------------------- */

/* "v0.10" vs "0.9": <0, 0, >0 */
static int vercmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        else if (*a) break;
        if (*b == '.') b++;
        else if (*b) break;
    }
    return 0;
}

/* the version in "$VER: bash 0.1 (...)" of the bash at path */
static int installed_version(const char *path, char *v, int max)
{
    static char buf[16384 + 64];
    const char *tag = "$VER: bash ";
    FILE *f = fopen(path, "rb");
    long keep = 0, n;
    if (!f) return 0;
    while ((n = fread(buf + keep, 1, sizeof(buf) - 64 - keep, f)) > 0) {
        long len = keep + n, i;
        for (i = 0; i + 40 < len; i++)
            if (buf[i] == '$' && !memcmp(buf + i, tag, strlen(tag))) {
                const char *p = buf + i + strlen(tag);
                int k = 0;
                while (k < max - 1 && p[k] && p[k] != ' ') v[k] = p[k], k++;
                v[k] = 0;
                fclose(f);
                return k > 0;
            }
        keep = len < 40 ? len : 40;
        memmove(buf, buf + len - keep, keep);
    }
    fclose(f);
    return 0;
}

/* ---- installing ------------------------------------------------------------------- */

static int run(const char *cmd)
{
    return (int)SystemTags((STRPTR)cmd, SYS_Input, Input(), SYS_Output, Output(), SYS_UserShell, TRUE, NP_StackSize, 32768,
                           TAG_DONE);
}

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static const char *first_of(const char *const *v)
{
    for (; *v; v++)
        if (exists(*v)) return *v;
    return NULL;
}

static int install(const char *url)
{
    static const char *const lhas[] = {"C:lha", "C:LhA", "C:lzx", NULL};
    static const char *const installers[] = {"SYS:Utilities/Installer", "C:Installer", "SYS:System/Installer",
                                             "SYS:Tools/Installer", NULL};
    char cmd[300];
    const char *lha, *inst;
    Sink s;
    int st;
    BPTR dir, old;
    memset(&s, 0, sizeof(s));
    printf("Downloading %s\n", url);
    if (!(s.fh = Open((STRPTR)DLFILE, MODE_NEWFILE))) {
        printf("Can't write " DLFILE ".\n");
        return 0;
    }
    st = https_get(url, "application/octet-stream", &s);
    Close(s.fh);
    printf("\n");
    if (st != 200) {
        if (st) printf("GitHub answered %d.\n", st);
        DeleteFile((STRPTR)DLFILE);
        return 0;
    }
    if (!(lha = first_of(lhas)) || strstr(lha, "lzx")) {
        printf("Saved as " DLFILE ". There is no LhA in C: to unpack it: unpack it\n"
               "(Aminet util/arc/lha) and double-click Install-AmiBash.\n");
        return 1;
    }
    run("Delete >NIL: " DLDIR " ALL QUIET");
    sprintf(cmd, "%s >NIL: -q x " DLFILE " " DLDIR "/", lha);
    if (run(cmd) != 0 || !exists(DLDIR "/AmiBash/Install-AmiBash")) {
        printf("Can't unpack " DLFILE " with %s.\n", lha);
        return 0;
    }
    DeleteFile((STRPTR)DLFILE);
    if (!(inst = first_of(installers))) {
        printf("Unpacked in " DLDIR "/AmiBash; double-click Install-AmiBash there.\n");
        return 1;
    }
    printf("Starting the installer...\n");
    /* the script finds the archive's files next to itself */
    if (!(dir = Lock((STRPTR)DLDIR "/AmiBash", ACCESS_READ))) return 0;
    old = CurrentDir(dir);
    /* every argument by name: OS 3.x and AROS order them differently */
    sprintf(cmd, "%s SCRIPT=Install-AmiBash APPNAME=AmiBash MINUSER=AVERAGE", inst);
    st = run(cmd);
    CurrentDir(old);
    UnLock(dir);
    if (st != 0) printf("The installer ended with %d; the files are in " DLDIR "/AmiBash.\n", st);
    else {
        run("Delete >NIL: " DLDIR " ALL QUIET");
        printf("Done. Open a new Shell or terminal tab for the new bash.\n");
    }
    return st == 0;
}

/* ---- main ------------------------------------------------------------------------- */

int main(void)
{
    enum { A_INSTALL, A_FORCE, A_REPO, A_CAFILE, A_BASH, A_API, A_N };
    LONG a[A_N] = {0};
    struct RDArgs *ra;
    const char *repo, *bash, *api;
    char url[300], have[32] = "", *tag = NULL, *name = NULL, *date = NULL, *notes = NULL, *dl = NULL;
    const char *p;
    Sink s;
    int st, rc = RETURN_ERROR, cmp;

    (void)ver;
    memset(&s, 0, sizeof(s));
    if (!(ra = ReadArgs((STRPTR)"INSTALL/S,FORCE/S,REPO/K,CAFILE/K,BASH/K,API/K", a, NULL))) {
        PrintFault(IoErr(), (STRPTR)"amibash-update");
        printf("usage: amibash-update [INSTALL] [FORCE] [REPO=owner/name] [CAFILE=file] [BASH=path]\n");
        return RETURN_FAIL;
    }
    repo = a[A_REPO] ? (const char *)a[A_REPO] : "farsonic/AmiBash";
    bash = a[A_BASH] ? (const char *)a[A_BASH] : "C:bash";
    api = a[A_API] ? (const char *)a[A_API] : "https://api.github.com";
    if (!installed_version(bash, have, sizeof(have)) && strcmp(bash, "C:bash") &&
        installed_version("C:bash", have, sizeof(have)))
        bash = "C:bash";                /* BASH= was the name of a resident one */
    else if (!have[0]) {
        printf("No AmiBash found as %s.\n", bash);
        have[0] = 0;
    }
    if (!ssl_open((const char *)a[A_CAFILE])) goto out;
    sprintf(url, "%.80s/repos/%.150s/releases/latest", api, repo);
    printf("Asking GitHub for the latest AmiBash...\n");
    st = https_get(url, "application/vnd.github+json", &s);
    if (st == 404) {
        /* no release yet: say what the newest tag is */
        free(s.buf);
        memset(&s, 0, sizeof(s));
        sprintf(url, "%.80s/repos/%.150s/tags", api, repo);
        if (https_get(url, "application/vnd.github+json", &s) == 200 && (tag = json_str(s.buf, "name", NULL)))
            printf("%s has no release yet; its newest tag is %s.\n", repo, tag);
        else
            printf("%s has no releases.\n", repo);
        goto out;
    }
    if (st != 200 || !s.buf) {
        if (st) printf("GitHub answered %d.\n", st);
        goto out;
    }
    tag = json_str(s.buf, "tag_name", NULL);
    name = json_str(s.buf, "name", NULL);
    date = json_str(s.buf, "published_at", NULL);
    notes = json_str(s.buf, "body", NULL);
    /* the release's AmiBash*.lha */
    for (p = s.buf; !dl;) {
        char *u = json_str(p, "browser_download_url", &p);
        const char *f;
        if (!u) break;
        f = strrchr(u, '/');
        if (f && !strncasecmp(f + 1, "AmiBash", 7) && strlen(f) > 4 && !strcasecmp(f + strlen(f) - 4, ".lha")) dl = u;
        else free(u);
    }
    if (!tag) {
        printf("GitHub's answer has no version in it.\n");
        goto out;
    }
    if (date && strlen(date) >= 10) date[10] = 0;
    if (name && strcmp(name, tag)) printf("Latest release: %s (%s)\n", tag, name);
    else printf("Latest release: %s\n", tag);
    if (date) printf("Published:      %s\n", date);
    printf("Installed:      %s\n", have[0] ? have : "none");
    cmp = have[0] ? vercmp(have, tag) : -1;
    if (cmp >= 0 && !a[A_FORCE]) {
        printf(cmp == 0 ? "AmiBash is up to date.\n" : "The installed AmiBash is newer than the release.\n");
        rc = RETURN_OK;
        goto out;
    }
    if (notes && cmp < 0) {
        /* the first lines of the release notes */
        int lines = 0;
        char *l = notes, *e;
        printf("\n");
        while (l && *l && lines < 12) {
            e = strchr(l, '\n');
            if (e) *e = 0;
            printf("  %s\n", l);
            lines++;
            l = e ? e + 1 : NULL;
        }
        if (l && *l) printf("  ...\n");
        printf("\n");
    }
    if (!a[A_INSTALL]) {
        printf("A newer AmiBash is available. To install it: amibash-update INSTALL\n");
        rc = RETURN_WARN;
        goto out;
    }
    if (!dl) {
        printf("The release has no AmiBash .lha to download; see https://github.com/%s/releases\n", repo);
        goto out;
    }
    rc = install(dl) ? RETURN_OK : RETURN_ERROR;
out:
    free(s.buf);
    free(tag);
    free(name);
    free(date);
    free(notes);
    free(dl);
    ssl_close();
    FreeArgs(ra);
    return rc;
}
