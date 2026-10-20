# quoting: single, double, backslash, mixing
echo 'single $HOME `x` "dq" \n'
echo "double \$ \` \" \\ \n end"
echo a\ b\ \ c
echo "a"'b'"c"
echo ''""''x
x="hello world"
echo "$x" '$x' "${x}" "'$x'" '"$x"'
echo "nested $(echo "inner $(echo "deep")")"
echo "tab	here"
echo \"\'\\
echo "\a\b\c"
echo 'it'\''s'
echo "multi
line"
echo "\
continued"
echo foo\
bar
printf '%s\n' "a  b" a\ \ b 'a  b'
v='*'
echo "$v" '$v'
empty=""
printf '[%s]\n' "$empty" $empty "" ''
printf '[%s]\n' "x"$empty"y"
echo "${x:+"quoted alt"}"
echo "$(printf '%s' "a b")"
echo "a\$b"
echo \$HOME
echo '\'
echo "\\\\"
echo "'"
echo "\""
