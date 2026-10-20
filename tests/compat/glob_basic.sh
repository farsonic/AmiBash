# globbing in a temp dir
mkdir g && cd g || exit 1
touch a.txt b.txt c.log .hidden ab abc Abc 'sp ace.txt'
mkdir sub sub2; touch sub/x.txt sub2/y.txt
echo *
echo *.txt
echo ?b
echo [ab]*
echo [!a]*
echo [^a]*
echo a?? 
echo [[:upper:]]*
echo *.none
echo "*.txt"
echo '*'.txt
echo \*.txt
echo */
echo */*.txt
echo s*/*
x='*.log'
echo $x "$x"
shopt -s nullglob
echo none: *.none :end
arr=(*.zzz)
echo "${#arr[@]}"
shopt -u nullglob
shopt -s dotglob
echo *
shopt -u dotglob
echo .*
echo .h*
shopt -s nocaseglob
echo a*
shopt -u nocaseglob
for f in *.txt; do echo "file: $f"; done
set -f
echo *
set +f
shopt -s failglob
echo *.nomatch
echo "after failglob $?"
shopt -u failglob
echo [a-b]*
echo [c-]*
echo a[]]* 2>/dev/null
touch 'a]b'
echo a[]]*
echo *[[:space:]]*
cd ..
