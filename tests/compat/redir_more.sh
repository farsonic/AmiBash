exec 3>&1
out=$( { echo "to stdout"; echo "to fd3" >&3; } 3>&1 1>/dev/null )
echo "captured: [$out]"
exec 3>&-
{ echo "err line" >&2; } 2>&1 | tr a-z A-Z
f() { echo "fout"; echo "ferr" >&2; }
f 2>&1 >/dev/null | sed 's/^/piped: /'
exec 4>log.txt
echo one >&4
echo two 1>&4
exec 4>&-
cat log.txt
cat <<EOF >&2 2>/dev/null
heredoc to stderr
EOF
cat <<EOF 3>&1 >&3
heredoc via fd3
EOF
x=$(cat < log.txt); echo "$x" | wc -l
while read -r a; do echo "a=$a"; done < <(echo procsub)
read -r line < log.txt; echo "first: $line"
{ read -r l1; read -r l2; echo "$l2 $l1"; } < log.txt
echo appended >> log.txt; tail -1 log.txt
: > log.txt; wc -c < log.txt
printf 'abc' > nonl.txt; cat nonl.txt; echo
echo "x" > "file with spaces.txt"; cat "file with spaces.txt"
name=dyn.txt; echo dynamic > "$name"; cat $name
echo "err" 2>&1 1>/dev/null
ls nonexistent_zz 2>&1 | wc -l
{ echo a; echo b >&2; } > both.txt 2>&1; sort both.txt | tr '\n' ' '; echo
exec 5<&0; exec 5<&-
echo "fd juggle" 3>&1 4>&3 1>&4
