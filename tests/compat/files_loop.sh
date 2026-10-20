cat > input.txt <<'EOF'
alpha 3
beta 1
gamma 2

delta 5
# comment line
epsilon 4
EOF
while IFS= read -r line; do
    [[ -z $line || $line == \#* ]] && continue
    echo "line: $line"
done < input.txt
total=0
while read -r name val; do
    [[ $name == \#* || -z $name ]] && continue
    total=$((total + val))
done < input.txt
echo "total=$total"
sort -k2 -n input.txt | grep -v '^#' | grep -v '^$' | head -3
grep -c . input.txt
grep -n gamma input.txt
awk_free=$(grep -v '^#' input.txt | grep . | wc -l); echo "records: $awk_free"
lines=(); while IFS= read -r l; do lines+=("$l"); done < input.txt; echo "${#lines[@]} lines"
mapfile -t arr < input.txt; echo "mapfile ${#arr[@]}"
n=0; while read -r; do ((n++)); done < input.txt; echo "n=$n"
exec 3< input.txt; read -r first <&3; read -r second <&3; exec 3<&-; echo "$first / $second"
for f in a b c; do echo "content $f" > "file_$f.txt"; done
for f in file_*.txt; do echo "$f: $(cat "$f")"; done
ls file_*.txt | wc -l
cat file_a.txt file_b.txt > combined.txt; wc -l < combined.txt
rm file_b.txt; ls file_*.txt
[ -f file_b.txt ] || echo "removed"
mkdir -p sub/dir; touch sub/dir/x; [ -d sub/dir ] && echo "nested dir"
cp input.txt copy.txt && cmp -s input.txt copy.txt && echo "copy same"
mv copy.txt moved.txt; [ -e moved.txt ] && [ ! -e copy.txt ] && echo "moved"
head -2 input.txt; tail -1 input.txt
tr -d '0-9' < input.txt | head -1
cut -d' ' -f1 input.txt | paste -sd, -
sed -n '2p' input.txt
sed -e 's/a/A/g' -e '/^#/d' input.txt | head -2
uniq_count=$(cut -d' ' -f2 input.txt | sort -u | wc -l); echo "uniq $uniq_count"
