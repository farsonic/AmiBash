cat <<< "simple"
cat <<< 'single $x'
x="var value"
cat <<< "$x"
cat <<< $x
read -r a b <<< "first second third"
echo "$a / $b"
wc -l <<< "one"
tr a-z A-Z <<< "upper me"
read -ra arr <<< "x y z"
echo "${#arr[@]} ${arr[1]}"
cat <<< ""
echo "---"
cat <<< "multi
line"
while read -r w; do echo "<$w>"; done <<< "$(printf 'a\nb')"
IFS=, read -r p q <<< "1,2"
echo $p $q
cat <<< $((6*7))
cat <<<"no space"
