# $'...' ANSI-C quoting
echo $'a\tb'
echo $'line1\nline2'
echo $'quote\'s'
echo $'back\\slash'
echo $'\x41\x42\x43'
echo $'\101\102'
echo $'é' | od -An -c | tr -s ' '
echo $'esc\e[0m' | od -An -c | tr -s ' '
echo $'a\0b' | od -An -c | tr -s ' '
x=$'one\ntwo'
echo "$x"
printf '%s|' $x; echo
echo "$'not ansi'"
echo $'\cA' | od -An -c | tr -s ' '
echo $'\"dq\"' $'\?'
echo $'mix'"ed"'and'
y=$'tab\there'
echo "${#y}"
echo $'\x4a\x4B'
