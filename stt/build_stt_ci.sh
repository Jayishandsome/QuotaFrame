#!/bin/sh
# Builds the speech-to-text helper with Apple's Swift compiler, then asks macOS
# to prepare the language model (first run downloads it; may ask permission).
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root/stt"
if ! command -v swiftc >/dev/null 2>&1; then
  echo "Apple's command line tools are missing. Run: xcode-select --install"
  exit 1
fi
locale=$(sed -n 's/^STT_LOCALE=//p' "$root/config/backend.env" 2>/dev/null | tail -1)
locale=${locale:-zh-HK}
mkdir -p build
set -- -O -parse-as-library -swift-version 5 -target "$(uname -m)-apple-macos13.0" \
  Sources/main.swift -o build/butler-stt \
  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker Info.plist
if swiftc "$@" 2>build/compile.log; then
  echo "Built with Apple SpeechAnalyzer (on-device) and the older recogniser as backup."
else
  echo "This Mac's SDK has no SpeechAnalyzer (needs macOS 26 tools); using the older Apple recogniser."
  swiftc "$@" -D LEGACY_ONLY
fi
codesign --force --sign - build/butler-stt >/dev/null 2>&1 || true
echo "Preparing speech recognition for $locale (first time may download a model)..."
./build/butler-stt --prepare --locale "$locale"
