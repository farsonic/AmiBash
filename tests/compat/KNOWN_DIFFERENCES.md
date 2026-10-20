# Known differences between AmiBash and GNU bash

The compatibility suite (`tests/run_compat.sh`, `make test`) runs every
`tests/compat/*.sh` script under `/bin/bash --norc` and under
`build/amibash`, and compares stdout and the exit status exactly, and
stderr after normalising the script path / shell name prefix.

The checks below were left out of the suite on purpose: they are
deliberate Amiga adaptations or cannot be done without `fork()`.

## No concurrency

* `cmd &` runs the command to completion before the shell continues
  (external commands with no redirections are started with
  `os_run_bg()` and really run in the background).  `$!` is a
  pseudo-id for synchronous jobs; `wait $!` returns the remembered exit
  status, `wait` alone returns 0.
* Pipelines run their stages one after another, passing the output in a
  memory buffer.  Therefore a producer that never ends (`yes | head`,
  `cat /dev/zero | ...`) never terminates, and SIGPIPE-related exit
  statuses (141) never occur.  Tests avoid such pipelines.
* `coproc`, job control (`jobs`, `fg`, `bg`, `%1`), `>(...)` output
  process substitution are not supported.  `<(...)` is emulated with a
  temporary file.

## File system

* Symbolic links are not supported on AmigaOS: `ln -s` fails with
  "links are not supported on this system", so `test -L/-h` checks were
  removed from `test_bracket.sh`.
* `umask` is only remembered by the shell (AmigaOS has no file creation
  mask); it is printed and set like in bash and restored after
  subshells.

## Descriptors

* Only descriptors 0-9 exist.  `{var}>file` picks a free descriptor from
  9 downwards (bash uses 10 and up), so scripts must not print the
  number.

## Messages

* Error messages from the coreutils-style builtins (`cat`, `ls`, ...)
  are printed without the `script: line N:` prefix, like the external
  GNU programs, but their wording comes from the utility builtins and
  may differ in detail.
* `[[ a == ]]` reports "syntax error in conditional expression" instead
  of bash's three-line message.
* `eval 'x=(1 2'` (unterminated array assignment) returns 2; bash
  oddly returns 1.

## Other

* `$BASH_SUBSHELL` is 1 inside a script that is run as a command from
  another AmiBash script (the child runs in-process as an emulated
  subshell); bash starts a new process with level 0.
* `BASH_SOURCE` for a function frame is the file that was executing
  when the function was called, not the file where it was defined.
* `$'\u....'` produces UTF-8 on the host build but ISO-8859-1 on the
  Amiga (characters above 255 become `?`).
