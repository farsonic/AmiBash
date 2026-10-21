$((6*7)); ls /
echo hello $((6*7))
export AMIBASH_DEBUG=1; echo first
echo in window two; cd RAM:
exit
echo I am in Bash 2; pwd
exit
echo started from Workbench; echo args=$#
exit
echo $0 $BASH; ls | head -3; cd RAM: ; pwd
version; dir C: | head -3; echo rc=$?
exit
argtest wb; echo $PATH
