echo one > f1
echo two >> f1
cat f1
echo three > f1
cat f1
cat < f1
echo err >&2 2>/dev/null
{ echo out; echo err >&2; } > both 2>&1
cat both
{ echo out2; echo err2 >&2; } &> both2
cat both2
{ echo o3; echo e3 >&2; } &>> both2
cat both2
ls nosuchfile_zz 2> errfile; echo "status $?"
[ -s errfile ] && echo "errfile non-empty"
echo "to devnull" > /dev/null
echo hi 1>&2 2>/dev/null | cat
exec 3> fd3.txt
echo "via fd3" >&3
echo "more fd3" >&3
exec 3>&-
cat fd3.txt
exec 4< f1
read -r line <&4
echo "read from 4: $line"
exec 4<&-
exec 5>&1
exec > redirected.txt
echo "this goes to file"
exec >&5 5>&-
echo "back to stdout"
cat redirected.txt
> empty.txt
[ -e empty.txt ] && [ ! -s empty.txt ] && echo "empty created"
cat < nonexist 2>/dev/null || echo "input redirect failed"
echo a > x1 > x2
cat x1 x2
set -o noclobber
echo new > f1 2>/dev/null || echo "noclobber blocked"
echo forced >| f1
cat f1
set +o noclobber
f() { echo "func out"; echo "func err" >&2; }
f > fo 2> fe
cat fo fe
for i in 1 2 3; do echo $i; done > loopout
cat loopout
while read -r l; do echo "L$l"; done < loopout
if true; then echo inif; fi > ifout
cat ifout
echo "pre" 2>&1 >/dev/null
exec 6<> rw.txt
echo "rw" >&6
exec 6>&-
cat rw.txt
{ echo a; echo b; } | { read x; read y; echo "$y$x"; }
echo text >&-  2>/dev/null; echo "closed status $?"
echo "dup" 3>&1 1>&2 2>&3 2>/dev/null
