printf '%d\n' 1.5
printf '%d\n' abc
printf '%d\n' 12abc
printf '%x\n' -1
printf '%o\n' 9
printf '%s %s %s\n' a b
printf '%z\n' 1
printf '%-3s|%3s|\n' ab ab
printf '%.0s' x; echo none
printf '%c' ""; echo "|"
printf '%5s\n' ''
printf "%s\n" "a\tb"
printf '%b\n' "a\tb" "\101" "\0101" "\x41"
printf '%08.3f\n' 3.14159
printf '%-8d|\n' -42
printf '%+.2e\n' 1234.5
printf '%G\n' 0.00001
printf '%i\n' 0x10
printf '%d %d\n' 1
echo -e "\0101"
echo -e "\x41\x4g"
echo -e '☺' | od -An -tx1 | tr -s ' '
echo -n -e "a\n" | od -c | head -1
echo -En 'x\n'; echo
shift 2 3 2>&1
shift -1 2>&1
cd a b 2>&1
cd /nonexistent 2>&1
pwd -X 2>&1 | head -1
read -z 2>&1 | head -1
declare -Z x 2>&1
local y 2>&1
unset -x 2>&1
export -q 2>&1 | head -1
alias -x 2>&1 | head -1
type -z ls 2>&1 | head -1
let 2>&1
test 1 -eq 2>&1
[ 1 2>&1
[ 1 -eq 1 1 ] 2>&1
echo $?
