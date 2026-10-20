set -- a b c d e
echo "$# $1"
shift; echo "$# $1"
shift 2; echo "$# $*"
shift 5; echo "shift too far $?"
echo "$#"
set -- "x y" z
echo "$#"; for a in "$@"; do echo "<$a>"; done
set --; echo "empty $#"
set -- 1 2 3 4 5 6 7 8 9 10 11
echo "${10} ${11} $10"
echo "${@:2:3}" "${*:9}" "${@: -2}"
echo "${#@}" "${#*}"
f() { echo "in f: $# $@"; set -- inner; echo "after set: $1"; }
f p q; echo "outer still $1"
set -- a; shift 0; echo "$1"
set -a; AUTO=1; set +a; env | grep -c '^AUTO=1$'
echo "$-" | grep -q h && echo "has h" || echo "no h"
set -u; (echo "${undefined_zz}") 2>/dev/null || echo "set -u caught"; set +u
set -o | grep -E '^(errexit|nounset|pipefail) ' | tr -s ' \t' ' '
set -- -dash; echo "$1"
set -- --; echo "$# [$1]"
set - ; echo "after set - $#"
args=("$@")
IFS=: ; echo "$*"; unset IFS
