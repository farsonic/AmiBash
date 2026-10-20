# ${...} operators
unset u; e=""; s="value"
echo "1:${u-def}" "${e-def}" "${s-def}"
echo "2:${u:-def}" "${e:-def}" "${s:-def}"
echo "3:${u+alt}" "${e+alt}" "${s+alt}"
echo "4:${u:+alt}" "${e:+alt}" "${s:+alt}"
echo "5:${u=assigned}" "$u"
unset u
echo "6:${e:=filled}" "$e"
echo "7:${#s}" "${#u}" "${#}"
p=/usr/local/lib/libfoo.so.1
echo "${p#*/}" "${p##*/}" "${p%.*}" "${p%%.*}"
echo "${p#/usr}" "${p%/*}" "${p##*.}"
f=archive.tar.gz
echo "${f%.gz}" "${f%%.*}" "${f#*.}" "${f##*.}"
echo "${s:1}" "${s:1:3}" "${s: -2}" "${s:(-3):2}" "${s:0:-1}" "${s: -4:-1}"
echo "${s:10}" "[${s:5}]"
x=aaa-bbb-aaa
echo "${x/a/X}" "${x//a/X}" "${x/#a/X}" "${x/%a/X}" "${x//a}" "${x/b*/}"
echo "${x//[ab]/.}" "${x/-*-/=}"
y="hello world"
echo "${y^}" "${y^^}" "${y,}" "${Y:-Y}" 
Z="HELLO World"
echo "${Z,}" "${Z,,}" "${Z~}" "${Z~~}"
echo "${y^^[lo]}" "${Z,,[LO]}"
n=12345
echo ${#n} ${n:2:2}
(echo "${u:?custom message}") 2>&1 | sed 's/.*: //'
(echo "${u?}") 2>/dev/null; echo "status $?"
v=abc
echo "${v@Q}" "${v@U}" "${v@u}" "${v@L}"
w="it's"
echo "${w@Q}"
echo "${v@A}"
declare -i num=5
echo "${num@a}"
q='a b'
echo "${q@Q}"
