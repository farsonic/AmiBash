type echo
type cd
f() { :; }
type -t f
type -t echo
type -t if
type -t nonexistent_cmd_zz; echo "status $?"
alias ll='ls -l'
type -t ll 2>/dev/null || echo "alias not expanded in script without shopt"
shopt -s expand_aliases
alias ll='ls -l'
type ll
type -t ll
type if
type nonexistent_cmd_zz 2>&1 | sed 's/^.*: line [0-9]*: //'
command -v echo
command -v f
command -v nonexistent_cmd_zz; echo "status $?"
command -v if
command echo "via command"
command nonexistent_cmd_zz 2>/dev/null; echo "status $?"
g() { echo "function g"; }
command -v g
type -t [
type -t [[
builtin echo "via builtin"
builtin nonexistent 2>/dev/null; echo "builtin status $?"
type -a true | head -1
hash -r; echo "hash ok"
echo() { builtin echo "wrapped: $*"; }
echo hi
command echo bypass
unset -f echo
echo restored
type -p nonexistent_zz; echo "p status $?"
