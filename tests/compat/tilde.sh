echo ~ | sed "s#^$HOME\$#HOME#"
echo ~/x | sed "s#^$HOME#HOME#"
echo "~" '~'
x=~
[ "$x" = "$HOME" ] && echo "assign tilde"
y=a:~/b
[ "$y" = "a:$HOME/b" ] && echo "assign colon tilde"
z="~"
echo "$z"
cd /tmp
echo ~+ 
cd /
cd /tmp
echo ~-
echo \~ ~nosuchuser_zz
HOME=/custom/home
echo ~ ~/sub
export PATH2=~/bin
echo $PATH2
arr=(~ ~/q)
echo "${arr[@]}"
echo ${undefined:-~}
