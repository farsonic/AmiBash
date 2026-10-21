/*
 * main.c - AmiBash entry point and read/parse/execute loop.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sh.h"

#ifdef AMIBASH_PURE
#define main sh_main        /* the start-up code is in pure_amiga.c */
#elif defined(AMIGA)
/* the shell recurses (parser, expansion, functions): ask for a big stack */
size_t __stack = 65536;     /* use the Stack command to raise it */
#endif
#ifdef AMIGA
static const char ver[] = "$VER: bash " AMIBASH_SHORTVER " (" AMIBASH_DATE ") AmiBash " AMIBASH_BUILD " build";
#endif

Shell sh;

void sh_exit(int status)
{
    run_exit_trap();
    sh_flush_all();
    if (sh.interactive && sh.opt_history) {
        const char *hf = var_get("HISTFILE");
        if (hf && *hf) hist_save(hf);
    }
    os_console_detach();
    os_cleanup();
    os_exit(status);
}

const char *sh_basename_cmd(void)
{
    return sh.name ? path_basename(sh.name) : "bash";
}

/* ---- the read/parse/execute loop ---- */

static int read_line(ShFile *in, SBuf *line, const char *ps, int interactive)
{
    if (interactive && in->kind == SHF_OS && in->tty) {
        char *prompt;
        if (var_get("AMIBASH_TERM") && ps == var_get("PS1")) {
            /* tab title in the AmiBash Terminal (xterm title sequence) */
            char *t = prompt_expand("bash: \\w");
            sh_printf(1, "\033]0;%s\007", t);
            xfree(t);
        }
        prompt = prompt_expand(ps);
        char *l = le_readline(prompt);
        xfree(prompt);
        if (!l) return 0;
        sb_clear(line);
        sb_adds(line, l);
        xfree(l);
        return 1;
    }
    if (interactive) {
        char *prompt = prompt_expand(ps);
        sh_puts(2, prompt);
        shf_flush(sh_fd(2));
        xfree(prompt);
    }
    return shf_getline(in, line, NULL);
}

int sh_repl(ShFile *in, int interactive)
{
    SBuf buf, line;
    int lineno = 1;
    sb_init(&buf);
    sb_init(&line);
    for (;;) {
        int r, first = 1, eof = 0;
        Arena *a;
        Parser *p;
        sb_clear(&buf);
        sb_addn(&buf, "", 0);
        sh.flow = FLOW_NONE;
        if (interactive) procsub_cleanup();
        /* accumulate lines until the input parses completely */
        for (;;) {
            const char *ps;
            if (interactive) {
                ps = first ? var_get("PS1") : var_get("PS2");
                if (!ps) ps = first ? "$ " : "> ";
            } else {
                ps = "";
            }
            le_cancelled = 0;
            if (!read_line(in, &line, ps, interactive)) {
                eof = 1;
                break;
            }
            if (le_cancelled) {
                /* ^C: drop everything typed so far */
                sb_clear(&buf);
                first = 1;
                continue;
            }
            if (interactive && sh.opt_histexpand) {
                int err, changed;
                char *x = hist_expand(sb_str(&line), &err, &changed);
                if (err) {
                    sb_clear(&buf);
                    xfree(x);
                    first = 1;
                    goto next;
                }
                if (changed) {
                    sh_printf(2, "%s\n", x);
                    sb_clear(&line);
                    sb_adds(&line, x);
                }
                xfree(x);
            }
            if (sh.opt_verbose) sh_printf(2, "%s\n", sb_str(&line));
            sb_adds(&buf, sb_str(&line));
            sb_addc(&buf, '\n');
            first = 0;
            /* dry-run parse to see if the command is complete */
            {
                Arena *ta = arena_new();
                Parser *tp = parser_new(buf.s, ta, 1);
                Node *n;
                int pr;
                Arena *save = exec_arena;
                (void)save;
                do {
                    pr = parser_next(tp, &n);
                } while (pr == PARSE_OK);
                parser_free(tp);
                arena_free(ta);
                if (pr != PARSE_INCOMPLETE) break;
            }
        }
        if (eof && buf.len == 0) break;
        if (interactive && buf.len > 1) {
            char *h = xstrndup(buf.s, buf.len - 1);
            char *q;
            for (q = h; *q; q++)
                if (*q == '\n') *q = sh.sh_cmdhist ? ' ' : *q;
            if (sh.opt_history) hist_add(h);
            xfree(h);
        }
        /* execute */
        os_check_break();
        os_break_flag = 0;
        a = arena_new();
        p = parser_new(buf.s, a, lineno);
        exec_arena = a;
        for (;;) {
            Node *n;
            r = parser_next(p, &n);
            if (r == PARSE_EOF) break;
            if (r != PARSE_OK) {
                sh.lineno = parser_lineno(p);
                if (r == PARSE_INCOMPLETE) sh_error("syntax error: unexpected end of file");
                else report_syntax_error(p, NULL);
                sh.laststatus = 2;
                if (!interactive) {
                    parser_free(p);
                    arena_free(a);
                    sb_free(&buf);
                    sb_free(&line);
                    return 2;
                }
                break;
            }
            if (n) {
                exec_node(n);
                sh_flush_all();
                if (sh.flow == FLOW_EXIT) {
                    parser_free(p);
                    arena_free(a);
                    sb_free(&buf);
                    sb_free(&line);
                    return sh.exitstatus;
                }
                if (sh.flow == FLOW_INTR) {
                    sh.flow = FLOW_NONE;
                    if (!interactive) {
                        parser_free(p);
                        arena_free(a);
                        return 130;
                    }
                    break;
                }
                sh.flow = FLOW_NONE;
            }
        }
        lineno = parser_lineno(p);
        parser_free(p);
        exec_arena = NULL;
        arena_free(a);
        if (eof) break;
    next:;
    }
    sb_free(&buf);
    sb_free(&line);
    if (interactive) sh_puts(2, "exit\n");
    return sh.laststatus;
}

