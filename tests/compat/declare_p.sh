a=simple; declare -p a
declare -i n=42; declare -p n
declare -r ro=const; declare -p ro
declare -x ex=exported; declare -p ex
arr=(one "two words" three); declare -p arr
declare -A assoc=([k1]=v1); declare -p assoc
declare -a empty=(); declare -p empty
declare -l low=MiXeD; declare -p low; echo $low
declare -u up=MiXeD; declare -p up; echo $up
q='has "quotes" and $dollar'; declare -p q
nl=$'a\nb'; declare -p nl
declare unset_decl; declare -p unset_decl
declare -ai ia=(1 2); declare -p ia
sparse=([1]=a [5]=b); declare -p sparse
declare -p nonexistent_var 2>&1 | sed 's/^.*declare/declare/'
f() { local loc=inner; declare -p loc; }; f
declare -n ref=a; declare -p ref
export E2=v; declare -p E2
declare +x E2; declare -p E2
declare -i calc; calc="2*21"; echo $calc
typeset t=typeset; declare -p t
declare -g G=global; declare -p G
readonly R2=x; declare -p R2
bs='back\slash'; declare -p bs
