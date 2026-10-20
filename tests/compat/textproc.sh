cat > data.csv <<'EOF'
name,dept,salary
alice,eng,120
bob,sales,80
carol,eng,110
dave,ops,95
erin,sales,85
EOF
tail -n +2 data.csv | sort -t, -k3 -n | head -2
tail -n +2 data.csv | cut -d, -f2 | sort | uniq -c | sed 's/^ *//'
grep eng data.csv | wc -l
grep -v '^name' data.csv | sort -t, -k1,1r | head -1
sed -n '/bob/,/dave/p' data.csv
sed 's/,/ | /g' data.csv | head -2
grep -E '^(a|e)' data.csv
grep -i ALICE data.csv
grep -c sales data.csv
declare -A total
while IFS=, read -r name dept sal; do
    [[ $name == name ]] && continue
    total[$dept]=$(( ${total[$dept]:-0} + sal ))
done < data.csv
for d in $(printf '%s\n' "${!total[@]}" | sort); do echo "$d=${total[$d]}"; done
max=$(tail -n +2 data.csv | cut -d, -f3 | sort -n | tail -1); echo "max=$max"
tr ',' '\t' < data.csv | head -1 | od -c | head -1 | tr -s ' '
sort -t, -k2,2 -k3,3n data.csv | head -3
echo "b a c" | tr ' ' '\n' | sort | tr '\n' ' '; echo
printf '%s\n' 10 9 100 1 | sort -n | tr '\n' ' '; echo
printf '%s\n' 10 9 100 1 | sort | tr '\n' ' '; echo
printf '%s\n' b B a A | sort | tr '\n' ' '; echo
printf 'x\ny\nx\n' | sort -u | tr '\n' ' '; echo
printf 'line1\nline2\n' | sed '1d'
echo "hello world" | sed -E 's/(hello) (world)/\2 \1/'
echo "aaa" | sed 's/a/b/2'
echo "path/to/file" | sed 's#/#\\#g'
seq 3 | sed 'N;s/\n/+/'
echo "x" | grep -q x && echo "grep -q ok"
echo "abc" | grep -o b
seq 10 | grep -E '^[0-9]$' | tr '\n' ' '; echo
seq 5 | head -n -2 2>/dev/null | tr '\n' ' '; echo
wc -w <<< "one two three"
wc -c <<< "abc"
echo "a:b:c" | tr ':' '\n' | tail -1
rev <<< "abc" 2>/dev/null || echo cba
printf '%s\n' c a b | sort | paste -sd' ' -
