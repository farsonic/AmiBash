set -x
echo hello
x=5
y="a b"
echo "$y" $x
f() { echo in f; }
f arg
for i in 1 2; do :; done
[[ $x == 5 ]]
(( x++ ))
set +x
echo quiet
PS4='>> '
set -x
echo custom ps4
{ set +x; } 2>/dev/null
echo done
