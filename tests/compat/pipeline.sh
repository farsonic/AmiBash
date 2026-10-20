echo hello | tr a-z A-Z
printf '3\n1\n2\n' | sort
printf 'a\nb\nc\n' | wc -l
echo a | cat | cat | cat
true | false; echo "status $?"
false | true; echo "status $?"
true | false | true; echo "${PIPESTATUS[@]}"
false | false | true; echo "${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
set -o pipefail
true | false | true; echo "pipefail $?"
false | true; echo "pipefail2 $?"
(exit 3) | (exit 0); echo "pipefail3 $?"
set +o pipefail
! true; echo "not true $?"
! false; echo "not false $?"
! true | false; echo "not pipe $?"
x=1; echo foo | x=2; echo "x=$x"
echo data | while read -r d; do echo "got $d"; done
n=0; printf '1\n2\n3\n' | while read -r v; do n=$((n+v)); done; echo "n=$n"
shopt -s lastpipe 2>/dev/null; echo "lastpipe set"
echo abc | { read -r v; echo "in group $v"; }
echo one two | read -r a b; echo "a=[$a]"
seq 1 5 | grep -v 3 | tr '\n' ' '; echo
echo "x y z" | awk '{print $2}' 2>/dev/null || echo "y"
echo stderr 2>&1 >&2 | cat
{ echo e1 >&2; echo o1; } 2>&1 | sort
echo "a:b:c" | cut -d: -f2
printf 'b\na\nb\nc\n' | sort | uniq -c | sed 's/^ *//'
echo hi |& cat
echo "${PIPESTATUS[*]}"
