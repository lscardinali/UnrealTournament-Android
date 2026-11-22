#!/bin/bash
# Compile PadUI.u with the Windows UCC of the PC install (UnrealTournament/), output native/padui/out/PadUI.u.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
UT="$HERE/../../UnrealTournament"
rm -rf "$UT/PadUI" "$UT/System/PadUI.u"
mkdir -p "$UT/PadUI/Classes" "$HERE/out"
cp "$HERE"/Classes/*.uc "$UT/PadUI/Classes/"
# make ini: the install's ini plus EditPackages=PadUI (after the last EditPackages line)

python -I - "$UT/System/UnrealTournament.ini" "$UT/System/PadUIMake.ini" <<'PY'
import sys
lines = open(sys.argv[1], encoding='latin-1').read().split('\n')
last = max(i for i, l in enumerate(lines) if l.startswith('EditPackages='))
lines.insert(last + 1, 'EditPackages=PadUI')
open(sys.argv[2], 'w', encoding='latin-1', newline='').write('\n'.join(lines))
PY
cd "$UT/System"
./UCC.exe make -ini=PadUIMake.ini -silent 2>&1 | grep -v -E "^\s*$" | tail -30
cp PadUI.u "$HERE/out/"
rm -rf "$UT/PadUI" PadUIMake.ini PadUI.u
ls -l "$HERE/out/PadUI.u"
