v=global
a() { local v=a; b; echo "a sees $v"; }
b() { echo "b sees $v"; v=modified_by_b; }
a; echo "global is $v"
c() { local v; echo "c local unset: [${v-unset}]"; v=c; d; }
d() { echo "d sees $v"; }
c
e() { local x=1; f_inner() { echo "inner x=$x"; }; f_inner; }
e
counter() { local -i n=0; n+=1; n+=1; echo $n; }; counter; counter
g() { local arr=(1 2 3); arr[1]=X; echo "${arr[@]}"; }; g; echo "${arr[@]:-noarr}"
h() { local -A m=([k]=v); echo "${m[k]}"; }; h
i() { declare v=declared_local; echo $v; }; i; echo $v
j() { declare -g G2=glob; }; j; echo $G2
k() { local v=1; k2; echo "k after k2: $v"; }; k2() { local v=2; unset v; echo "k2 unset: ${v-unset}"; }; k
l() { local IFS=,; set -- a b c; echo "$*"; }; l; set -- x y; echo "$*"
m() { local -; set -e; echo "set -e local"; }; m; false; echo "errexit restored $?"
n() { export NV=exported_in_fn; }; n; echo "$NV"
o() { local o_var=inner; p; }; p() { echo "${o_var:-unset}"; }; o; p
q() { local x=$1; [ "$x" -gt 0 ] && q $((x-1)); echo -n "$x "; }; q 3; echo
