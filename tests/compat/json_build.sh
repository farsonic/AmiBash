json_escape() {
    local s=$1
    s=${s//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//$'\n'/\\n}
    s=${s//$'\t'/\\t}
    printf '%s' "$s"
}
to_json() {
    local -n _m=$1
    local first=1 k out="{"
    for k in $(printf '%s\n' "${!_m[@]}" | sort); do
        (( first )) || out+=","
        first=0
        if [[ ${_m[$k]} =~ ^-?[0-9]+$ ]]; then
            out+="\"$(json_escape "$k")\":${_m[$k]}"
        else
            out+="\"$(json_escape "$k")\":\"$(json_escape "${_m[$k]}")\""
        fi
    done
    out+="}"
    echo "$out"
}
declare -A person=([name]="Ann \"The\" Coder" [age]=37 [city]=$'Line1\nLine2' [path]='C:\dir')
to_json person
list_json() {
    local IFS=,
    local items=()
    local x
    for x in "$@"; do items+=("\"$x\""); done
    echo "[${items[*]}]"
}
list_json a b "c d"
list_json
nested="{\"people\":[$(to_json person)],\"count\":1}"
echo "$nested"
kv() { printf '"%s":"%s"' "$1" "$2"; }
echo "{$(kv a 1),$(kv b 2)}"
