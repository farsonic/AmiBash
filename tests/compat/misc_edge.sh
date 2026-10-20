echo "${PWD:+x}" 
x=( $(printf 'a\nb c\n') ); echo ${#x[@]}
IFS=$'\n' y=( $(printf 'a\nb c\n') ); echo ${#y[@]}
declare -a z='(1 2 3)'; echo ${#z[@]}
read -r -a q <<< 'a "b c" d'; echo ${#q[@]}
eval "w=($(printf '%q ' 'a b' c))"; echo ${#w[@]}
printf -v 'arr[2]' '%s' val; echo "${arr[2]}"
declare -A m; printf -v 'm[k]' x; echo ${m[k]}
k=key; declare -A n; n[$k]=1; ((n[$k]++)); echo ${n[key]}
s=abc; echo ${s:1:1}${s: -1}
echo $(( $(wc -l < /dev/null) + 1 ))
cat <<EOF
$(for i in 1 2; do echo "row $i"; done)
EOF
v=$'a\nb'; [[ $v == *$'\n'* ]] && echo has-nl
echo ${v@Q}
u=$(printf '\t'); echo "${#u}"
t=$'x\ty'; echo "$t" | cut -f2
echo "a,b" | IFS=, read -r p q; echo "[$p]"
f() { local IFS=,; echo "$*"; local a=($1); echo ${#a[@]}; }; f "1,2,3"
g() { echo $#; }; g "${undefined_arr[@]}"
h=(); g "${h[@]}"; g "${h[@]:-}"
echo "$((10#08 + 1))"
c=0; for i in {1..100}; do ((c+=i)); done; echo $c
echo {1..3}{,}
echo a{1..3}b
echo $'a\'b'
echo "it's" 'a"b'
printf "%s\n" "$(echo "$(echo "a\"b")")"
echo '\\' "\\" \\
echo -e '\\n'
echo "$(echo '\\')"
echo `echo '\\'`
echo `echo "\\\\"`
cat <<EOF
unterminated heredoc
