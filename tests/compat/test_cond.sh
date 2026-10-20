t() { if eval "$1"; then echo "T: $1"; else echo "F: $1"; fi; }
t '[[ abc == abc ]]'
t '[[ abc == a* ]]'
t '[[ abc == "a*" ]]'
t '[[ a* == "a*" ]]'
t '[[ abc != b* ]]'
t '[[ abc = ?b? ]]'
t '[[ -z "" && -n x ]]'
t '[[ -z x || -n x ]]'
t '[[ ! -z x ]]'
t '[[ ( 1 -eq 1 ) && ( 2 -lt 1 || 3 -gt 2 ) ]]'
t '[[ a < b ]]'
t '[[ b > a ]]'
t '[[ 10 -gt 9 ]]'
t '[[ 10 > 9 ]]'
t '[[ 1+1 -eq 2 ]]'
x=hello
t '[[ $x == hello ]]'
t '[[ $x == h*o ]]'
t '[[ $x == "h*o" ]]'
y="a b"
t '[[ $y == "a b" ]]'
t '[[ $y == a\ b ]]'
t '[[ -n $undefined_v ]]'
t '[[ $x =~ ^h.*o$ ]]'
t '[[ $x =~ l+ ]]'
t '[[ $x =~ ^x ]]'
if [[ "2024-01-15" =~ ^([0-9]{4})-([0-9]{2})-([0-9]{2})$ ]]; then
  echo "y=${BASH_REMATCH[1]} m=${BASH_REMATCH[2]} d=${BASH_REMATCH[3]} all=${BASH_REMATCH[0]}"
  echo "${#BASH_REMATCH[@]}"
fi
re='^[a-z]+([0-9]+)$'
[[ abc123 =~ $re ]] && echo "num=${BASH_REMATCH[1]}"
[[ "a.b" =~ a.b ]] && echo "dot matches"
[[ "axb" =~ "a.b" ]] || echo "quoted dot literal"
[[ "a.b" =~ "a.b" ]] && echo "quoted literal match"
[[ "foo bar" =~ foo\ bar ]] && echo "escaped space"
[[ "x" =~ ^(a|b|x)$ ]] && echo "alternation ${BASH_REMATCH[1]}"
[[ nomatch =~ ^z ]]; echo "${#BASH_REMATCH[@]} after fail"
touch f1
t '[[ -f f1 ]]'
t '[[ -d f1 ]]'
t '[[ -e f1 && ! -d f1 ]]'
v=set
t '[[ -v v ]]'
t '[[ -v nope ]]'
arr=(1 2)
t '[[ -v arr[1] ]]'
t '[[ -v arr[5] ]]'
t '[[ -o noglob ]]'
t '[[ 08 -eq 8 ]] 2>/dev/null'
t '[[ "" ]]'
t '[[ x ]]'
t '[[ $x ]]'
e=
t '[[ $e ]]'
t '[[ abc == @(abc|def) ]]'
t '[[ -n "$x" && $x == h* && ${#x} -eq 5 ]]'
[[ a == a ]]; echo "status $?"
[[ a == b ]]; echo "status $?"
t '[[ "$x" == "${x}" ]]'
t '[[ abc < abd ]]'
p='a*'
t '[[ abc == $p ]]'
t '[[ abc == "$p" ]]'
t '[[ "[x]" == "[x]" ]]'
t '[[ 5 -eq 5 ]] && [[ 6 -ne 5 ]]'
