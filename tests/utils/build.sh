#!/bin/sh
# build the utility test driver on the host
R=$(cd "$(dirname "$0")/../.." && pwd)
RE="$R/tests/utils/re_posix.c"
if [ -f "$R/src/regex.c" ] && [ -z "$USE_POSIX_RE" ]; then RE="$R/src/regex.c"; fi
exec gcc -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I"$R/src" \
  "$R/src/util.c" "$R/src/io.c" "$R/src/path.c" "$R/src/os_posix.c" "$R/src/timeutil.c" \
  "$R/src/utils_fs.c" "$R/src/utils_text.c" "$R/src/utils_misc.c" \
  "$R/tests/utils/driver.c" "$RE" -o "${OUT:-$R/tests/utils/driver}"
