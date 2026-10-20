# redirection & fd edge cases
exec 3>&1 4>&2
echo "to 3" >&3
exec 3>&- 4>&-
echo "x" 2>&1 | cat
f() { echo out; echo err >&2; }
f 2>/dev/null
f 2>&1 1>/dev/null
{ f; } 2>&1 | sort
out=$(f 2>&1); echo "$out" | sort | tr '\n' ' '; echo
exec 7>seven.txt; echo s7 >&7; exec 7>&-; cat seven.txt
echo hello >file; exec 8<file; cat <&8; exec 8<&-
echo a >f2; echo b >>f2; exec 9<f2; read -u 9 x; read -u 9 y; echo "$x$y"; exec 9<&-
cat < f2 > f3; cat f3
>f4 echo prefix-redir; cat f4
echo "$(<f2)" | wc -l
while read -r line; do echo "L:$line"; done < f2 > f5; cat f5
if true; then echo "if-redir"; fi 2>&1 > f6; cat f6
for i in 1 2; do echo $i; done | tr '\n' ,; echo
echo "noclobber" > nc; set -C; echo x > nc 2>/dev/null || echo "blocked"; set +C
echo "stderr only" 1>&2 2>/dev/null
ls nonexist_zz 2>/dev/null; echo "ls rc $?"
cat <<<"herestring" >hs; cat hs
echo "dup out" >&1
exec {fd}>dyn.txt 2>/dev/null && { echo "dyn fd" >&$fd; exec {fd}>&-; cat dyn.txt; } || echo "dyn fd"
echo done
