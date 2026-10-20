fizzbuzz() { local i; for ((i=1; i<=15; i++)); do
    if ((i % 15 == 0)); then echo -n "FizzBuzz "; elif ((i % 3 == 0)); then echo -n "Fizz "
    elif ((i % 5 == 0)); then echo -n "Buzz "; else echo -n "$i "; fi; done; echo; }
fizzbuzz
primes() { local n=$1 i j; local -a sieve; for ((i=2; i<=n; i++)); do sieve[i]=1; done
    for ((i=2; i*i<=n; i++)); do ((sieve[i])) || continue; for ((j=i*i; j<=n; j+=i)); do sieve[j]=0; done; done
    for ((i=2; i<=n; i++)); do ((sieve[i])) && echo -n "$i "; done; echo; }
primes 50
bubble() { local -a a=("$@"); local n=${#a[@]} i j t swapped
    for ((i=0; i<n-1; i++)); do swapped=0
        for ((j=0; j<n-i-1; j++)); do if ((a[j] > a[j+1])); then t=${a[j]}; a[j]=${a[j+1]}; a[j+1]=$t; swapped=1; fi; done
        ((swapped)) || break; done; echo "${a[*]}"; }
bubble 5 3 8 1 9 2 7
hanoi() { local n=$1 from=$2 to=$3 via=$4; ((n == 0)) && return; hanoi $((n-1)) $from $via $to; moves=$((moves+1)); hanoi $((n-1)) $via $to $from; }
moves=0; hanoi 6 A C B; echo "hanoi moves: $moves"
to_bin() { local n=$1 b=; ((n == 0)) && { echo 0; return; }; while ((n > 0)); do b=$((n % 2))$b; n=$((n / 2)); done; echo $b; }
for v in 0 5 255 1024; do echo "$v -> $(to_bin $v) -> $((2#$(to_bin $v)))"; done
caesar() { local s=$1 k=$2 out= c i o; for ((i=0; i<${#s}; i++)); do c=${s:i:1}
    case $c in [a-z]) printf -v o '%d' "'$c"; o=$(( (o - 97 + k) % 26 + 97 )); printf -v c "\\$(printf '%03o' $o)";;
               [A-Z]) printf -v o '%d' "'$c"; o=$(( (o - 65 + k) % 26 + 65 )); printf -v c "\\$(printf '%03o' $o)";; esac
    out+=$c; done; echo "$out"; }
caesar "Hello, World" 3
caesar "$(caesar "Round Trip" 13)" 13
echo "Hello" | tr 'A-Za-z' 'N-ZA-Mn-za-m'
is_pal() { local s=${1,,}; s=${s//[^a-z]/}; local r= i; for ((i=${#s}-1; i>=0; i--)); do r+=${s:i:1}; done; [[ $s == "$r" ]]; }
for w in "Racecar" "A man, a plan, a canal: Panama" "hello"; do is_pal "$w" && echo "pal: $w" || echo "not: $w"; done
gcd() { local a=$1 b=$2 t; while ((b)); do t=$b; b=$((a % b)); a=$t; done; echo $a; }
echo "gcd $(gcd 48 18) $(gcd 17 5) lcm $(( 48 * 18 / $(gcd 48 18) ))"
collatz() { local n=$1 steps=0; while ((n != 1)); do ((n % 2)) && n=$((3*n+1)) || n=$((n/2)); ((steps++)); done; echo $steps; }
echo "collatz 27: $(collatz 27)"
matmul() { local -a A=(1 2 3 4) B=(5 6 7 8) C; local i j k
    for ((i=0;i<2;i++)); do for ((j=0;j<2;j++)); do C[i*2+j]=0; for ((k=0;k<2;k++)); do ((C[i*2+j] += A[i*2+k]*B[k*2+j])); done; done; done; echo "${C[*]}"; }
matmul
bsearch() { local x=$1; shift; local -a a=("$@"); local lo=0 hi=$((${#a[@]}-1)) mid
    while ((lo <= hi)); do mid=$(( (lo+hi)/2 )); if ((a[mid] == x)); then echo $mid; return 0; elif ((a[mid] < x)); then lo=$((mid+1)); else hi=$((mid-1)); fi; done; echo -1; return 1; }
bsearch 7 1 3 5 7 9 11; bsearch 4 1 3 5 7
qsort() { (($# <= 1)) && { echo "$@"; return; }; local p=$1 x; shift; local -a l=() g=()
    for x; do ((x < p)) && l+=($x) || g+=($x); done; echo $(qsort "${l[@]}") $p $(qsort "${g[@]}"); }
qsort 9 4 7 1 8 2 6
stack=(); push() { stack+=("$1"); }; pop() { local top=${stack[-1]}; unset 'stack[-1]'; echo "$top"; }
push a; push b; push c; pop; pop; echo "left ${stack[*]}"
queue=(); enq() { queue+=("$1"); }; deq() { echo "${queue[0]}"; queue=("${queue[@]:1}"); }
enq 1; enq 2; enq 3; deq; deq; echo "q: ${queue[*]}"
fact_big() { local r=1 i; for ((i=2; i<=20; i++)); do ((r *= i)); done; echo $r; }; fact_big
wc_words() { local -i c=0; local w; for w in $1; do ((c++)); done; echo $c; }; wc_words "the quick  brown   fox"
