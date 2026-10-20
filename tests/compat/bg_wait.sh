echo start
{ echo "bg job"; } &
wait
echo "after wait"
f() { echo "func bg $1"; }
f 1 &
wait $!
echo "status $?"
(exit 3) &
wait $!; echo "bg status $?"
x=5; { x=6; } & wait; echo "x=$x"
