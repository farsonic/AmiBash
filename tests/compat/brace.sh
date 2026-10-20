echo {a,b,c}
echo x{a,b}y
echo {1..5}
echo {5..1}
echo {1..10..3}
echo {10..1..4}
echo {a..e}
echo {e..a..2}
echo {01..10}
echo {-3..3}
echo {-05..5..5}
echo {a,b}{1,2}
echo {a,{b,c},d}
echo {a{1,2},b}
echo pre{,fix}
echo {a}
echo {a..}
echo {1..3}{a..b}
echo a{b,c}d{e,f}
echo "{a,b}" '{a,b}' \{a,b\}
x=1
echo {$x,2}
echo {1..$x}
echo file.{txt,log,md}
mkdir -p d/{x,y}/{1,2}
ls -d d/*/* 
echo {,}
echo {a,}b
echo { a,b }
echo {a,b}-{c}
echo {X..c} | tr -d '\\`'
echo ~/{a,b} | sed "s#$HOME#HOME#g"
for i in {1..3}; do printf '%s ' "$i"; done; echo
set -B; echo {x,y}; set +B; echo {x,y}; set -B
echo {1..3..0}
echo {a..c..-1}
