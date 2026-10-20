eval 'if true; then echo x; fi fi'
echo "st $?"
eval 'echo $(( 1 + )'
echo "st $?"
eval 'for in; do'
echo "st $?"
eval '( echo a'
echo "st $?"
eval 'echo "unterminated'
echo "st $?"
eval 'case x in'
echo "st $?"
eval 'f() {'
echo "st $?"
eval '}'
echo "st $?"
eval 'echo a;;'
echo "st $?"
printf 'echo in-src\nif then\necho after\n' > bad.sh
source ./bad.sh
echo "source status $?"
eval 'echo ok; echo "two"'
eval "echo 'unterminated"
echo "still running $?"