static void source_if_exists(const char *path)
{
    if (path && sh_exists(path) && !sh_is_dir(path)) {
        exec_file(path, NULL, 0, 1);
        sh.flow = FLOW_NONE;
    }
}

static void usage(void)
{
    sh_puts(2, "Usage: bash [option] ... [-c command | script-file] [arguments ...]\n"
               "Options: -c cmd  -i  -l  -s  -e  -u  -x  -v  -n  --version  --norc  SELFTEST\n");
}

int main(int argc, char **argv)
{
    int i, status = 0;
    const char *cmdstr = NULL;
    int force_inter = 0, read_stdin = 0, norc = 0, noprofile = 0;
    char cwd[512];

    os_init();
    if (os_wb_launch()) return 0;      /* Workbench: opened a Bash window */
    io_init();
    memset(&sh, 0, sizeof(sh));
    sh.opt_braceexpand = 1;
    sh.opt_hashall = 1;
    sh.umask = 022;
    sh.opt_emacs = 1;
    sh.opt_history = 1;
    sh.sh_pathconv = 1;
    sh.sh_cmdhist = 1;
    sh.sh_checkwinsize = 1;
#ifdef AMIGA
    sh.sh_nocaseglob = 1;
#endif
    sh.pid = os_pid();
    sh.starttime = os_time();
    sh.name = argc > 0 && argv[0] ? argv[0] : "bash";
    sh.name = "bash";
    vars_init();

    /* current directory */
    if (os_getcwd(cwd, sizeof(cwd))) sh.pwd = xstrdup(cwd);
    else sh.pwd = xstrdup(".");
    {
        const char *envpwd = var_get("PWD");
#ifndef AMIGA
        /* keep a logical $PWD from the environment if it is the same dir */
        if (envpwd && envpwd[0] == '/') {
            char *a = os_fullpath(envpwd), *b = os_fullpath(sh.pwd);
            if (a && b && strcmp(a, b) == 0) {
                xfree(sh.pwd);
                sh.pwd = xstrdup(envpwd);
            }
            xfree(a);
            xfree(b);
        }
#else
        (void)envpwd;
#endif
    }
    var_set("PWD", sh.pwd, V_EXPORT);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "SELFTEST") == 0 || strcmp(a, "--selftest") == 0) {
            sh_exit(sh_run_selftest());
        }
        if (strcmp(a, "--version") == 0) {
            sh_printf(1, "AmiBash %s (%s build, %s), GNU bash %s compatible\n", AMIBASH_SHORTVER, AMIBASH_BUILD, AMIBASH_DATE, AMIBASH_VERSION);
            sh_puts(1, "A bash-compatible shell for AmigaOS (no ixemul needed).  License GPLv3+\n");
            sh_flush_all();
            return 0;
        }
        if (strcmp(a, "--help") == 0) {
            usage();
            sh_flush_all();
            return 0;
        }
#ifdef AMIGA
        if (strcmp(a, "--term") == 0) {
            /* open the tabbed AmiBash Terminal */
            extern const char *os_self_path(void);
            return term_main(os_self_path());
        }
