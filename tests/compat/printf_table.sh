printf '%-10s|%6s|%8s\n' Name Qty Price
printf '%-10s|%6s|%8s\n' ---------- ------ --------
while IFS=: read -r name qty price; do
    printf '%-10s|%6d|%8.2f\n' "$name" "$qty" "$price"
done <<'EOF'
apple:3:1.5
banana:12:0.25
cherry:100:12.125
EOF
total=0; for p in 150 300 1212; do total=$((total + p)); done
printf 'total: %d.%02d\n' $((total / 100)) $((total % 100))
printf '%s\n' "${PIPESTATUS[@]}" >/dev/null
printf '%3d%%\n' 5 50 100
printf '%-5s%s\n' a b c d e
printf '[%*s]\n' 6 right
printf '[%-*s]\n' 6 left
printf '%05.1f|%+5d|% d\n' 3.14159 42 7
printf '%#o %#x %#X\n' 8 255 255
printf '%e\n' 0
printf '%.3s\n' "truncate"
printf '%s=%q\n' key "value with space"
for i in 1 2 3; do printf '%d^2=%-3d' $i $((i*i)); done; echo
printf -v line '%*s' 20 ''; echo "${line// /=}"
printf '%s\n' 1 2 3 | paste -sd+ - | bc 2>/dev/null || echo 6
