PS3="choose: "
select item in apple banana cherry; do
    echo "picked [$item] reply=$REPLY"
    [[ $REPLY == 3 ]] && break
done <<'EOF' 2>/dev/null
1
9
2
3
EOF
echo "done select"
