text="the cat and the dog and the bird the end"
declare -A freq
for w in $text; do
    freq[$w]=$(( ${freq[$w]:-0} + 1 ))
done
for w in "${!freq[@]}"; do
    printf '%s %d\n' "$w" "${freq[$w]}"
done | sort -k2,2nr -k1,1
echo "---"
printf '%s\n' $text | sort | uniq -c | sort -k1,1nr -k2,2 | head -3 | sed 's/^ *//'
max=0; maxw=
for w in "${!freq[@]}"; do
    if (( freq[$w] > max )) || { (( freq[$w] == max )) && [[ $w < $maxw ]]; }; then max=${freq[$w]}; maxw=$w; fi
done
echo "most: $maxw ($max)"
echo "unique: ${#freq[@]}"
