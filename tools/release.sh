#!/usr/bin/env bash
# Build, sign and publish a firmware release. Run from the repository root.
# Usage: tools/release.sh MAJOR.MINOR.PATCH   (see docs/OTA.md)
set -euo pipefail

VERSION="${1:?usage: tools/release.sh MAJOR.MINOR.PATCH}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must be MAJOR.MINOR.PATCH"; exit 1; }

KEY="${OTA_PRIVATE_KEY:-$HOME/.relay-ota/ota_private.pem}"
PUB="${OTA_PUBLIC_KEY:-$HOME/.relay-ota/ota_public.pem}"
[[ -f "$KEY" ]] || { echo "private key not found: $KEY"; exit 1; }
[[ -f "$PUB" ]] || { echo "public key not found: $PUB"; exit 1; }
for cmd in pio gh openssl python3 git; do
  command -v "$cmd" >/dev/null || { echo "missing tool: $cmd"; exit 1; }
done
[[ -z "$(git status --porcelain)" ]] || { echo "working tree not clean: commit or stash first"; exit 1; }

if grep -qE 'your-|change-me' include/ota_config.h; then
  echo "include/ota_config.h still has placeholder values: configure it before releasing"
  exit 1
fi

printf '#pragma once\n#define FIRMWARE_VERSION "%s"\n' "$VERSION" > src/version.h

pio run -e release

OUT=release_out
rm -rf "$OUT" && mkdir -p "$OUT"
cp .pio/build/release/firmware.bin "$OUT/firmware.bin"

if [[ -f include/secrets.h ]]; then
  python3 - "$OUT/firmware.bin" << 'PY'
import re, sys
data = open(sys.argv[1], 'rb').read()
text = open('include/secrets.h').read()
leaked = [m.group(1) for m in re.finditer(r'#define\s+\w+\s+"((?:[^"\\]|\\.)*)"', text)
          if len(m.group(1)) >= 4 and m.group(1).encode() in data]
if leaked:
    print('refusing to publish: the firmware contains %d value(s) from include/secrets.h' % len(leaked))
    sys.exit(1)
PY
fi

read -r SIZE SHA < <(python3 -c "import hashlib,sys; d=open(sys.argv[1],'rb').read(); print(len(d), hashlib.sha256(d).hexdigest())" "$OUT/firmware.bin")
printf 'version=%s\nsize=%s\nsha256=%s\n' "$VERSION" "$SIZE" "$SHA" > "$OUT/manifest.txt"
openssl dgst -sha256 -sign "$KEY" -out "$OUT/manifest.sig" "$OUT/manifest.txt"
openssl dgst -sha256 -verify "$PUB" -signature "$OUT/manifest.sig" "$OUT/manifest.txt"

echo "---- manifest ----"
cat "$OUT/manifest.txt"
read -r -p "Commit, tag v$VERSION, push and publish? [y/N] " answer
[[ "$answer" == "y" ]] || { git checkout src/version.h; echo "aborted"; exit 1; }

git add src/version.h
git commit -m "Release v$VERSION"
git tag "v$VERSION"
git push origin HEAD "v$VERSION"
gh release create "v$VERSION" "$OUT/firmware.bin" "$OUT/manifest.txt" "$OUT/manifest.sig" \
  --title "v$VERSION" --notes "Firmware v$VERSION. See CHANGELOG.md."
echo "Published v$VERSION"
