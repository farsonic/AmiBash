trap 'echo "EXIT trap ran"' EXIT
trap 'echo "ERR at $LINENO? status $?"' ERR
false
echo "after false"
true
f() { return 2; }
f
trap - ERR
false
echo "err trap removed"
trap 'echo int' INT
trap -p INT
trap - INT
trap -p INT; echo "no INT trap"
t() { trap 'echo "func exit trap"' EXIT; }
t
echo "main continues"
( trap 'echo "subshell exit"' EXIT; echo "in subshell" )
trap 'echo first' EXIT
trap 'echo "final EXIT"; echo "status was $?"' EXIT
trap 'echo ret' RETURN 2>/dev/null; trap - RETURN
if false; then :; fi
false || true
echo "cond false no ERR"
exit 4
