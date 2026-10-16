#!/usr/bin/env bash
set -euo pipefail
dir="${1:?usage: gen_fixtures.sh DIR}"
rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

openssl ecparam -name prime256v1 -genkey -noout -out priv.pem 2>/dev/null
openssl ec -in priv.pem -pubout -out pub.pem 2>/dev/null
openssl ecparam -name prime256v1 -genkey -noout -out priv_other.pem 2>/dev/null
openssl ec -in priv_other.pem -pubout -out pub_other.pem 2>/dev/null
head -c 600000 /dev/urandom > firmware.bin

make_manifest() {
  python3 - "$1" "$2" << 'PY'
import hashlib, sys
version, prefix = sys.argv[1], sys.argv[2]
data = open('firmware.bin', 'rb').read()
open(prefix + '.txt', 'w').write('version=%s\nsize=%d\nsha256=%s\n' % (version, len(data), hashlib.sha256(data).hexdigest()))
PY
  openssl dgst -sha256 -sign priv.pem -out "$2.sig" "$2.txt"
}

make_manifest 1.0.1 manifest
make_manifest 1.0.0 manifest_same
make_manifest 0.9.0 manifest_old
