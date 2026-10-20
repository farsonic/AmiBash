nosuch_command_zz
echo "after not found: $?"
nosuch_command_zz arg1 arg2
./no/such/path
cd /nonexistent_dir_zz
echo "cd status $?"
readonly RO=1
RO=2
echo "ro status $?"
unset RO
echo "unset ro $?"
echo ${x!}
echo "after bad subst"
declare -A aa; aa=(1)
local outside=1
echo "local status $?"
shift 99
echo "shift status $?"
break
continue
return 5
echo "return outside $?"
"" 2>&1
echo "empty cmd $?"
f() { nosuch_inner_zz; }; f
echo "f status $?"
echo $(( 1 / 0 )) 
cat nonexistent_file_zz
echo "cat status $?"
[ 1 -eq ]
echo "test status $?"
let
echo end
