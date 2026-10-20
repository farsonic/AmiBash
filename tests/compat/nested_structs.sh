for i in 1 2 3; do
    case $i in
        1) while read -r w; do
               if [[ $w == b* ]]; then echo "case1 $w"; fi
           done <<< $'apple\nbanana\nberry' ;;
        2) for j in x y; do
               (( i * 2 == 4 )) && { echo "two $j"; continue; }
               echo never
           done ;;
        *) until (( i > 5 )); do (( i++ )); done; echo "i=$i" ;;
    esac
done
f() {
    local -a stack=()
    local tok
    for tok in "$@"; do
        case $tok in
            '(') stack+=("(") ;;
            ')') if (( ${#stack[@]} == 0 )); then echo unbalanced; return 1; fi
                 unset 'stack[${#stack[@]}-1]' ;;
        esac
    done
    (( ${#stack[@]} == 0 )) && echo balanced || echo unbalanced
}
f '(' '(' ')' ')'
f '(' ')' ')'
f '(' 
out=$(
    for x in a b; do
        echo "$x$(for y in 1 2; do printf '%s' "$y"; done)"
    done
)
echo "$out"
if true; then if false; then echo no; elif true; then if [[ 1 ]]; then echo deep-if; fi; fi; fi
{ { { echo triple; }; }; }
( ( ( echo sub3 ) ) )
x=$( (echo inner) ); echo "$x"
echo "$( case a in a) echo "case in subst";; esac )"
g() { h() { echo "h from g"; }; h; }; g
while true; do while true; do break 2; done; done; echo "broke out"
for i in 1 2; do for j in 1 2; do for k in 1 2; do [[ $k == 2 ]] && continue 3; echo "$i$j$k"; done; done; done
