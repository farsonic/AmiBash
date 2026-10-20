shopt extglob
shopt -s extglob; shopt extglob
shopt -u extglob; shopt extglob
shopt -q nullglob; echo "q $?"
shopt -s nullglob dotglob; shopt nullglob dotglob
shopt -p nullglob
shopt -u nullglob dotglob
shopt -s nosuchopt 2>&1 | sed 's/^.*shopt/shopt/'
set -o noglob; echo *; set +o noglob
set -o | grep -c noglob
shopt -o noglob 2>/dev/null | tr -s ' \t' ' '
echo "${BASHOPTS:+has bashopts}"
