set -e
trap 'echo "ERR trap: $?"' ERR
f() { echo "in f"; return 1; }
if f; then echo yes; else echo "f returned nonzero"; fi
f || echo "handled"
while f; do break; done
echo "loop survived"
g() { false; echo "g continues because called in condition"; }
g && echo "g ok"
x=$(echo a; false; echo b); echo "x=[$x]"
h() { set +e; false; echo "h survived with +e"; set -e; }
h
for i in 1 2; do true; done
[ 1 -eq 2 ] || echo "test fail handled"
{ false; } || echo "group fail handled"
echo "about to fail"
(false)
echo "never printed"
