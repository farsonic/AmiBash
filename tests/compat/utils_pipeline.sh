printf 'b 2\na 10\nc 1\na 3\n' > f.txt
sort f.txt; sort -r f.txt; sort -k2 -n f.txt; sort -u -k1,1 f.txt; sort -t' ' -k2,2nr f.txt
uniq -c <(sort f.txt) 2>/dev/null | head -2
cut -d' ' -f2 f.txt | sort -n | tail -1
head -c 5 f.txt; echo
tail -n 2 f.txt
tail -c 4 f.txt
wc f.txt; wc -l < f.txt
grep -n a f.txt; grep -v a f.txt; grep -c '' f.txt; grep -E 'a [0-9]{2}' f.txt; grep -w 1 f.txt
grep -l a f.txt; grep -h a f.txt f.txt | wc -l; grep -i A f.txt | head -1
sed 's/ /=/' f.txt; sed -n '$p' f.txt; sed '2,3d' f.txt; sed 'y/abc/ABC/' f.txt; sed '/a/s/$/!/' f.txt
tr 'a-c' 'A-C' < f.txt; tr -s ' ' < f.txt | head -1; tr -cd '0-9\n' < f.txt
seq 5; seq 2 2 8; seq -s, 3; seq 3 -1 1
cat -n f.txt; nl f.txt 2>/dev/null | head -1
paste f.txt f.txt | head -1
rev f.txt 2>/dev/null | head -1
expr 3 + 4; expr length abc 2>/dev/null; expr substr hello 2 3 2>/dev/null
basename /x/y.sh .sh; dirname /x/y.sh; dirname y.sh
echo "a b c" | wc -w
printf 'x\ny\n' | tac 2>/dev/null
od -c <<< "ab" | head -1
yes 2>/dev/null | head -2
echo hello | cut -c2-4
echo "1,2,3" | cut -d, -f2-
join <(printf 'a 1\nb 2\n') <(printf 'a x\nb y\n') 2>/dev/null
comm <(printf 'a\nb\n') <(printf 'b\nc\n') 2>/dev/null
mkdir -p dd/ee; touch dd/ee/ff; find dd | sort; find dd -type f; ls dd; ls -1 dd/ee
cp -r dd dd2; ls dd2/ee; rm -r dd2; ls dd2 2>/dev/null || echo gone
touch t1; mv t1 t2; ls t*
du -s dd >/dev/null 2>&1 && echo du-ok
date -d @0 +%Y 2>/dev/null || date -u +%Y >/dev/null && echo date-ok
true; false; echo $?
env | grep -c '^PATH='
printenv HOME >/dev/null && echo printenv-ok
test -x /bin/sh && echo x-ok
