#!/bin/sh
# Build the demo and wrap it as PfsynthDemo.app (SwiftPM has no app-bundle step).
set -e
cd "$(dirname "$0")/PfsynthDemo"
LOG=$(mktemp); swift build -c release >"$LOG" 2>&1; STATUS=$?
grep -E "error:|Build complete" "$LOG" | grep -v "^\[" || true; rm -f "$LOG"
[ $STATUS -eq 0 ] || { echo "build failed"; exit 1; }
BIN=.build/release/PfsynthDemo; [ -x "$BIN" ] || { echo "build failed"; exit 1; }
APP=../PfsynthDemo.app; rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/PfsynthDemo"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>PfsynthDemo</string>
<key>CFBundleIdentifier</key><string>com.olaughlin.pfsynth.demo</string>
<key>CFBundleName</key><string>pfsynth demo</string>
<key>CFBundleDisplayName</key><string>pfsynth demo</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSMinimumSystemVersion</key><string>14.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
PLIST
codesign --force --sign - "$APP" >/dev/null 2>&1 || true
echo "built $APP"
