printf '%(%Y-%m-%d %H:%M:%S)T\n' 0
printf '%(%A %B %d)T\n' 86400
printf '%(%s)T\n' 1000
printf '[%(%H)T]\n' 3600
x=$(exit 3; echo no); echo "x=[$x] $?"
y=$(trap 'echo trapped' EXIT; echo body); echo "$y"
( trap 'echo "sub exit $?"' EXIT; exit 4 ); echo "after $?"
f() { trap 'echo "trap in f"' EXIT; }; ( f; echo "in sub" )
trap 'echo main-exit' EXIT
trap
g() { exit 5; }
( g ); echo "g sub $?"
echo $( exit 2 ) $?
eval 'exit_in_eval() { return 4; }'; exit_in_eval; echo $?
command -p echo "command -p"
exec echo "exec replaces shell"
echo "not reached"
