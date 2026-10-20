eval echo hello
eval 'x=5; echo $x'
cmd='echo "a  b"'; eval "$cmd"
eval "y=\$x"; echo $y
v=name; eval "$v=value"; echo $name
eval 'for i in 1 2; do echo "e$i"; done'
eval "echo \$(( 3 * 4 ))"
eval 'f() { echo "defined in eval"; }'; f
eval false; echo "eval status $?"
eval; echo "empty eval $?"
eval 'exit_code() { return 7; }'; exit_code; echo $?
for n in a b; do eval "var_$n=$n$n"; done; echo $var_a $var_b
eval "arr=(1 2 3)"; echo ${#arr[@]}
eval 'echo "nested $(eval echo inner)"'
cat > lib.sh <<'EOF'
LIBVAR=fromlib
libfunc() { echo "libfunc $1"; }
echo "sourcing with $# args: $*"
return 3
echo "not reached"
EOF
source ./lib.sh; echo "source status $?"
echo $LIBVAR
libfunc x
. ./lib.sh one two; echo "dot status $?"
set -- outer1 outer2
. ./lib.sh
echo "after source: $*"
cat > lib2.sh <<'EOF'
set -- changed
EOF
. ./lib2.sh; echo "params now: $*"
source ./nonexistent.sh 2>/dev/null; echo "missing status $?"
cat > lib3.sh <<'EOF'
local_in_src() { local z=1; echo "z=$z"; }
EOF
. ./lib3.sh; local_in_src
eval 'echo $LINENO' | grep -q '[0-9]' && echo "lineno ok"
s='echo one; echo two'
eval "$s"
q="it's"; eval "echo \"$q\""
