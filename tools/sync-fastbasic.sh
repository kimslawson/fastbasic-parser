#!/bin/sh
#
#  fbp - The missing parser for FastBasic
#
#  Copies the needed files from a FastBasic source tree into vendor/fastbasic.
#
#  Usage: tools/sync-fastbasic.sh /path/to/fastbasic
#
#  fbp uses FastBasic's own grammar (.syn files), target definitions, syntax
#  table loader and peephole optimizer unmodified, so that the parsing is
#  always exactly the same as the FastBasic compiler.  After syncing, rebuild
#  and run "make test".  If the "reference" files changed, review the diff and
#  port the changes to src/engine.cc, as that file is derived from them.
#
set -e

if [ $# -ne 1 ] || [ ! -f "$1/src/syntax/basic.syn" ]; then
    echo "Usage: $0 /path/to/fastbasic-source" >&2
    exit 1
fi

FB="$1"
D="$(dirname "$0")/../vendor/fastbasic"

rm -rf "$D"
mkdir -p "$D/compiler" "$D/syntax" "$D/targets" "$D/reference"

# Compiled into fbp unmodified:
for f in \
    atarifp.cc atarifp.h codew.h ifile.cc ifile.h looptype.cc looptype.h \
    peephole.cc peephole.h synt-optimize.cc synt-optimize.h synt-parser.cc \
    synt-parser.h synt-preproc.cc synt-preproc.h synt-pstate.cc synt-pstate.h \
    synt-sm-list.h synt-sm.cc synt-sm.h synt-symlist.cc synt-symlist.h \
    synt-wlist.cc synt-wlist.h vartype.cc vartype.h
do
    cp "$FB/src/compiler/$f" "$D/compiler/"
done

# Not compiled, src/engine.cc is derived from those:
for f in parser.h parser.cc parser-actions.cc parser-actions.h compile.cc; do
    cp "$FB/src/compiler/$f" "$D/reference/"
done

# Grammar and target definitions, embedded into fbp:
cp "$FB"/src/syntax/*.syn "$D/syntax/"
cp "$FB"/compiler/*.tgt "$D/targets/"

cp "$FB/LICENSE" "$D/LICENSE"

# Record upstream version
{
    echo "FastBasic sources copied from https://github.com/dmsc/fastbasic"
    if git -C "$FB" rev-parse HEAD >/dev/null 2>&1; then
        echo "commit:   $(git -C "$FB" rev-parse HEAD)"
        echo "describe: $(git -C "$FB" describe --tags --always)"
        echo "date:     $(git -C "$FB" log -1 --format=%cd --date=short)"
    fi
    grep '^VERSION' "$FB/version.mk" 2>/dev/null || true
} > "$D/VERSION"

echo "Synced FastBasic files into $D:"
cat "$D/VERSION"
