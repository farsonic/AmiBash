# bash 5.3 features and changes
echo "version ${BASH_VERSINFO[0]}.${BASH_VERSINFO[1]}"
printf "%Q|%#q|%#Q|%q\n" "a b" "c d" "e" "f'g"
printf "[%.3Q] [%10q] [%#q] [%#q]\n" "abcdef ghi" "a b" "" "it's"
# GLOBSORT
mkdir gs && cd gs
echo 12345 > a; touch b; echo 1 > c
GLOBSORT=size; echo *; GLOBSORT=-size; echo *; GLOBSORT=-name; echo *; GLOBSORT=; echo *
touch 10 2 1 03 x
GLOBSORT=numeric; echo *; GLOBSORT=-numeric; echo *; GLOBSORT=bogus; echo *
unset GLOBSORT; cd ..
# source -p
mkdir d; echo 'echo "from d: $*"' > d/s.sh; echo 'echo "from cwd"' > s.sh
source -p d s.sh one; . -p ":d" s.sh; source -p nowhere s.sh; echo "st $?"
source -x s.sh; echo "st $?"
# compgen -V
compgen -V arr -W "aa ab b" a; declare -p arr
# trap -P, BASH_TRAPSIG
trap "echo hi" INT; trap -P INT; trap -P TERM; trap "" QUIT; trap -P QUIT INT; trap - INT QUIT
trap 'echo "err trap $BASH_TRAPSIG"' ERR; false; trap - ERR; echo "after [${BASH_TRAPSIG-unset}]"
[[ $BASH_MONOSECONDS =~ ^[0-9]+$ ]] && echo monoseconds
# TIMEFORMAT (digits masked: times vary)
{ TIMEFORMAT="[%R|%lR|%0R|%1R|%6R|%%|%7R|%3lS]"; time sleep 0.01; } 2>&1 | sed 's/[0-9]/9/g'
{ TIMEFORMAT=; time true; } 2>&1; unset TIMEFORMAT
{ time -p true; } 2>&1 | sed 's/[0-9]/9/g'
# read -E, shopt
read -E w <<< hello; echo "read [$w]"
shopt array_expand_once bash_source_fullpath assoc_expand_once
# regular expressions that do not compile
re='('; [[ a =~ $re ]]; echo "st $?"; re='a{2,1}'; [[ aa =~ $re ]]; echo "st $?"; re='['; [[ a =~ $re ]]; echo "st $?"
# LINENO: the line a simple command starts on (assignments: ends on)
echo "multi
line $LINENO"
z="a
b $LINENO"; echo "$z"
eval 'echo "eval $LINENO"
echo "eval2 $LINENO"'
# unexpected end of file names the open command
eval 'for i in 1; do
echo'
eval '{ echo'
# integer expected; arithmetic syntax error
[ 1 -eq x ]; echo "st $?"
echo $(( 1 + )); echo $(( 5 = 3 )); echo $(( 1 @ 2 ))
f() { cat <<X
hd
X
! true
y=2
}
declare -f f
