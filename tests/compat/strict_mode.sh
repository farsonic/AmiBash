set -euo pipefail
IFS=$'\n\t'
script_name=${BASH_SOURCE[0]##*/}
echo "script: $script_name"
die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing $1"; }
need echo
tmp=$(mktemp)
trap 'rm -f "$tmp"; echo cleanup' EXIT
echo "data" > "$tmp"
[[ -s $tmp ]] && echo "tmp has data"
count=${1:-0}
echo "count=$count"
opt=${UNSET_OPT:-}
[[ -z $opt ]] && echo "opt empty"
read -r -d '' multi <<'EOF' || true
first line
second line
EOF
echo "$multi" | wc -l
arr=(one two three)
for a in "${arr[@]}"; do echo "item $a"; done
result=$(printf '%s\n' "${arr[@]}" | grep -c o || true)
echo "with o: $result"
if ! grep -q nothing "$tmp"; then echo "grep negation ok"; fi
val=$(echo "x" | tr x y)
echo "$val"
(( count++ )) || true
echo "count now $count"
set +u
echo "${never_set}"
set -u
f() { local first=$1; local second=${2:-def}; echo "$first $second"; }
f a
f a b
echo "end"
missing_var_test() { echo "$undefined_in_strict"; }
missing_var_test
echo "not reached"
