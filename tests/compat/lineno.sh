echo "line $LINENO"
f() {
  echo "in f $LINENO"
  nosuch_in_f
}
f
for i in 1; do
  echo "loop $LINENO"
done
if true; then
  nosuch_if
fi
x=$(
  echo "subst $LINENO"
)
echo "$x"
cat <<EOF
heredoc $LINENO
EOF
echo "after heredoc $LINENO"
echo "multi
line $LINENO"
case a in
  a) echo "case $LINENO";;
esac
{ echo "group $LINENO"
  nosuch_grp; }
while false; do :; done; echo "while $LINENO"
arr=(
1
2
); echo "arr $LINENO"
echo \
  "continued $LINENO"
trap 'echo "trap line $LINENO"' ERR
false
g() { false; }
g
eval 'echo "eval $LINENO"
echo "eval2 $LINENO"'
echo "end $LINENO"
