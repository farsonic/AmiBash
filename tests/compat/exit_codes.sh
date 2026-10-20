(exit 0); echo $?
(exit 255); echo $?
(exit 256); echo $?
(exit 257); echo $?
(exit -1); echo $?
f() { return 255; }; f; echo $?
bash_true=$(true; echo $?); echo $bash_true
$(exit 4); echo "empty cmd from subst: $?"
x=$(exit 5) y=$(exit 6); echo "two assigns: $?"
echo $( (exit 7) ; echo $? )
false | true; echo $?
if (exit 3); then :; else echo "else got $?"; fi
while (exit 2); do :; done; echo "while $?"
case a in a) (exit 4);; esac; echo "case $?"
{ (exit 9); }; echo "group $?"
! (exit 3); echo "not $?"
(( 256 )); echo "arith $?"
[[ -n "" ]]; echo "cond $?"
test 1 -gt 2; echo "test $?"
exit 300
