#!/bin/bash
set -euo pipefail
set +x

python3 - <<'PY'
import os
import sys
required = (
    "APPLE_DEVELOPER_ID_CERTIFICATE", "APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD",
    "KEYCHAIN_PASSWORD", "CODESIGN_APPLE_ID", "NOTARYTOOL_APPLE_ID",
    "NOTARYTOOL_TEAM_ID", "NOTARYTOOL_PASSWORD",
)
missing = [name for name in required if not os.environ.get(name)]
for name in missing:
    print(f"::error::Signed distribution requires secret {name}")
sys.exit(bool(missing))
PY

root="$(pwd)"
work="$root/.build-native/signing"
mkdir -p "$work"
keychain="$work/distribution.keychain-db"
certificate="$work/certificate.p12"
old_keychains=()
while IFS= read -r item; do
  old_keychains+=("$item")
done < <(security list-keychains -d user | sed 's/^[[:space:]]*"//;s/"[[:space:]]*$//')
cleanup() {
  security list-keychains -d user -s "${old_keychains[@]}" || true
  security delete-keychain "$keychain" || true
  rm -f "$certificate" "$keychain" "$keychain-shm" "$keychain-wal"
}
trap cleanup EXIT
printf '%s' "$APPLE_DEVELOPER_ID_CERTIFICATE" | base64 --decode > "$certificate"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$keychain"
security import "$certificate" -P "$APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD" \
  -T /usr/bin/codesign -t cert -f pkcs12 -k "$keychain"
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
  -k "$KEYCHAIN_PASSWORD" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" "${old_keychains[@]}"
security find-identity -v -p codesigning "$keychain" | grep -F -- "$CODESIGN_APPLE_ID"

binary="$root/.build-native/dist/bin/ct2-server"
codesign --force --options runtime --timestamp --keychain "$keychain" \
  --sign "$CODESIGN_APPLE_ID" "$binary"
codesign --verify --strict --verbose=2 "$binary"
codesign -dvv "$binary" 2>&1 | grep -F "TeamIdentifier=$NOTARYTOOL_TEAM_ID"
mkdir -p "$root/.build-native/packages"
dmg="$root/.build-native/packages/ct2-server-macos-$BUILD_ARCH-signed.dmg"
hdiutil create -fs HFS+ -volname ct2-server -srcfolder "$root/.build-native/dist" "$dmg"
codesign --timestamp --keychain "$keychain" --sign "$CODESIGN_APPLE_ID" "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
xcrun notarytool submit "$dmg" --apple-id "$NOTARYTOOL_APPLE_ID" \
  --team-id "$NOTARYTOOL_TEAM_ID" --password "$NOTARYTOOL_PASSWORD" \
  --wait --timeout 30m --output-format json > "$work/notary-result.json"
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); print(r); sys.exit(0 if r.get("status") == "Accepted" else 1)' "$work/notary-result.json"
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
spctl --assess --type execute --verbose=2 "$binary"
(cd "$(dirname "$dmg")" && shasum -a 256 "$(basename "$dmg")" > "$(basename "$dmg").sha256")
