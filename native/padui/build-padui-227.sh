#!/bin/bash
# Compile PadUI.u for Unreal 227 with the Windows UCC of a 227 install (default: ../../Unreal, the spike's local
# copy; override with UNREAL=...). Same source as UT: lines tagged //#UT are dropped, //#227 lines are uncommented.
# Output: native/padui/out-227/PadUI.u + PadUI.int, copied to the unreal flavor's assets (Android/app/src/unreal/assets/padui-227/).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
UNREAL="${UNREAL:-$HERE/../../Unreal}"
SYS="$UNREAL/SYSTEM"; [ -d "$SYS" ] || SYS="$UNREAL/System"
rm -rf "$UNREAL/PadUI" "$SYS/PadUI.u"
mkdir -p "$UNREAL/PadUI/Classes" "$HERE/out-227"
python -I - "$HERE/Classes" "$UNREAL/PadUI/Classes" <<'PY'
import os, sys
src, dst = sys.argv[1], sys.argv[2]
for name in os.listdir(src):
    out = []
    for line in open(os.path.join(src, name), encoding='utf-8').read().split('\n'):
        if '//#UT' in line:
            continue
        if line.lstrip().startswith('//#227 '):
            line = line.replace('//#227 ', '', 1)
        out.append(line)
    open(os.path.join(dst, name), 'w', encoding='utf-8', newline='').write('\n'.join(out))
PY
python -I - "$SYS/Unreal.ini" "$SYS/PadUIMake.ini" <<'PY'
import sys
lines = open(sys.argv[1], encoding='latin-1').read().split('\n')
last = max(i for i, l in enumerate(lines) if l.startswith('EditPackages='))
lines.insert(last + 1, 'EditPackages=PadUI')
open(sys.argv[2], 'w', encoding='latin-1', newline='').write('\n'.join(lines))
PY
cd "$SYS"
./UCC.exe make -ini=PadUIMake.ini -silent 2>&1 | tr -d '\0' | tr '\r' '\n' | grep -a -i -E "error|warning|success|failed" | tail -20
cp PadUI.u "$HERE/out-227/"
rm -rf "$UNREAL/PadUI" PadUIMake.ini PadUI.u
cp "$HERE/int/PadUI.int" "$HERE/out-227/"
mkdir -p "$HERE/../../Android/app/src/unreal/assets/padui-227"
cp "$HERE/out-227/PadUI.u" "$HERE/out-227/PadUI.int" "$HERE/../../Android/app/src/unreal/assets/padui-227/"
ls -l "$HERE/out-227/PadUI.u" "$HERE/out-227/PadUI.int"
