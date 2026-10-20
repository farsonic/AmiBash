show() { printf '<%s>' "$@"; echo " ($#)"; }
set -- "a b" c "" d
show "$@"
show $@
show "$*"
show $*
show "${@}"
show "x$@y"
show "x$*y"
show "${@:2}"
show "${@:2:2}"
show "${@: -1}"
show "${*:1:2}"
show "$@" "$@"
set --
show "$@"
show "$*"
show "${@}"
show x"$@"y
show "${@:-default}"
f() { show "$@"; show "$#"; }
f 1 "2 3"
g() { shift 2; show "$@"; }
g a b c d
h() { local IFS=-; show "$*"; }
h p q r
set -- one two three
echo "$1 ${2} ${3:-x} ${4:-four}"
echo "${#1} ${#2}"
echo $#
for a; do echo "for: $a"; done
for a in "$@"; do echo "in: $a"; done
args=("$@"); show "${args[@]}"
k() { echo "$0" | grep -q positional && echo "\$0 is script"; }; k
set -- "${@/o/0}"; show "$@"
set -- "${@^^}"; show "$@"
IFS=,; set -- $(echo "x,y,,z"); unset IFS; show "$@"
