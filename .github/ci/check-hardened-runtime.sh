#!/usr/bin/env bash
set -euo pipefail

configuration="${1:?pass ordinary or protected}"
case "$(uname -m)" in
    arm64) entitlement=com.apple.security.cs.allow-jit ;;
    x86_64) entitlement=com.apple.security.cs.allow-unsigned-executable-memory ;;
    *) echo "Unsupported native macOS architecture" >&2; exit 1 ;;
esac

evidence=build/hardened-runtime
mkdir -p "$evidence"
cat > "$evidence/entitlements.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>$entitlement</key><true/></dict></plist>
EOF

binaries=(consumer-build/mexce_ordinary_consumer)
if test "$configuration" = protected; then
    binaries+=(consumer-build/mexce_protected_consumer)
fi
if test "$(uname -m)" = arm64; then
    binaries+=(build/apple_jit_arena_tests)
fi

for binary in "${binaries[@]}"; do
    name="$(basename "$binary")"
    file "$binary" | tee "$evidence/$name.binary.txt"
    otool -L "$binary" | tee "$evidence/$name.dependencies.txt"
    test "$(lipo -archs "$binary")" = "$(uname -m)"
    codesign --force --sign - --options runtime --timestamp=none \
        --entitlements "$evidence/entitlements.plist" "$binary"
    codesign --verify --strict --verbose=2 "$binary"
    codesign --display --verbose=4 "$binary" 2>&1 | tee "$evidence/$name.signature.txt"
    grep -q 'flags=.*runtime' "$evidence/$name.signature.txt"
    codesign --display --entitlements - --xml "$binary" > "$evidence/$name.entitlements.plist"
    python - "$evidence/$name.entitlements.plist" "$entitlement" <<'PY'
import plistlib
import sys

with open(sys.argv[1], "rb") as source:
    assert plistlib.load(source) == {sys.argv[2]: True}
PY
    "$binary" 2>&1 | tee "$evidence/$name.run.log"
done
