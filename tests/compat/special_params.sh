set -- "hello world" foo Bar
echo "${@^^}" "${*,}" "${@^}"
echo "${#@}" "${@:2:1}" "${@/o/0}"
echo "${@#h}" "${*%o}"
declare -A m; k="a b"; m[$k]=1; (( m[$k]++ )); echo "${m[$k]}"
(( m["c d"] = 5 )); echo "${m[c d]}"
i=1; a=(); (( a[i]=7, a[i+1]=8 )); echo "${a[@]}"
echo $(( a[1] * a[2] ))
x=3; echo $(( x += x *= 2 )) $x
IFS=: read -ra f <<< "a::b:"; echo "${#f[@]}" "${f[1]:-empty}"
IFS=: read -ra g <<< ":lead"; echo "${#g[@]}" "[${g[0]}]"
IFS=' :' read -ra h <<< " a : b "; echo "${#h[@]}" "${h[*]}"
printf '%b' 'stop\cnot printed'; echo
printf '%s\c\n' a 2>/dev/null; echo
echo "a\cb" | cat
echo -e "a\cb"; echo
type -a echo | head -2
type -a true
f() { :; }; type -a f
hash 2>/dev/null; echo "hash $?"
kill -0 $$ && echo "kill -0 ok"
kill -l 9 2>/dev/null || echo KILL
trap 'echo usr1' USR1 2>/dev/null; trap -p USR1 | head -1
history 2>/dev/null | wc -l
