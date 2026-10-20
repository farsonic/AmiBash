set -u
x=1
echo "$x"
echo "${y:-default}"
echo "${y-dash}"
echo "${#}" "$#"
arr=(a)
echo "${arr[0]}"
echo "${@:-none}"
f() { echo "${1:-noarg}"; }; f
echo "${y+set}"
echo "$y"
echo "not reached"
