name=World
cat <<EOF
Hello $name
Math: $((1+2))
Cmd: $(echo sub)
Escaped: \$name \\ \`
Literal 'single' "double"
EOF
cat <<'EOF'
No $expansion here $(echo x) \n
EOF
cat <<"EOF"
Also $none
EOF
cat <<-EOF
	tab stripped $name
		two tabs
	EOF
cat <<EOF1; cat <<EOF2
first
EOF1
second
EOF2
f() {
    cat <<EOF
in function $1
EOF
}
f arg
for i in 1 2; do
    cat <<EOF
loop $i
EOF
done
while read -r line; do echo "read: $line"; done <<EOF
alpha
beta
EOF
cat <<EOF | tr a-z A-Z
piped heredoc
EOF
x=$(cat <<EOF
captured
EOF
)
echo "$x"
cat <<E\OF
quoted by backslash $name
EOF
cat <<EOF
EOF
echo "empty done"
cat << 'END' > out.txt
written to file
END
cat out.txt
cat <<EOF
line with trailing backslash \
continues
EOF
read -r a b <<EOF
one two
EOF
echo "$a|$b"
cat <<EOF
${name:-x} ${#name} ${name/o/0}
EOF
