echo if then fi do done case esac in
echo {echo,x}
x=$( case y in y) echo "case-in-subst";; esac ); echo $x
y=$( # comment )
echo "after comment"
); echo "$y"
z=$((echo a); (echo b)); echo $z
echo "$(cat <<EOF
heredoc in dq subst
EOF
)"
w=`echo \`echo nested\``; echo $w
v="multi
line string"; echo "$v"
echo 'a
b'
f () { echo spaced-def; }; f
function g() { echo func-paren; }; g
function h
{
  echo brace-newline
}
h
for i in a b
do
  echo "loop $i"
done
if true
then
  echo then-newline
fi
while false
do :
done
case a
in
  a)
    echo case-newlines
    ;;
esac
[[ -n x ]] &&
  echo "cont and"
echo "a" \
  "b"
arr=(a
b
c); echo ${#arr[@]}
echo $(( 1 +
  2 ))
{ echo grp; } ; ( echo sub )
echo "}" "{" ")" 2>/dev/null
echo a\;b a\&b a\|b
echo 'semi;inside' "pipe|inside"
echo $((1))$((2))
echo ${#}${#x}
echo "${x:-"nested "quotes""}"
k=1; ((k++)); [[ $k -eq 2 ]] && echo k2
echo "esc \" quote" 'sq \ back'
echo `echo 'bt sq'`
: "${UNSET_CONFIG:=default}"; echo $UNSET_CONFIG
echo ${var:-a}${var:-b}
true && { echo and-group; } || { echo or-group; }
! true || echo "bang or"
echo "#not comment" a#b \#c
echo ~nouser_zz/x
case "x)" in "x)") echo "paren in pattern";; esac
case x in (x|y) echo "leading paren alt";; esac
echo "$( echo "(" )" ")"
