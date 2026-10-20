# more edge cases
a="x"; b=${a:-"y z"}; echo "$b"
echo ${a:+'q r'}
c=("${a:-1 2}"); echo ${#c[@]}
d=(${a:+1 2}); echo ${#d[@]}
e="$@"; echo "[$e]"
set -- p q; e="$@"; echo "[$e]"; e=$*; echo "[$e]"
IFS=,; e="$*"; echo "[$e]"; unset IFS
f=$@; echo "[$f]"
g=( "$@" ); echo ${#g[@]}
h='$a'; echo ${h}
echo "${a}b" "$a"b ${a}b
i=10; echo $i0 ${i}0
echo $1$2 ${1}${2}
echo "$#$#"
echo $-x | grep -c h
echo "\$a=$a"
j=1; k=$j+1; echo $k
declare -i l; l=$j+1; echo $l
m="  lead"; echo $m "$m"
n='a  b'; echo $n; echo "$n"
o=$(echo "  x  "); echo "[$o]"
p=`echo '$a'`; echo "$p"
q=$(echo '$a'); echo "$q"
echo $(echo \$a)
echo `echo \$a`
r="a\$b"; echo "$r"
echo "\q \$ \\ \" \`"
echo '\q \$ \\ \" \`'
echo \q \$ \\ \" \`
s=$'\x41\n\x42'; echo "$s"
echo "${s//$'\n'/ }"
t="a b c"; set -- $t; echo $#
u="*"; set -- $u; echo $# >/dev/null
v=""; set -- $v; echo $#
set -- "$v"; echo $#
w=(a "" b); echo ${#w[@]}; set -- ${w[@]}; echo $#; set -- "${w[@]}"; echo $#
