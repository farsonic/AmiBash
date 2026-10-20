f() { return $1; }
f 1 | f 2 | f 3; echo "${PIPESTATUS[*]} $?"
: | f 4; echo "${PIPESTATUS[@]}"
true; echo "${PIPESTATUS[*]}"
false; echo "${PIPESTATUS[*]}"
! f 5 | f 0; echo "neg $? ${PIPESTATUS[*]}"
set -o pipefail
f 2 | f 0 | f 0; echo "pf $?"
f 0 | f 3 | f 4; echo "pf last nonzero $?"
if f 1 | f 0; then echo yes; else echo "pf if no"; fi
set +o pipefail
r=$(: | f 7); echo "subst $?"
{ f 2 | f 0; } ; echo "group ${PIPESTATUS[*]}"
cnt=$(printf 'a\nb\n' | wc -l); echo "cnt=$cnt"
echo abc | tee /dev/null | cat
echo "${PIPESTATUS[@]}"
