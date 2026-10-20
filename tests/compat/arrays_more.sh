a=(3 1 4 1 5 9 2 6)
echo "len ${#a[@]}"
sorted=($(printf '%s\n' "${a[@]}" | sort -n)); echo "${sorted[*]}"
max=${a[0]}; for v in "${a[@]}"; do (( v > max )) && max=$v; done; echo "max $max"
sum=0; for v in "${a[@]}"; do (( sum += v )); done; echo "sum $sum"
rev=(); for ((i=${#a[@]}-1; i>=0; i--)); do rev+=("${a[i]}"); done; echo "${rev[*]}"
uniq=(); declare -A seen; for v in "${a[@]}"; do [[ ${seen[$v]} ]] || { uniq+=("$v"); seen[$v]=1; }; done; echo "${uniq[*]}"
contains() { local n=$1; shift; local e; for e; do [[ $e == "$n" ]] && return 0; done; return 1; }
contains 5 "${a[@]}" && echo "has 5"; contains 7 "${a[@]}" || echo "no 7"
idx=(); for i in "${!a[@]}"; do [[ ${a[i]} == 1 ]] && idx+=($i); done; echo "ones at ${idx[*]}"
unset 'a[2]'; echo "${!a[*]}"; a=("${a[@]}"); echo "${!a[*]}"
b=("${a[@]:2:3}"); echo "${b[*]}"
c=("${a[@]/1/one}"); echo "${c[*]}"
matrix=(); for r in 0 1 2; do for col in 0 1 2; do matrix[r*3+col]=$((r*col)); done; done; echo "${matrix[*]}"
declare -A m=([x]=1 [y]=2 [z]=3)
keys=$(printf '%s\n' "${!m[@]}" | sort | tr '\n' ' '); echo "$keys"
vals=$(printf '%s\n' "${m[@]}" | sort -n | tr '\n' ' '); echo "$vals"
unset 'm[y]'; echo "${#m[@]}"
m+=([w]=0); echo "${#m[@]}"
for k in $(printf '%s\n' "${!m[@]}" | sort); do printf '%s=%s ' "$k" "${m[$k]}"; done; echo
arr=("a b" "c d"); copy=("${arr[@]}"); copy[0]=changed; echo "${arr[0]} / ${copy[0]}"
str="${arr[*]}"; echo "$str"
IFS=- eval 'joined="${arr[*]}"'; echo "$joined"
n=(); n[5]=x; n[2]=y; echo "${n[@]} ${!n[@]}"
echo "${n[@]:3}"
printf '[%s]' "${arr[@]}"; echo
printf '[%s]' "${arr[*]}"; echo
z=(); z+=(); echo "${#z[@]}"
w=(one); w[0]+=more; echo "${w[0]}"
x=([0]=a [2]=c); x[1]=b; echo "${x[*]}"
declare -a y; y[3]=q; echo "${#y[@]} ${y[3]}"
q=(a b c); echo "${q[@]: -2:1}"
p=( $(echo "1 2 3") ); echo "${#p[@]}"
o=("$(echo "1 2 3")"); echo "${#o[@]}"
