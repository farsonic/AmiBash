# quoting stress
a='x'
echo "'$a'" '"$a"' "\"$a\"" "'\''"
echo "${a}"'${a}'"\${a}"
echo $'\'$a\''
echo "$(echo "'")" "$(echo '"')"
echo "`echo \"in bt\"`"
echo "a\
b"
echo 'a\
b'
echo a\
b
b="with  spaces"; c=$b; echo "$c"
d="$b"; e=${b}; echo "$d|$e"
f=("$b"); echo ${#f[@]}
g=($b); echo ${#g[@]}
echo "${b// /_}" ${b// /_}
h='*'; echo "$h" $h | head -c 1; echo
mkdir qd && cd qd; touch q1 q2; echo $h; echo "$h"; echo '$h' "\$h"
i=$'tab\there'; echo $i "$i"
j="a'b\"c"; echo "$j" "${j//\'/X}" "${j//\"/Y}"
k='\n'; echo "$k" -e "$k"
printf '%s\n' "$k"
echo "$(printf '%s' '$(not expanded)')"
l="semi;colon"; echo $l
m='$(echo injected)'; echo $m "$m"
n='`echo bt`'; echo $n
eval "o='single in eval'"; echo "$o"
eval "p=\"dq in eval \$a\""; echo "$p"
q=\"; echo "$q" ${q}
r=\'; echo "$r"
echo \\\\ "\\\\" '\\\\'
echo "\$" '\$' \$
echo "#" '#' \# x#y
echo "{}" '{a,b}' \{a,b\} "{a,b}"
echo "~" '~' \~
echo "*" '*' \*
