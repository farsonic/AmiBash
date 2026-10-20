echo "lvl $BASH_SUBSHELL"; ( echo "lvl $BASH_SUBSHELL"; ( echo "lvl $BASH_SUBSHELL" ) ); echo "$(echo lvl $BASH_SUBSHELL)"
( umask 077 ); umask
( shopt -s nullglob; shopt -q nullglob && echo "in on" ); shopt -q nullglob || echo "out off"
( set -o pipefail; set -o | grep pipefail | tr -s ' \t' ' ' ); set -o | grep pipefail | tr -s ' \t' ' '
mkdir -p pd; echo x | (cd pd; pwd | sed 's#.*/##'); pwd | sed 's#.*/##' | grep -vq pd && echo "cwd unchanged"
echo a | { exit 3; }; echo "after exit in stage $?"
v=1; echo | v=2; echo "v=$v"
v=1; echo | { v=3; }; echo "v=$v"
( trap 'echo T' INT; trap -p INT ); trap -p INT; echo "no INT trap outside"
( readonly RO_SUB=1 ); RO_SUB=2; echo "RO_SUB=$RO_SUB"
( declare -A A_SUB; A_SUB[x]=1 ); declare -p A_SUB 2>/dev/null || echo "A_SUB not outside"
( export ESUB=1 ); env | grep -c ESUB=
( IFS=:; ); printf '%q\n' "$IFS"
( set -- a b ); echo "$#"
( cd /; cd - >/dev/null; pwd | grep -c / )
( pushd / >/dev/null ); dirs | wc -w
n=0; for i in 1 2 3; do ( n=$((n+i)) ); done; echo "n=$n"
( OPTIND=5 ); echo "OPTIND=$OPTIND"
x=$( (exit 7); echo $? ); echo "$x"
f() { ( return 3 ); echo "ret in sub $?"; }; f
