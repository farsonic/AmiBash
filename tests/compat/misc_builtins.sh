umask 022; umask
umask -S 2>/dev/null | head -1
x=1; export x; env | grep -c '^x=1$'
export -n x; env | grep -c '^x=1$'
export Y=2; printenv Y
readonly RA=(1 2); echo "${RA[1]}"
RA[0]=9 2>/dev/null; echo "${RA[0]}"
let a=2+3 b=a*2; echo "$a $b"
declare -i i=10; i+=5; echo $i
declare -u U; U=shout; echo $U
declare -l L; L=WHISPER; echo $L
true; echo "true $?"; false; echo "false $?"
: ignored args; echo "colon $?"
echo "$(type -t :)"
v=$(command -v printf); echo "${v:-none}"
wait; echo "wait $?"
sleep 0; echo "slept"
times >/dev/null 2>&1; echo "times ok"
enable -n test 2>/dev/null; enable test 2>/dev/null; echo "enable ok"
help cd >/dev/null 2>&1; echo "help ok $?"
caller 2>/dev/null; echo "caller outside $?"
f() { caller 0 | awk '{print $2}' 2>/dev/null; }; f >/dev/null
echo "${BASH_VERSINFO[0]}"
[ -n "$BASH_VERSION" ] && echo "has version"
echo "${#RANDOM}" | grep -q '[0-9]' && echo "random ok"
SECONDS=0; [ "$SECONDS" -lt 5 ] && echo "seconds ok"
echo $((LINENO > 0))
printf '%s\n' "${FUNCNAME[@]:-nofunc}"
exec 2>&1; echo "exec redirect ok"
eval 'echo $((1+1))'
echo "${HOME:+home set}"
unset -v x; echo "${x-unset}"
