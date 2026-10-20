usage() { echo "usage: prog [-v] [-n count] [-o file] args..."; }
main() {
    local verbose=0 count=1 output= OPTIND opt
    while getopts ":vn:o:h" opt; do
        case $opt in
            v) verbose=$((verbose + 1)) ;;
            n) [[ $OPTARG =~ ^[0-9]+$ ]] || { echo "bad count: $OPTARG"; return 2; }
               count=$OPTARG ;;
            o) output=$OPTARG ;;
            h) usage; return 0 ;;
            :) echo "option -$OPTARG needs an argument"; return 2 ;;
            \?) echo "unknown option -$OPTARG"; usage; return 2 ;;
        esac
    done
    shift $((OPTIND - 1))
    echo "verbose=$verbose count=$count output=${output:-stdout} args=$#"
    local i
    for ((i = 0; i < count; i++)); do
        for a in "$@"; do
            if [[ -n $output ]]; then echo "$a" >> "$output"; else echo "[$i] $a"; fi
        done
    done
    [[ -n $output ]] && { echo "wrote $(wc -l < "$output") lines"; rm -f "$output"; }
    return 0
}
main -v -v -n 2 foo bar; echo "rc=$?"
main -o out.txt -n 3 x; echo "rc=$?"
main -n abc; echo "rc=$?"
main -x; echo "rc=$?"
main -n; echo "rc=$?"
main -h; echo "rc=$?"
main -- -v; echo "rc=$?"
main -vn1 a; echo "rc=$?"
# long option handling by hand
parse_long() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --name=*) echo "name=${1#*=}" ;;
            --name) echo "name=$2"; shift ;;
            --flag) echo "flag" ;;
            --) shift; echo "rest: $*"; break ;;
            -*) echo "unknown $1" ;;
            *) echo "arg $1" ;;
        esac
        shift
    done
}
parse_long --name=x --flag pos --name y -q -- a b
