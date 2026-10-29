#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
[[ "$(uname -s)" == Darwin ]] || { echo 'Can macOS/Xcode SDK; Windows dung workflow GitHub Actions kem theo.' >&2; exit 1; }
command -v cmake >/dev/null
xcrun --sdk iphoneos --find clang >/dev/null
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DVT_BUILD_PLUGIN=ON
mkdir -p dist
xcodebuild -project build-ios/VTMenuIOS.xcodeproj -scheme VTMenu -configuration Release -destination 'generic/platform=iOS' "CONFIGURATION_BUILD_DIR=$PWD/dist" CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
test -f dist/VTMenu.dylib
python3 - <<'PY'
from pathlib import Path
import json,hashlib,struct
p=Path('dist/VTMenu.dylib');b=p.read_bytes()
assert b[:4]==b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I',b,4)[0]==0x100000c
assert struct.unpack_from('<I',b,12)[0]==6
profile=json.loads(Path('analysis/ios_build.json').read_text())
meta={k:profile[k] for k in ('bundle','version','build','framework_uuid')}
meta['menu_sha256']=hashlib.sha256(b).hexdigest();meta['signed']=False
meta['menu_revision']=5;meta['features']=['esp','spam_jump']
Path('dist/menu-build.json').write_text(json.dumps(meta,indent=2))
PY
otool -L dist/VTMenu.dylib
cp tao_ipa_menu.py dist/
cp HUONG_DAN_WINDOWS.md dist/
cp Resources/FONT_LICENSE.txt dist/
cp third_party/imgui/LICENSE.txt dist/IMGUI_LICENSE.txt
cp third_party/lumin/LICENSE dist/LUMIN_LICENSE.txt
cp third_party/lumin/NOTICE.md dist/LUMIN_NOTICE.md
echo 'Created dist/VTMenu.dylib and menu-build.json. This is not a signed IPA.'
