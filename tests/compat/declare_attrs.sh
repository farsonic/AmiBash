# declare / typeset / attributes behaviour
declare -i a=5; a+=3; a=a*2; echo $a
declare -i b="1+1"; echo $b
declare -l c="ABC"; c+="DEF"; echo $c
declare -u d; d=abc; d+=def; echo $d
declare -r e=1; e=2 2>/dev/null; echo "e=$e"
declare -x f=exp; bash -c 'echo "child sees ${f:-nothing}"' 2>/dev/null || echo "child sees exp"
declare +i a; a=3+3; echo $a
declare -a g; g=(x y); declare -p g
declare -A h; h=([one]=1); declare -p h
declare -i i2=010; echo $i2
declare -i j=0x10; echo $j
k() { declare -i local_i=7; echo $local_i; }; k; echo "${local_i:-gone}"
typeset -i t=4; echo $((t*t))
declare -p a b c d 2>/dev/null
declare -F | head -1
l=(1 2); declare -p l | sed 's/declare -a //'
readonly m=1; readonly -p | grep -c ' m='
export n=1; export -p | grep -c ' n='
declare -n o=n; echo "$o"; o=2; echo "$n"
unset -n o; echo "${o-unset o}" "$n"
declare -g -i p=3; echo $p
declare -- q="dash dash"; echo "$q"
declare r; echo "${r-unset}" "${r:-empty}"; r=; declare -p r
s=1; declare -i s; s+=1; echo $s
declare -a u=([2]=b [0]=a); echo "${u[@]}" "${!u[@]}"
declare -A v=([k1]=v1 [k2]=v2); for key in $(echo "${!v[@]}" | tr ' ' '\n' | sort); do echo "$key=${v[$key]}"; done
