#!/bin/bash
# run_compat.sh - run every tests/compat/*.sh script under GNU bash and
# under AmiBash, compare stdout + exit status strictly and stderr after
# normalising the program-name / script-path prefixes.
#
#   tests/run_compat.sh [-v] [-q] [name-filter...]
#     -v   show stderr diffs too        -q  only the summary
#
# Environment: BASH_REF (default /bin/bash), AMIBASH (default build/amibash)

cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
REF=${BASH_REF:-/bin/bash}
SUT=${AMIBASH:-$ROOT/build/amibash}
TIMEOUT=${TIMEOUT:-10}
verbose=0 quiet=0
while [ $# -gt 0 ]; do
    case $1 in
        -v) verbose=1 ;;
        -q) quiet=1 ;;
        *) break ;;
    esac
    shift
done

[ -x "$SUT" ] || { echo "run_compat: $SUT not built" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/amibash-compat.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# stderr normalisation: script path and shell name prefixes
norm_err() {
    sed -e "s#$2#SCRIPT#g" \
        -e 's#^/bin/bash: #SH: #' -e 's#^bash: #SH: #' \
        -e "s#^$SUT: #SH: #" -e 's#^amibash: #SH: #' \
        -e "s#$1#WORKDIR#g" "$3"
}

run_one() { # shell script outdir
    local sh=$1 script=$2 out=$3 dir=$WORK/wd
    rm -rf "$dir" "$WORK/home"
    mkdir -p "$dir" "$WORK/home"
    ( ulimit -f 20000 2>/dev/null; cd "$dir" && env -i PATH=/usr/bin:/bin HOME="$WORK/home" LC_ALL=C TERM=dumb \
        USER=tester LANG=C TZ=UTC \
        timeout "$TIMEOUT" "$sh" --norc "$script" a1 "b 2" \
        </dev/null >"$out/stdout" 2>"$out/stderr" )
    echo $? >"$out/status"
}

pass=0 fail=0 warn=0 total=0
failed_names=""
for script in "$ROOT"/tests/compat/*.sh; do
    name=$(basename "$script" .sh)
    if [ $# -gt 0 ]; then
        m=0
        for f in "$@"; do case $name in *$f*) m=1 ;; esac; done
        [ $m = 1 ] || continue
    fi
    total=$((total + 1))
    a=$WORK/$name/ref b=$WORK/$name/sut
    mkdir -p "$a" "$b"
    run_one "$REF" "$script" "$a"
    run_one "$SUT" "$script" "$b"
    ok=1
    if ! cmp -s "$a/stdout" "$b/stdout"; then ok=0; fi
    if ! cmp -s "$a/status" "$b/status"; then ok=0; fi
    norm_err "$WORK" "$script" "$a/stderr" >"$a/stderr.n"
    norm_err "$WORK" "$script" "$b/stderr" >"$b/stderr.n"
    errdiff=0
    cmp -s "$a/stderr.n" "$b/stderr.n" || errdiff=1
    if [ $ok = 1 ]; then
        pass=$((pass + 1))
        if [ $errdiff = 1 ]; then
            warn=$((warn + 1))
            [ $quiet = 1 ] || echo "WARN  $name (stderr differs)"
            if [ $verbose = 1 ]; then
                diff -u --label bash --label amibash "$a/stderr.n" "$b/stderr.n" | head -40
            fi
        else
            [ $quiet = 1 ] || echo "ok    $name"
        fi
    else
        fail=$((fail + 1))
        failed_names="$failed_names $name"
        if [ $quiet = 0 ]; then
            echo "FAIL  $name"
            if ! cmp -s "$a/status" "$b/status"; then
                echo "  exit status: bash=$(cat "$a/status") amibash=$(cat "$b/status")"
            fi
            diff -u --label bash --label amibash "$a/stdout" "$b/stdout" | head -60 | sed 's/^/  /'
            if [ $errdiff = 1 ]; then
                echo "  -- stderr --"
                diff -u --label bash --label amibash "$a/stderr.n" "$b/stderr.n" | head -30 | sed 's/^/  /'
            fi
        fi
    fi
done

echo
echo "compat: $pass/$total passed, $fail failed, $warn with stderr differences"
[ -n "$failed_names" ] && echo "failed:$failed_names"
[ $fail = 0 ]
