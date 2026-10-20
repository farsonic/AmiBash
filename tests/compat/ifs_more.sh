show() { printf '<%s>' "$@"; echo; }
IFS=' '; s="a  b"; show $s
IFS=$' \t\n'; s=$'\ta\t\tb\t'; show $s
IFS=:; s="::a::b::"; show $s
IFS=': '; s=" : a : : b : "; show $s
IFS=,; s="a,,b"; show $s; show "${s}"
IFS=; s="a b"; show $s
unset IFS; s=" a b "; show $s
IFS=,; read -r x y z <<< "1,2,3,4"; show "$x" "$y" "$z"
IFS=,; read -r x y <<< "1,"; show "$x" "$y"
IFS=' '; read -r x y <<< "  lead"; show "$x" "$y"
unset IFS
arr=(x y z); IFS=+; echo "${arr[*]}"; echo "$((${arr[*]/#*/1}))" 2>/dev/null; unset IFS
IFS=$'\n'; set -- $(printf 'l 1\nl 2\n'); unset IFS; show "$@"
f() { local IFS=:; echo "$*"; }; f a b c
IFS=x; v="axbxc"; for p in $v; do echo -n "$p."; done; echo; unset IFS
IFS=- read -r a b <<< "x-y-z"; show "$a" "$b"
oldIFS=$IFS; IFS=; arr2=($(echo "a b")); IFS=$oldIFS; echo "${#arr2[@]}"
