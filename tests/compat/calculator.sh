calc() {
    local a=$1 op=$2 b=$3
    case $op in
        +) echo $((a + b)) ;;
        -) echo $((a - b)) ;;
        x|'*') echo $((a * b)) ;;
        /) if (( b == 0 )); then echo "error: division by zero" >&2; return 1; fi
           echo $((a / b)) ;;
        %) echo $((a % b)) ;;
        ^) echo $((a ** b)) ;;
        *) echo "unknown op: $op" >&2; return 2 ;;
    esac
}
while read -r a op b; do
    if result=$(calc "$a" "$op" "$b" 2>&1); then
        echo "$a $op $b = $result"
    else
        echo "$a $op $b failed($?): $result"
    fi
done <<'EOF'
1 + 2
10 - 15
6 x 7
20 / 3
20 / 0
17 % 5
2 ^ 16
3 ? 4
EOF
rpn() {
    local -a st=()
    local t
    for t in "$@"; do
        case $t in
            [0-9]*) st+=("$t") ;;
            *) local b=${st[-1]}; unset 'st[-1]'; local a=${st[-1]}; unset 'st[-1]'
               st+=("$(calc "$a" "$t" "$b")") ;;
        esac
    done
    echo "${st[0]}"
}
rpn 3 4 + 2 x
rpn 5 1 2 + 4 x + 3 -
avg() { local sum=0 n=0 x; for x; do ((sum+=x, n++)); done; echo "$((sum / n)).$(( (sum * 10 / n) % 10 ))"; }
avg 1 2 3 4
avg 10 15
