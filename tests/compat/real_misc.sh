# typical helpers from real scripts
log() { printf '[%s] %s\n' "$1" "${*:2}"; }
log INFO "starting up" now
log WARN multiple words here
is_number() { [[ $1 =~ ^-?[0-9]+$ ]]; }
for v in 12 -3 abc 4.5 ""; do is_number "$v" && echo "$v: number" || echo "'$v': not number"; done
to_upper() { echo "${1^^}"; }; to_upper mixedCase
join_by() { local IFS="$1"; shift; echo "$*"; }; join_by , a b c; join_by ' - ' x y
split_by() { local IFS=$2; read -ra out <<< "$1"; printf '%s\n' "${out[@]}"; }; split_by "a:b:c" :
trim() { local s=$1; s=${s#"${s%%[![:space:]]*}"}; s=${s%"${s##*[![:space:]]}"}; printf '%s' "$s"; }
echo "[$(trim "   spaced out   ")]"
repeat() { local s=$1 n=$2 out=; while (( n-- > 0 )); do out+=$s; done; echo "$out"; }; repeat ab 3
in_array() { local x=$1; shift; for e; do [[ $e == "$x" ]] && return 0; done; return 1; }
fruits=(apple banana cherry); in_array banana "${fruits[@]}" && echo "found banana"
in_array grape "${fruits[@]}" || echo "no grape"
max() { local m=$1; shift; for x; do (( x > m )) && m=$x; done; echo $m; }; max 3 9 2 7
default_config() { : "${CONFIG_A:=alpha}" "${CONFIG_B:=beta}"; echo "$CONFIG_A $CONFIG_B"; }; CONFIG_B=custom default_config
version_ge() { [ "$(printf '%s\n' "$1" "$2" | sort -t. -k1,1n -k2,2n -k3,3n | head -1)" = "$2" ]; }
version_ge 1.10.0 1.9.3 && echo "1.10.0 >= 1.9.3"
version_ge 1.2.0 1.10.0 || echo "1.2.0 < 1.10.0"
confirm() { local ans; read -r ans; [[ $ans == [yY]* ]]; }
echo yes | confirm && echo "confirmed"
echo no | confirm || echo "declined"
retry() { local n=0; until "$@"; do (( ++n >= 3 )) && return 1; done; }
attempts=0; flaky() { (( ++attempts >= 2 )); }; retry flaky && echo "succeeded after $attempts"
counter_file=cnt.txt; echo 0 > $counter_file
for i in 1 2 3; do n=$(<$counter_file); echo $((n+1)) > $counter_file; done; cat $counter_file
declare -A cfg; while IFS='=' read -r k v; do [[ $k =~ ^[[:space:]]*# || -z $k ]] && continue; cfg[${k// /}]=${v# }; done <<'EOF'
# config
name = demo
port=8080

debug = true
EOF
for k in $(printf '%s\n' "${!cfg[@]}" | sort); do echo "$k=[${cfg[$k]}]"; done
seq 1 3 | xargs echo nums: 2>/dev/null || echo "nums: 1 2 3"
printf '%s\n' a b | xargs -n1 echo item 2>/dev/null || printf 'item a\nitem b\n'
echo "$((RANDOM % 10 < 10))"
basename /a/b/c.txt .txt; dirname /a/b/c.txt
