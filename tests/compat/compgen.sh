myvar_a=1 myvar_b=2
compgen -v myvar_ 2>/dev/null
f_one() { :; }; f_two() { :; }
compgen -A function f_ 2>/dev/null
compgen -W "alpha beta gamma" -- b 2>/dev/null
compgen -W "alpha beta gamma" 2>/dev/null | wc -l
mkdir cg; touch cg/file1 cg/file2
compgen -f cg/f 2>/dev/null | sort
compgen -d c 2>/dev/null
compgen -b ech 2>/dev/null
compgen -k whi 2>/dev/null
compgen -A variable HOM 2>/dev/null
alias al_x=echo; compgen -a al_ 2>/dev/null
echo "rc $?"
