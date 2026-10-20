#!/bin/bash
echo "PATH=$PATH"
type argtest
argtest "hello world" x*y; echo "rc=$?"
out=$(argtest captured); echo "out=$out"
argtest piped | while read -r l; do echo "got $l"; done
argtest >RAM:redir.txt one two; cat RAM:redir.txt
cdtest RAM:; echo "after cdtest: PWD=$PWD"; cd Work:
greet() { echo "Hi $1 from ${FUNCNAME[0]}"; }; greet Amiga
cd /work/Work/proj && ls -l && wc -l notes.txt && cd -
echo "Script done: $0 $#"
