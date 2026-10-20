a=(1 2 3); (( a[1]++ )); echo "${a[1]}"
(( a[2] *= 10 )); echo "${a[2]}"
i=1; echo $(( a[i] + a[i+1] ))
x=$(( y = 3 )); echo "$x $y"
echo $(( 2 > 1 ? 10 : 20 )) $(( 0 ? 1 : 0 ? 2 : 3 ))
echo $(( -2 ** 2 )) $(( (-2) ** 2 )) $(( 2 ** 3 ** 2 ))
echo $(( 7 / -2 )) $(( -7 % 2 )) $(( 7 % -2 ))
echo $(( 1 << 63 )) $(( (1 << 63) - 1 ))
echo $(( ~0 )) $(( !1 )) $(( - - 3 ))
v=10; echo $(( v += 5, v * 2 ))
echo $(( 0x7fffffffffffffff )) $(( -0x10 ))
echo $(( 2#11111111 )) $(( 8#777 )) $(( 36#Z )) $(( 64#@ ))
n=5; while (( n-- )); do echo -n "$n "; done; echo
for (( i = 0, j = 10; i < j; i += 3, j -= 3 )); do echo -n "($i,$j)"; done; echo
(( x = 5, y = x * 2, z = y - 1 )); echo $x $y $z
echo $(( 10 > 5 && 5 > 1 )) $(( 1 == 1 || 1 / 0 ))
echo $(( 0 && 1 / 0 ))
str=abc; echo $(( str )) 2>/dev/null || echo "str unset->0"
abc=4; echo $(( str + 1 ))
echo $(( 1 + 2 )) $((1+2)) $(( 1+2 ))
let "q = 2 ** 10" "r = q / 3"; echo $q $r
declare -i di=5; di='di * 3'; echo $di
echo $(( (1 + 2) * (3 + 4) / 7 ))
echo $(( 100 / 7 * 7 + 100 % 7 ))
c=0; (( c++ )); echo "status after c++ from 0: $?"
c=0; (( ++c )); echo "status after ++c: $?"
echo $(( 5 & 3 ^ 1 | 8 ))
echo $(( 1 < 2 == 1 ))
echo $(( 017 + 0x1A + 2#101 ))
e=3; echo $(( e ** 2 ** 1 ))
echo $(( 9223372036854775807 * 2 ))
m=-5; echo $(( m < 0 ? -m : m ))
echo $(( 3, 4, 5 ))
printf '%d\n' $(( 255 & ~15 ))
