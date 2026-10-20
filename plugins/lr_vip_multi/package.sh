#!/bin/bash
# Assembles build/package/lr_vip_multi: the library and manifest from build/, everything else from dist/.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/build/package/lr_vip_multi"
rm -rf "$OUT"
mkdir -p "$OUT/bin"
cp "$ROOT/build/plugins/lr_vip_multi/liblr_vip_multi.so" "$OUT/bin/"
cp "$ROOT/build/plugins/lr_vip_multi/lr_vip_multi.pplugin" "$OUT/"
if [ -d "$HERE/dist" ]; then cp -r "$HERE/dist/." "$OUT/"; fi
echo "package: $OUT"
