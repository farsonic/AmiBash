echo() { builtin echo "[wrapped] $*"; }
echo test
unset -f echo
echo normal
cd() { builtin cd "$@" && echo "cd to ${PWD##*/}"; }
mkdir -p zdir; cd zdir; cd ..
unset -f cd
f() { echo "$#"; }; f "" ""; f
g() { return 300; }; g; echo "ret $?"
h() { exit 3; }; (h); echo "exit in sub $?"
deep() { (( $1 > 0 )) && deep $(( $1 - 1 )) || echo "bottom"; }; deep 50
acc=""; add() { acc+="$1"; }; for c in a b c; do add $c; done; echo $acc
fwd() { inner "$@"; }; inner() { printf '<%s>' "$@"; echo; }; fwd "a b" c
v=outer; setv() { v=$1; }; setv changed; echo $v
lv() { local v=$1; echo "local $v"; }; lv inner; echo "global $v"
ff() { echo "${FUNCNAME[*]}"; }; gg() { ff; }; gg
re() { local n=$1; [[ $n -le 1 ]] && { echo 1; return; }; echo $(( n * $(re $((n-1))) )); }; re 6
fn_with_redir() { echo "redirected"; } > fn.out; fn_with_redir; cat fn.out
pipe_fn() { tr a-z A-Z; }; echo "pipe into fn" | pipe_fn
retval() { local r; r=$(echo computed); echo "$r"; }; x=$(retval); echo "$x"
declare -f retval | head -1
type retval | head -1
cnt() { echo $#; }; arr=(1 "2 3" 4); cnt "${arr[@]}"; cnt ${arr[@]}; cnt "${arr[*]}"
outer() { local x=1; inner2() { echo "x=$x"; }; inner2; }; outer; x=global; inner2
f1() { f2; }; f2() { echo "f2 called by ${FUNCNAME[1]}"; }; f1
lvl() { echo "${#FUNCNAME[@]}"; }; lvl; w() { lvl; }; w
