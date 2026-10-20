set -e
true
echo "start"
false || echo "or-ed false ok"
if false; then echo no; fi
while false; do :; done
! true
echo "bang ok"
f() { false; echo "in f after false (not reached when not in cond)"; }
f || echo "f failed in || context"
x=$(false; echo "subst continues")
echo "x=$x"
( false ) || echo "subshell fail handled"
false && echo "not printed"
echo "&& list ok"
cnt=0
(( cnt++ )) || true
echo "cnt=$cnt"
set +e
false; echo "+e continues $?"
set -e
g() { echo "g start"; false; echo "g not reached"; }
g
echo "never"
