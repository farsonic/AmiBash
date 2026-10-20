# nested quoting in substitutions
a="x y"
echo "$(echo "$a")"
echo "$(echo '$a')"
echo "`echo \"$a\"`"
echo $(echo "a   b")
echo "$(echo "a   b")"
b=$(echo "it's")
echo "$b"
c="${a/x/'q r'}"
echo "$c"
d="${a:-"def val"}"
echo "$d"
unset u
echo "${u:-"a b"}" ${u:-"a   b"} ${u:-a   b}
echo "${u:-'single'}"
echo ${u:-'single  s'}
e='$(echo nope)'
echo "$e"
eval "echo $e"
f="a\"b"
echo "$f"
echo "${f//\"/Q}"
g='a\b'
echo "$g" "${g//\\/X}"
echo "$(echo "$(echo "$(echo nest3)")")"
arr=("a b" "c")
echo "${arr[0]}" "${arr[@]/a/"A A"}"
echo "pre${a}post" "pre$a"post
echo "${a}"'${a}'"${a}"
