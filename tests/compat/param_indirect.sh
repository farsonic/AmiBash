# indirection, prefix listing, nameref
foo=bar; bar=baz
echo "${!foo}"
name=bar
echo "${!name}"
pre_a=1 pre_b=2 pre_c=3
echo "${!pre_*}"
for v in "${!pre_@}"; do echo "$v=${!v}"; done
arr=(x y z)
ref='arr[1]'
echo "${!ref}"
declare -n nr=foo
echo "$nr"
nr=changed
echo "$foo"
f() { local -n r=$1; r="set by f"; }
f target
echo "$target"
g() { local -n a=$1; a+=(new); echo "${#a[@]}"; }
list=(1 2)
g list
echo "${list[@]}"
unset foo
echo "${!foo-unset}" "${foo:-gone}"
idx=2
echo "${arr[idx]}" "${arr[$idx]}" "${arr[idx-1]}"
