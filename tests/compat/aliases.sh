shopt -s expand_aliases
alias hi='echo hello'
hi
hi there
alias greet='echo greet:'
greet world
alias
alias hi
unalias hi
hi 2>/dev/null || echo "hi gone"
alias chain='greet '
alias target='chained-target'
chain target
alias ls_al='ls'
alias e=echo
e "alias with $HOME" | sed "s#$HOME#HOME#"
f() { e "alias in function"; }
f
alias multi='echo one; echo two'
multi
unalias -a
alias 2>/dev/null | wc -l
alias q=
alias q
alias x='echo "quoted alias"'
x
