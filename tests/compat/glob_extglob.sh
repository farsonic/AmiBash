shopt -s extglob
mkdir e && cd e || exit 1
touch foo.c foo.h bar.c baz.o qux README Makefile
echo *.@(c|h)
echo !(*.c)
echo +(f|o)*
echo ?(foo).c
echo *(ba)[rz]*
echo @(foo|bar).c
x=foo.c
case $x in *.@(c|h)) echo "c or h";; esac
[[ $x == !(*.o) ]] && echo "not .o"
[[ baz.o == !(*.o) ]] || echo "is .o"
[[ aaa == +(a) ]] && echo "plus a"
[[ "" == *(a) ]] && echo "star empty"
[[ ab == ?(a)b ]] && echo "opt a"
y=foo.bar.baz
echo "${y%%.@(bar|baz)*}"
echo "${y//@(a|o)/_}"
echo "${y/#+(f|o)/X}"
z="   trim   me   "
echo "[${z##+([[:space:]])}]"
echo "[${z%%+([[:space:]])}]"
t="${z##+( )}"; t="${t%%+( )}"; echo "[$t]"
cd ..
