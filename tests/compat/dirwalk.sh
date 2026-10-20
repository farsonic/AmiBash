mkdir -p tree/a/b/c tree/d tree/e/f
touch tree/top.txt tree/a/one.txt tree/a/b/two.txt tree/a/b/c/three.txt tree/d/four.log tree/e/f/five.txt
walk() {
    local dir=$1 indent=$2 entry
    for entry in "$dir"/*; do
        [ -e "$entry" ] || continue
        if [ -d "$entry" ]; then
            echo "${indent}${entry##*/}/"
            walk "$entry" "$indent  "
        else
            echo "${indent}${entry##*/}"
        fi
    done
}
walk tree ""
count_files() {
    local dir=$1 n=0 e
    for e in "$dir"/*; do
        if [ -d "$e" ]; then n=$(( n + $(count_files "$e") ))
        elif [ -f "$e" ]; then n=$((n + 1)); fi
    done
    echo $n
}
echo "files: $(count_files tree)"
find_ext() {
    local dir=$1 ext=$2 e
    for e in "$dir"/*; do
        if [ -d "$e" ]; then find_ext "$e" "$ext"
        elif [[ $e == *.$ext ]]; then echo "$e"; fi
    done
}
find_ext tree txt
echo "logs: $(find_ext tree log)"
depth() {
    local d=$1 max=0 e sub
    for e in "$d"/*/; do
        [ -d "$e" ] || continue
        sub=$(depth "${e%/}")
        (( sub + 1 > max )) && max=$((sub + 1))
    done
    echo $max
}
echo "depth: $(depth tree)"
find tree -name '*.txt' | sort
find tree -type d | sort | head -4
