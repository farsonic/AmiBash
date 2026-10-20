f() { echo a; if true; then echo b; elif false; then :; else echo c; fi; for i in 1 2; do echo $i; done; }
declare -f f
g() {
  local x=1 y
  while [[ $x -lt 3 ]]; do (( x++ )); done
  case $x in
    3) echo three ;;
    *) echo other ;;
  esac
  echo "$x" > /dev/null 2>&1
  { echo grp; } | cat
  ( echo sub )
  x=$(echo subst)
  [ -n "$x" ] && echo ok || echo no
  for ((i=0; i<2; i++)); do echo $i; done
  until false; do break; done
  cat <<EOF
heredoc $x
EOF
  ! true
  arr=(1 2 3)
  echo "${arr[@]}"
}
type g
h() ( echo subshell-body )
declare -f h
f() {
  [[ -n $1 && $1 == a* ]] || return 1
  echo "x" 2>&1 >/dev/null | grep -q x && echo y
  cat <<< "here string"
  case $1 in a|b) echo ab ;& c) echo c ;;& *) echo any ;; esac
  local -a arr=("$@")
  echo $(( ${#arr[@]} + 1 )) &
  wait
  if [[ $1 ]]; then echo one; elif [[ $2 ]]; then echo two; else echo none; fi
  select x in a b; do break; done
  time true
  echo done
}
declare -f f
g() { echo "single line"; }
declare -f g
h() { if true; then echo a; fi; }
declare -f h
