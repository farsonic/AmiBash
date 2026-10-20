printf 'a b\0c d\0' > nul.bin
while IFS= read -r -d '' rec; do echo "rec=[$rec]"; done < nul.bin
printf 'whole\nfile\ncontent\n' > whole.txt
IFS= read -r -d '' all < whole.txt; printf '%s' "$all" | wc -l
read -r a b c <<< "only"; echo "[$a][$b][$c]"
read -r a b <<< "x y z w"; echo "[$a][$b]"
read -r -a arr <<< ""; echo "${#arr[@]}"
echo "a\\b" | { read v; echo "$v"; }
echo "a\\b" | { read -r v; echo "$v"; }
printf 'line\\\ncont\n' | { read v; echo "$v"; }
printf 'k1=v1\nk2=v2\n' | while IFS='=' read -r k v; do echo "$k:$v"; done
read -n 1 ch <<< "xyz"; echo "$ch"
read -d , f1 <<< "aa,bb"; echo "$f1"
{ read -r h; while read -r r; do echo "row $r under $h"; done; } <<'EOF'
HEADER
r1
r2
EOF
echo "x y" | { read -r; echo "[$REPLY]"; }
echo "  x y  " | { read -r; echo "[$REPLY]"; }
echo "  x y  " | { read -r v; echo "[$v]"; }
read v1 v2 <<< $'tab\tsep'; echo "$v1|$v2"
r=$(printf 'no-nl' | { read -r l; echo "rc=$? [$l]"; }); echo "$r"
mapfile -t m <<< $'a\nb\nc'; echo "${#m[@]} ${m[2]}"
mapfile -t -O 5 off <<< $'x\ny'; echo "${!off[@]}"
printf '1\n2\n3\n' | mapfile -t inpipe; echo "${#inpipe[@]}"
