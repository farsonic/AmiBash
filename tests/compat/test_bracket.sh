t() { if "$@"; then echo "T"; else echo "F($?)"; fi; }
t [ -z "" ]
t [ -n "" ]
t [ -n "x" ]
t [ "abc" = "abc" ]
t [ "abc" != "abd" ]
t [ "abc" == "abc" ]
t [ a \< b ]
t [ b \> a ]
t [ 3 -eq 3 ]
t [ 3 -ne 3 ]
t [ 2 -lt 3 ]
t [ 3 -le 3 ]
t [ 4 -gt 3 ]
t [ 3 -ge 4 ]
t [ -5 -lt 2 ]
t [ ! -z x ]
t [ x ]
t [ "" ]
t [ ]
t [ -z ]
t [ 1 -eq 1 -a 2 -eq 2 ]
t [ 1 -eq 2 -o 2 -eq 2 ]
t [ \( 1 -eq 1 \) -a ! \( 1 -eq 2 \) ]
touch file; mkdir dir
t [ -e file ]
t [ -f file ]
t [ -d dir ]
t [ -f dir ]
t [ -e nonexist ]
t [ -s file ]
echo data > file
t [ -s file ]
t [ -r file ]
t [ -w file ]
t test -d /
t test 5 -gt 2
t test abc
t test
t [ file -nt nonexist ]
t [ abc = abc ] && echo chained
t [ "a b" = "a b" ]
x=5
t [ "$x" -eq 5 ]
t test "$x" = 5
t [ 1 -eq a ] 2>/dev/null
[ 1 -eq a ] 2>&1 | sed 's/.*: //'
t [ a = ]  2>/dev/null
t [ -t 0 ]
t [ -o errexit ] 
set -e; t [ -o errexit ]; set +e
t [ -v x ]
t [ -v nosuchvar ]
t [ 010 -eq 10 ]
t [ " 5" -eq 5 ]
t /usr/bin/test 1 = 1 2>/dev/null || t [ 1 = 1 ]
