# indexed arrays
a=(one two three)
echo "${a[0]}" "${a[1]}" "${a[2]}" "$a"
echo "${#a[@]}" "${#a[1]}"
a[5]=five
echo "${#a[@]}" "${!a[@]}"
echo "${a[@]}"
echo "${a[-1]}" "${a[-2]-none}" "${a[@]: -1}"
unset 'a[1]'
echo "${!a[@]}" "${a[*]}"
a+=(six seven)
echo "${!a[@]}"
b=("${a[@]:1:2}")
echo "${b[@]}" "${#b[@]}"
c=([3]=c [1]=a b)
echo "${!c[@]}" "${c[@]}"
d=()
echo "${#d[@]}" "[${d[@]}]"
d[0]=x
d+=y
echo "${d[@]}"
IFS=,
echo "${a[*]}"
echo "${a[@]}"
unset IFS
e=("a b" c "d  e")
for x in "${e[@]}"; do echo "<$x>"; done
for x in ${e[@]}; do echo "($x)"; done
printf '%s\n' "${e[@]/#/pre-}"
printf '%s\n' "${e[@]%e}"
printf '%s\n' "${e[@]^^}"
f=(3 1 2)
f[1]+=0
echo "${f[@]}"
echo "${e[@]:1}"
echo "${e[@]:0:1}"
echo "${e[*]:1:2}"
declare -a g=(1 2 3)
g=("${g[@]}" 4)
echo "${g[@]}" $(( ${#g[@]} * 2 ))
h=(a b c d e)
echo "${h[@]:1:3}" "${h[@]: -2}" "${h[@]:(-3):2}"
unset h[0]
echo "${h[@]}"
unset h
echo "${#h[@]}"
i=(x); i[3]=y
echo "${i[@]}" "${#i[@]}"
words="alpha beta gamma"
w=($words)
echo "${#w[@]}" "${w[2]}"
echo "${a[@]+set}" "${zz[@]+set}"
