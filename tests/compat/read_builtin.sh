printf 'line1\nline2\nline3\n' > data
while IFS= read -r line; do echo "[$line]"; done < data
read -r first < data; echo "first=$first"
printf '  lead and trail  \n' | { read -r x; echo "[$x]"; }
printf '  lead and trail  \n' | { IFS= read -r x; echo "[$x]"; }
printf 'a\\tb\\\nc\n' | { read x; echo "[$x]"; }
printf 'a\\tb\n' | { read -r x; echo "[$x]"; }
printf 'one two three four\n' | { read a b; echo "a=$a b=$b"; }
printf 'x:y:z\n' | { IFS=: read -r p q r; echo "$p,$q,$r"; }
printf 'k=v\n' | { IFS== read -r k v; echo "$k -> $v"; }
printf 'arr1 arr2 arr3\n' | { read -ra A; echo "${#A[@]} ${A[2]}"; }
printf 'abcdef' | { read -n 3 c; echo "$c"; }
printf 'xy;zw' | { read -d ';' d; echo "$d"; }
printf 'no newline at end' | { read -r l; echo "st=$? l=$l"; }
printf '' | { read -r l; echo "empty st=$?"; }
printf 'a\nb\n' | { read; echo "REPLY=$REPLY"; }
printf 'in1\nin2\n' > two
{ read -r l1; read -r l2; } < two; echo "$l1+$l2"
exec 3< two; read -r -u 3 u1; read -r -u 3 u2; exec 3<&-; echo "$u1/$u2"
n=0; while read -r w; do n=$((n+1)); done < data; echo "count=$n"
printf 'last' > nonl; while IFS= read -r l || [ -n "$l" ]; do echo "got:$l"; done < nonl
mapfile -t lines < data; echo "${#lines[@]}: ${lines[1]}"
mapfile lines2 < data; printf '%s' "${lines2[0]}"
readarray -t -n 2 lim < data; echo "${lim[@]}"
mapfile -t -s 1 skip < data; echo "${skip[@]}"
printf 'a,b,c' | { mapfile -t -d , parts; echo "${parts[@]} ${#parts[@]}"; }
printf '1 2\n3 4\n' | while read -r a b; do echo $((a*b)); done
IFS=$'\t' read -r c1 c2 <<< $'col1\tcol 2'; echo "$c1|$c2"
read -r -a words <<< "  spaced   words here  "; echo "${#words[@]}"
read -t 1 -r tt <<< "timeout ok"; echo "$tt"
read -s -r ss <<< "silent"; echo "$ss"
read -N 4 nn <<< "abcdefg"; echo "$nn"
read -p "prompt> " pp <<< "answer" 2>/dev/null; echo "$pp"
