parse() {
  local OPTIND opt
  while getopts "ab:c" opt; do
    case $opt in
      a) echo "flag a";;
      b) echo "b=$OPTARG";;
      c) echo "flag c";;
      \?) echo "invalid";;
    esac
  done
  shift $((OPTIND - 1))
  echo "rest: $*"
}
parse -a -b val file1 file2
parse -ac -bX
parse -b "with space" -- -a
parse -z 2>&1
parse file -a
silent() {
  local OPTIND opt
  while getopts ":x:y" opt; do
    case $opt in
      x) echo "x=$OPTARG";;
      y) echo "y";;
      :) echo "missing arg for $OPTARG";;
      \?) echo "unknown $OPTARG";;
    esac
  done
}
silent -x
silent -q -y
silent -x1 -y
OPTIND=1
set -- -v -f out.txt in.txt
while getopts "vf:" o; do echo "$o ${OPTARG:-}"; done
echo "OPTIND=$OPTIND"
shift $((OPTIND-1)); echo "$@"
OPTIND=1
getopts "a" o -a; echo "$o $?"
getopts "a" o -a; echo "end $?"
