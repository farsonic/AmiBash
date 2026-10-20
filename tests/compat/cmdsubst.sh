a=$(echo hello)
echo "$a"
b=`echo backtick`
echo "$b"
c=$(echo "$(echo nested)")
echo "$c"
d=`echo \`echo inner\``
echo "$d"
e=$(printf 'trail\n\n\n')
echo "[$e]"
f=$(printf '\n\nlead')
echo "[$f]"
g="$(echo a; echo b)"
echo "$g"
h=$(echo "  spaced   out  ")
echo [$h] "[$h]"
i=$(exit 3); echo "status $?"
j=$(false) || echo "false captured"
k=$(echo $((2+3)))
echo $k
l=$(for x in 1 2 3; do echo -n $x; done)
echo $l
m=$(case a in a) echo A;; esac)
echo $m
n=$(echo ')' )
echo "$n"
o=$( (echo sub) )
echo $o
p=$(cat <<EOF
heredoc in subst
EOF
)
echo "$p"
q=$(echo "a'b")
echo "$q"
r="$(echo "quoted \"inner\"")"
echo "$r"
s=$(x=5; echo $x)
echo "$s ${x:-unset}"
echo "$(echo one)$(echo two)"
echo $(echo a b) $(echo c)
for w in $(echo w1 w2 w3); do echo "w=$w"; done
t=$(echo $'tab\there')
echo "$t"
u=$(echo hi >&2) 2>/dev/null
echo "[$u]"
v=$(echo out; echo err >&2) 2>&1
echo "[$v]"
echo "${#a}" $(( $(echo 4) * 2 ))
y=$(echo "#notcomment")
echo "$y"
z=$(echo a # comment here
)
echo "$z"
echo "$(echo "$(echo "$(echo "$(echo deep)")")")"
lines=$(printf 'l1\nl2\nl3')
echo "$lines" | wc -l
count=0; while read -r l; do count=$((count+1)); done <<< "$(printf 'x\ny\nz')"; echo $count
