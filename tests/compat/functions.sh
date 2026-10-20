greet() { echo "Hello, $1"; }
greet World
function named { echo "function keyword $#"; }
named a b c
function paren() { echo paren; }
paren
f() { return 3; }; f; echo "ret $?"
g() { false; }; g; echo "implicit $?"
h() { local v=local; echo "$v"; }; v=global; h; echo "$v"
dyn() { echo "sees $dv"; }; caller() { local dv=fromcaller; dyn; }; dv=glob; caller; dyn
fact() { if (( $1 <= 1 )); then echo 1; else echo $(( $1 * $(fact $(( $1 - 1 ))) )); fi; }
fact 10
fib() { local n=$1; if (( n < 2 )); then echo $n; return; fi; local a b; a=$(fib $((n-1))); b=$(fib $((n-2))); echo $((a+b)); }
fib 12
fib2() { local -i n=$1 a=0 b=1 t; while (( n-- > 0 )); do t=$a; a=$b; b=$((t+b)); done; echo $a; }
fib2 50
args() { echo "$#: $*"; for a; do echo "<$a>"; done; }
args "x y" z ""
fn() { echo "${FUNCNAME[0]} from ${FUNCNAME[1]:-main}"; }
outer() { fn; }
outer
fn
counter=0; inc() { counter=$((counter+1)); }; inc; inc; inc; echo $counter
ret_in_loop() { for i in 1 2 3; do if [ $i = 2 ]; then return $i; fi; done; }; ret_in_loop; echo "loop ret $?"
nested() { inner() { echo "inner defined"; }; inner; }; nested; inner
loc() { local x; x=1; local y=2 z; echo "${x}${y}${z:-n}"; }; loc
shadow() { local PATH=/nonexistent; echo "$PATH"; }; shadow; [ "$PATH" != /nonexistent ] && echo "PATH restored"
recurse() { local d=$1; [ $d -eq 0 ] && return; echo -n "$d "; recurse $((d-1)); }; recurse 5; echo
unset -f greet; greet x 2>/dev/null || echo "greet unset"
pos() { echo "$1 $2"; shift; echo "$1"; }; pos a b
f2() { echo "$0" | grep -c functions; }; f2
declare -f fact >/dev/null && echo "declare -f works"
declare -F fib
compute() { local result=$(( $1 + $2 )); echo $result; }; r=$(compute 3 4); echo "r=$r"
modarr() { arr+=("$1"); }; arr=(); modarr a; modarr b; echo "${arr[@]}"
localarr() { local -a la=(1 2 3); echo "${#la[@]}"; }; localarr; echo "${#la[@]}"
rec_local() { local v=$1; if [ "$1" -lt 3 ]; then rec_local $(( $1 + 1 )); fi; echo -n "$v"; }; rec_local 0; echo
f3() { echo "status in: $?"; }; false; f3
f4() { local x=1; unset x; echo "${x-unset local}"; }; x=global; f4; echo $x
