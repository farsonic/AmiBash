s=$SECONDS
i=0; sum=0
while (( i < 500 )); do sum=$((sum + i*i)); ((i++)); done
echo "arith loop 500: sum=$sum $((SECONDS-s))s"
s=$SECONDS
for n in {1..200}; do x="item$n"; y=${x/item/I}; done
echo "string ops 200: $y $((SECONDS-s))s"
s=$SECONDS
seq 1 300 | sort -rn | head -3 | tr '\n' ' '; echo
echo "pipeline: $((SECONDS-s))s"
s=$SECONDS
f() { (( $1 <= 1 )) && { echo 1; return; }; echo $(( $1 * $(f $(($1-1))) )); }
echo "fact 10 = $(f 10) $((SECONDS-s))s"