#endif
        if (strcmp(a, "--amiterm") == 0) {
            /* running in a tab of the AmiBash Terminal */
            var_set("AMIBASH_TERM", "1", V_EXPORT);
            continue;
        }
        if (strcmp(a, "--norc") == 0) {
            norc = 1;
            continue;
        }
        if (strcmp(a, "--noprofile") == 0) {
            noprofile = 1;
            continue;
        }
        if (strcmp(a, "--login") == 0) {
            sh.login = 1;
            continue;
        }
        if (strcmp(a, "--posix") == 0) {
            sh.opt_posix = 1;
            continue;
        }
        if (strcmp(a, "--") == 0 || strcmp(a, "-") == 0) {
            i++;
            break;
        }
        if ((a[0] == '-' || a[0] == '+') && a[1]) {
            const char *p;
            int on = a[0] == '-';
            for (p = a + 1; *p; p++) {
                switch (*p) {
                case 'c':
                    if (i + 1 >= argc) {
                        sh_puts(2, "bash: -c: option requires an argument\n");
                        sh_flush_all();
                        return 2;
                    }
                    cmdstr = argv[++i];
                    break;
                case 'i': force_inter = on; break;
                case 'l': sh.login = on; break;
                case 's': read_stdin = 1; break;
                case 'e': sh.opt_errexit = on; break;
                case 'u': sh.opt_nounset = on; break;
                case 'x': sh.opt_xtrace = on; break;
                case 'v': sh.opt_verbose = on; break;
                case 'n': sh.opt_noexec = on; break;
                case 'f': sh.opt_noglob = on; break;
                case 'C': sh.opt_noclobber = on; break;
                case 'a': sh.opt_allexport = on; break;
                case 'h': sh.opt_hashall = on; break;
                case 'B': sh.opt_braceexpand = on; break;
                case 'o':
                    if (i + 1 < argc) {
                        char *sv[3];
                        sv[0] = "set";
                        sv[1] = on ? "-o" : "+o";
                        sv[2] = argv[++i];
                        {
                            const Builtin *b = builtin_lookup("set");
                            if (b) b->fn(3, sv);
                        }
                    }
                    break;
                default:
                    sh_printf(2, "bash: -%c: invalid option\n", *p);
                    usage();
                    sh_flush_all();
                    return 2;
                }
            }
            if (cmdstr) {
                i++;
                break;
            }
            continue;
        }
        break;
    }

    if (cmdstr) {
        /* bash -c 'cmd' name args... */
        if (i < argc) {
            sh.name = argv[i];
            params_set(argv + i + 1, argc - i - 1);
        }
        sh.interactive = force_inter;
        status = exec_string(cmdstr, "-c");
        if (sh.flow == FLOW_EXIT || sh.flow == FLOW_ABORT) status = sh.exitstatus;
        sh_exit(status);
    }

    if (i < argc && !read_stdin) {
        /* script file */
        sh.name = argv[i];
        sh.script = argv[i];
        if (!sh_exists(argv[i])) {
            sh_printf(2, "bash: %s: No such file or directory\n", argv[i]);
            sh_flush_all();
            return 127;
        }
        status = exec_file(argv[i], argv + i + 1, argc - i - 1, 0);
        if (sh.flow == FLOW_EXIT) status = sh.exitstatus;
        sh_exit(status);
    }
    if (i < argc) params_set(argv + i, argc - i);

    sh.interactive = force_inter || (os_isatty(os_stdin()) && os_isatty(os_stdout()));
    if (sh.interactive) {
        sh.sh_expand_aliases = 1;
        sh.opt_histexpand = 1;
        sh.opt_monitor = 0;
        if (!var_get("PS1")) {
#ifdef AMIGA
            /* the console shows colour 33 in pen 3 (blue on Workbench);
             * the AmiBash Terminal shows real ANSI colours */
            if (var_get("AMIBASH_TERM"))
                var_set("PS1", "\\[\\e[1m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\$ ", 0);
            else
                var_set("PS1", "\\[\\e[1m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[33m\\]\\w\\[\\e[0m\\]\\$ ", 0);
#else
            var_set("PS1", "\\u@\\h:\\w\\$ ", 0);
#endif
        }
        if (!var_get("HISTFILE")) {
            const char *home = var_get("HOME");
            if (home) {
                char *hf = path_join(home, ".bash_history");
                var_set("HISTFILE", hf, 0);
                xfree(hf);
            }
        }
        if (!var_get("HISTFILESIZE")) var_set("HISTFILESIZE", "500", 0);
        {
            const char *hf = var_get("HISTFILE");
            if (hf) hist_load(hf);
        }
        if (sh.login && !noprofile) {
#ifdef AMIGA
            source_if_exists("S:bash_profile");
#else
            source_if_exists("/etc/profile");
#endif
            {
                const char *home = var_get("HOME");
                if (home) {
                    char *p = path_join(home, ".bash_profile");
                    source_if_exists(p);
                    xfree(p);
                }
            }
        }
        if (!norc) {
#ifdef AMIGA
            source_if_exists("S:bashrc");
#endif
            {
                const char *home = var_get("HOME");
                if (home) {
                    char *p = path_join(home, ".bashrc");
                    source_if_exists(p);
                    xfree(p);
                }
            }
        }
    }
    if (sh.interactive) os_console_attach();   /* window title "Bash" */
    status = sh_repl(sh_fd(0), sh.interactive);
    sh_exit(status);
    return status;
}
