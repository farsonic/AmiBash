echo plain text
echo -n "no newline"; echo
echo -e "tab\there\nnewline"
echo -E "raw\tescape"
echo "default\tno-escape"
echo -e "\x41\0102C"
echo -e "stop\c here"
echo -ne "a\tb\n"
echo -- -n
echo -x
echo -
echo -nn x; echo
echo -en "combined\n"
echo ""
echo
printf '%s\n' a b c
printf '%s-%s\n' 1 2 3 4 5
printf '%d %i\n' 42 -7
printf '%5d|%-5d|%05d\n' 42 42 42
printf '%x %X %o %#x %#o\n' 255 255 8 255 8
printf '%c%c\n' hello world
printf '%5s|%-5s|%.2s\n' ab ab abcdef
printf '%f %.2f %10.3f %-8.1f|\n' 3.14159 3.14159 3.14159 2.5
printf '%e %g %g\n' 12345.678 0.0001 1000000
printf '%%literal\n'
printf 'no newline'; echo
printf '%b\n' 'a\tb' 'c\nd'
printf '%q\n' "hello world" 'it'\''s' 'a$b' '' 'plain'
printf '%s\n' "$(printf '%q' 'tab	x')"
printf '%d\n' "'A" '"B'
printf '%d\n' 0x1f 010
printf '%s %s\n' only
printf '%d\n' notnum 2>/dev/null; echo "status $?"
printf -v var '%03d' 7; echo "$var"
printf -v arr2 '%s,' x y z; echo "$arr2"
printf '%*d|%-*s|\n' 5 1 4 ab
printf '%.3d\n' 5
printf '%+d % d\n' 5 5
printf '\x41\101\n'
printf '%s\n'
printf 'a\\b\n'
printf '%5.1f%%\n' 99.44
printf '%i\n' -0
printf '%u\n' 3000000000
printf '[%10.4s]\n' abcdefgh
printf '%s\n' "${arr[@]:-empty}"
printf '%(%Y)T\n' 0 >/dev/null 2>&1; echo "T ok"
printf -- '--dash\n'
