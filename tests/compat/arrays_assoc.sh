# associative arrays
declare -A m
m[one]=1; m[two]=2; m["three four"]=34
echo "${m[one]}" "${m[two]}" "${m[three four]}"
echo "${#m[@]}"
for k in "${!m[@]}"; do echo "$k=${m[$k]}"; done | sort
printf '%s\n' "${m[@]}" | sort
unset 'm[one]'
echo "${#m[@]}"
[[ -v m[two] ]] && echo "two set"
[[ -v m[one] ]] || echo "one unset"
declare -A n=([a]=x [b]=y)
echo "${n[a]}${n[b]}"
n[a]+=z
echo "${n[a]}"
key=b
echo "${n[$key]}"
declare -A cnt
for w in a b a c a b; do cnt[$w]=$(( ${cnt[$w]:-0} + 1 )); done
for k in "${!cnt[@]}"; do echo "$k ${cnt[$k]}"; done | sort
declare -A e
echo "${#e[@]}"
e[x]=1
unset e
declare -p e 2>/dev/null || echo "e gone"
declare -A q=([k]="v w")
declare -p q
declare -A z
z[key with spaces]=yes
echo "${z[key with spaces]}"
(( cnt[a] += 10 ))
echo "${cnt[a]}"
echo "${cnt[a]:-0}" "${cnt[zz]:-0}"
