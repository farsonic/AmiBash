# word splitting with IFS
show() { printf '<%s>' "$@"; echo; }
x="  a  b   c  "
show $x
show "$x"
IFS=:
y="a:b::c:"
show $y
show "$y"
IFS=': '
z=" a : b:c  :: d "
show $z
IFS=,
set -- "1,2" "3"
show $*
show "$*"
show $@
show "$@"
IFS=
show $x
show $*
show "$*"
unset IFS
show $x
IFS=$'\n'
lines=$'one two\nthree four\n\nfive'
show $lines
unset IFS
w="a*b"
show $w
read -r a b c <<< "  first second third fourth "
show "$a" "$b" "$c"
IFS=: read -r a b c <<< "x:y:z:w"
show "$a" "$b" "$c"
IFS=, read -ra arr <<< "p,q,,r"
show "${arr[@]}"
echo "${#arr[@]}"
oldifs=$IFS
IFS=-; v="a-b-c"; set -- $v; echo $#; IFS=$oldifs
e=""
show $e
show $e $e x
show "" $e ""
n=$'a\tb\nc d'
show $n
IFS=x
v=axbxxc
show $v
show ${v}
for i in $v; do echo "[$i]"; done
unset IFS
s="one two"
for i in $s; do echo "[$i]"; done
for i in "$s"; do echo "[$i]"; done
