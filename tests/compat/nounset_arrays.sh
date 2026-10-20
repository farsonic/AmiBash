set -u
a=(); echo "[${a[@]}]" "${#a[@]}"
echo "[$@]" "$#"
declare -A m; echo "${#m[@]}"
echo "${m[x]:-dflt}"
f() { echo "${1:-none}" "$#"; }; f
echo "${!m[@]}"
b=(1); echo "${b[0]}"
(echo "${b[5]}") 2>&1 | sed 's/^.*line [0-9]*: //'
(echo "$1") 2>&1 | sed 's/^.*line [0-9]*: //'
(echo "${m[q]}") 2>&1 | sed 's/^.*line [0-9]*: //'
(echo $((zz + 1))) 2>&1 | sed 's/^.*line [0-9]*: //'
(echo "${zz@Q}") 2>&1 | sed 's/^.*line [0-9]*: //'
(echo "${#zz}") 2>&1 | sed 's/^.*line [0-9]*: //'
echo "${zz-ok}" "${zz:+x}"
[[ -v zz ]] || echo "not set"
(for v in "$zz"; do :; done) 2>&1 | sed 's/^.*line [0-9]*: //'
echo end
