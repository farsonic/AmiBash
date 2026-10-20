true; echo $?
false; echo $?
(exit 42); echo $?
f() { return 9; }; f; echo $?
nosuch_cmd_zz 2>/dev/null; echo $?
./nonexistent_script 2>/dev/null; echo $?
echo hi > /dev/null; echo $?
! false; echo $?
x=$(exit 6); echo $?
x=1; echo $?
[ 1 = 2 ]; echo $?
[[ a ]]; echo $?
(( 0 )); echo $?
false; true; echo $?
if false; then :; fi; echo $?
case x in y) ;; esac; echo $?
for i in; do :; done; echo $?
false; echo "$? $?"
v=$(false); echo "subst assign $?"
echo $(false) $?
touch noexec; ./noexec 2>/dev/null; echo "noexec $?"
mkdir dirx; ./dirx 2>/dev/null; echo "dir $?"
exit 3
