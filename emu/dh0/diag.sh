[ -t 0 ] && echo stdin-tty || echo stdin-notty
[ -t 1 ] && echo out-tty || echo out-notty
echo PATH=$PATH; echo HOME=$HOME PWD=$PWD
echo "--- ls /"; ls /
echo "--- ls -l C:"; ls -l C:
type ls cd dir list assign
echo T=$(echo hi > /tmp/x && cat /tmp/x)
echo os=$OSTYPE $BASH_VERSION
