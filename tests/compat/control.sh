x=5
if [ $x -gt 3 ]; then echo big; fi
if [ $x -gt 10 ]; then echo huge; elif [ $x -gt 4 ]; then echo medium; else echo small; fi
if false; then :; else echo else-branch; fi
if ! [ $x = 5 ]; then echo no; else echo yes; fi
i=0; while [ $i -lt 3 ]; do echo "w$i"; i=$((i+1)); done
i=0; until [ $i -ge 3 ]; do echo "u$i"; i=$((i+1)); done
for i in a b c; do echo "f$i"; done
for ((i=0; i<3; i++)); do echo "c$i"; done
for ((i=10, j=0; i>j; i-=3, j++)); do echo "$i,$j"; done
for ((;;)); do echo once; break; done
for i in 1 2 3 4 5; do [ $i = 2 ] && continue; [ $i = 4 ] && break; echo "bc$i"; done
for i in 1 2; do for j in a b c; do [ $j = b ] && continue 2; echo "$i$j"; done; done
for i in 1 2; do for j in a b; do [ $j = b ] && break 2; echo "$i$j"; done; done; echo "after break 2"
i=0; while true; do i=$((i+1)); [ $i -ge 5 ] && break; done; echo "i=$i"
set -- p q r; for arg; do echo "arg $arg"; done
for i in; do echo never; done; echo "empty for ok"
case abc in a*) echo "starts a";; *) echo other;; esac
case xyz in a|b) echo ab;; x*|y*) echo "x or y";; esac
case "" in "") echo "empty case";; esac
case 5 in [0-9]) echo digit;; esac
case foo in f) echo f;; fo) echo fo;; foo) echo foo;; esac
case a in a) echo "fall1";& b) echo "fall2";; c) echo nope;; esac
case a in a) echo "cont1";;& *) echo "cont2";;& b) echo nope;; *) echo "cont3";; esac
case word in (word) echo "paren pattern";; esac
v="a b"; case $v in "a b") echo "quoted match";; esac
p='*'; case x in $p) echo "var pattern";; esac
case x in "$p") echo "no";; *) echo "quoted var literal";; esac
while false; do :; done; echo "while status $?"
if (( x > 2 )); then echo arith-if; fi
[[ $x == 5 ]] && echo "and" || echo "or"
false && echo no || echo "or branch"
true || echo no && echo "and after or"
{ echo grouped; echo commands; }
n=0; while (( n < 10 )); do (( n++ )); (( n % 2 )) && continue; echo -n "$n "; done; echo
k=3; while [ $k -gt 0 ]; do k=$((k-1)); done; echo "k=$k"
for x in 1 2 3; do :; done; echo "last x=$x"
r=$(for i in 1 2 3; do echo $i; [ $i = 2 ] && break; done); echo $r
case 1 in 1) ;; esac; echo "empty case body $?"
if true; then false; fi; echo "if status $?"
select_ok=1; echo "done"
