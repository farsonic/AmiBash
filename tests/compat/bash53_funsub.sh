# bash 5.3: ${ command; } and ${| command; } run in the current shell
x=1
a=${ echo out; x=2; }
echo "a=[$a] x=$x"
b=${ printf 'l1\nl2\n\n\n'; }
echo "b=[$b]"
c=${ false; }; echo "status $?"
d=${| REPLY=val; echo printed; }
echo "d=[$d] REPLY=[${REPLY-unset}]"
REPLY=outer; e=${| REPLY=inner; }; echo "e=$e REPLY=$REPLY"
REPLY=outer; e=${| echo "[${REPLY-unset}]"; }; echo "e=[$e] $?"
f() { echo "in f $1"; return 3; }
g=${ f arg; }; echo "g=[$g] $?"
h="pre ${ echo mid; } post"; echo "$h"
i=${ local z=5; echo $z; }; echo "i=$i z=${z-unset}"
j=${ echo $BASH_SUBSHELL; }; echo "subshell $j"
k=${
echo multi
echo line
}; echo "[$k]"
echo "${ echo a; } ${ echo b; }"
echo ${ echo a; }x "${ echo b; }y" ${ echo c;}
m=${|f2() { REPLY=fn; }; f2; }; echo "m=$m"
n=${ return 4; echo no; }; echo "n=[$n] $?"
o=${ echo ${ echo inner; }; }; echo "o=$o"
p=${ { echo grp; }; }; echo "p=$p"
q=${ case a in a) echo case;; esac; }; echo "q=$q"
r=${ echo a}; }; echo "r=[$r]"
set -e; s=${ false; echo after; }; echo "s=[$s]"; set +e
fn() { local v; v=${ echo "in ${FUNCNAME[0]}"; }; echo "$v"; echo "${ echo inside; }"; }
declare -f fn
fn
t=${ exit 0; echo x; }
echo not-reached
