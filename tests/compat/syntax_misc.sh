# comments # here
echo a # trailing comment
echo b#notcomment
echo 'quoted # not comment'
echo multi \
  line \
  continuation
f() {
  echo "body"
} > fout
f; cat fout
g() ( echo "subshell body"; x=changed ); x=orig; g; echo $x
{ echo brace; }
{ echo nested; { echo deeper; }; }
if true; then echo t; fi; if false; then :; else echo e; fi
while false; do :; done; until true; do :; done
for i in 1 2; do echo $i; done; for i in 3; { echo $i; }
case x in x) echo x;; esac; case y in (y) echo y;; esac
echo $(( 1 +
2 ))
arr=(
  one
  two # comment in array
  three
)
echo "${#arr[@]}"
cat <<EOF | tr a-z A-Z
heredoc piped
EOF
echo one; echo two;
echo "semi;colon" 'and|pipe' "amp&ersand"
x=1;y=2;echo $x$y
[[ 1 -eq 1 &&
   2 -eq 2 ]] && echo "multiline [["
true &&
echo "after && newline"
false ||
echo "after || newline"
echo foo |
tr a-z A-Z
function h { echo "h"; }; h
k() { :; }; k; echo "empty fn $?"
echo ${x}${y} "${x}y"
echo {a,b}$x
time_ok=1; echo done
! { false; } && echo "negated group"
echo $'\''
echo "a"b'c'$'d'
: <<'COMMENT'
this is a block comment
$(not executed)
COMMENT
echo "after block comment"
