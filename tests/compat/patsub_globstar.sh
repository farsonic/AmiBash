x=abc; echo "${x/b/[&]}" "${x//?/<&>}" "${x/b/\&}"
shopt -s globstar; mkdir -p a/b/c; touch a/1.txt a/b/2.txt a/b/c/3.txt; echo **/*.txt; echo a/**
y='a*'; [[ abc == $y ]] && echo m1; [[ abc == "$y" ]] || echo m2
printf '%.2f %.0f %.1f\n' 2.675 2.5 0.05
printf '%5.2s|\n' abc
printf '%-5c|\n' x
printf '%s\n' $'a\x00b'
echo $((2**62*2))
v=" a b "; echo "[${v// /}]"
a=(1 2 3); echo "${a[@]/#/x}" "${a[*]/%/y}"
declare -A h=([a]=1); h[b]+=2; echo "${h[b]}"
echo ${#a[*]} ${#a}
s=$'line1\nline2'; echo "${s%%$'\n'*}"
echo "$(echo -e 'a\tb')"
unset a; a[2]=x; echo "${a[@]}" ${#a[@]}
echo "${@:-none}"
f() { echo $FUNCNAME; }; f
echo ${!BASH_V*} | tr ' ' '\n' | head -2
z=5; echo ${z:+$((z*2))}
r=$(printf '%s' "a b" | wc -w); echo $r
i=0; ((i+=1)); ((i++)); echo $i
str='x/y/z'; IFS=/ read -ra P <<< "$str"; echo ${P[1]}
[[ "a b" == "a b" ]] && echo eq
echo $'\101'"$(printf '\102')"
set -- 1 2 3; shift $#; echo "$#"
printf '%s|' "${a[@]:0}"; echo
echo -e "a\nb" | while read l; do echo "<$l>"; done
