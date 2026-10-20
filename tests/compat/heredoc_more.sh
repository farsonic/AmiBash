v=val
cat <<EOF
	tabs kept $v
  spaces kept
\$escaped \`bt\` \\ backslash
"double" 'single'
$(echo "cmd $v")
${v^^}
$((2**8))
EOF
cat <<'X'
$v `date` \n
X
cat <<-"END"
		stripped $v
	END
cat <<EOF
a
EOF
cat <<EOF
EOF
cat <<"E O F"
odd delimiter
E O F
fn() {
	cat <<-EOF
		in fn $1
	EOF
}
fn x
while read -r a b; do echo "$b-$a"; done <<EOF
1 one
2 two
EOF
cat <<EOF > hd.txt
to file $v
EOF
cat hd.txt
cat <<A; cat <<B
first A
A
second B
B
if true; then
cat <<EOF
inside if
EOF
fi
x=$(cat <<EOF
multi
line
EOF
); echo "$x" | wc -l
cat <<EOF | sort -r
b
c
a
EOF
cat << EOF
spaced delimiter op
EOF
cat <<EOF
backslash-newline \
joined
EOF
cat <<'EOF'
no join \
here
EOF
