set -e
f() { false && true; }
f; echo "f returned $? (and-list last not fatal)"
g() { true && false; }
g || echo "g failed handled"
h() { local x=$(false); echo "local masks status"; }
h
x=$(false) || echo "assignment status checked"
[[ 1 -eq 2 ]] || echo "cond handled"
for i in 1 2; do false || continue; done
echo "loop ok"
if ! false; then echo "negation ok"; fi
case x in x) false || true;; esac
echo "case ok"
( set +e; false; echo "sub +e" )
set +e
a=5; b=abc; [[ $a -eq 5 ]] && echo "num eq"
[[ $b -eq 0 ]] && echo "abc as arith is 0"
c=a; a2=3; [[ c -lt 2 ]] && echo "c->a->5 not less" || echo "c resolves to 5"
[[ 1+2 -eq 3 ]] && echo "expr in [["
[ "$b" = abc ] && echo "string test"
set -e
trap 'echo "ERR in cond? no"' ERR
if false; then :; fi
while false; do :; done
false || true
trap - ERR
echo "end"
false
echo "unreached"
