x=outer
(x=inner; echo "in: $x")
echo "out: $x"
(cd /tmp && pwd)
pwd | grep -q tmp && echo "changed?" || echo "cwd kept"
(exit 5); echo "status $?"
(set -e; false; echo "not reached")
echo "after set -e subshell $?"
(f() { echo "sub func"; }; f)
type f >/dev/null 2>&1 || echo "f not defined outside"
(export NEWV=1); echo "NEWV=${NEWV:-unset}"
y=1; ( y=$((y+1)); ( y=$((y+1)); echo "deep $y" ); echo "mid $y" ); echo "top $y"
arr=(a b); (arr+=(c); echo "${#arr[@]}"); echo "${#arr[@]}"
(set -- p q r; echo "$#"); echo "$#"
out=$(z=9; echo $z); echo "$out ${z:-nz}"
(trap 'echo sub exit' EXIT; echo body)
echo "after trap sub"
( umask 077 ); echo "umask ok"
( shopt -s nullglob; echo nomatch* ); echo nomatch*
( alias zz=echo ) ; alias zz 2>/dev/null || echo "no alias zz"
{ v=group; }; echo "group: $v"
( unset x; echo "unset in sub: ${x-gone}" ); echo "x still $x"
(echo a; exit 2; echo b); echo "st $?"
echo $( (exit 4); echo $? )
( declare -A m=([k]=v); echo "${m[k]}" ); declare -p m 2>/dev/null || echo "m not outside"
