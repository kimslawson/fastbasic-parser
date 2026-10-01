#!/bin/sh
#
#  fbp - The missing parser for FastBasic
#
#  Runs fbp over the test programs and compares with the expected outputs.
#  Note that fbp also verifies each output against the original program.
#
#  Usage: tests/run-tests.sh path/to/fbp [-u]
#
#  With "-u", updates the expected outputs instead of comparing.
#
FBP="$1"
UPDATE="$2"
D="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d 2>/dev/null || echo /tmp/fbp-test.$$)"
mkdir -p "$T" "$D/expected"

if [ ! -x "$FBP" ]; then
    echo "Usage: $0 path/to/fbp [-u]" >&2
    exit 1
fi

total=0
fail=0
for f in "$D"/progs/*.bas "$D"/../samples/*.bas; do
    b="$(basename "$f" .bas)"
    for m in l lO s sO S Sa; do
        case $m in
            l)  opt="-l" ;;
            lO) opt="-l -u -O" ;;
            s)  opt="-s -e" ;;
            sO) opt="-s -e -O" ;;
            S)  opt="-S -e -n 80" ;;
            Sa) opt="-S" ;;
        esac
        total=$((total + 1))
        out="$T/$b.$m"
        if ! "$FBP" -q $opt -o "$out" "$f" 2> "$T/err"; then
            echo "FAIL (fbp error): $b [$opt]"
            cat "$T/err"
            fail=$((fail + 1))
            continue
        fi
        exp="$D/expected/$b.$m"
        if [ "$UPDATE" = "-u" ]; then
            cp "$out" "$exp"
        elif ! cmp -s "$out" "$exp"; then
            echo "FAIL (different output): $b [$opt]"
            diff "$exp" "$out" | head -10
            fail=$((fail + 1))
        fi
    done
done
rm -rf "$T"
echo "Tests: $total, failures: $fail."
[ $fail -eq 0 ]
