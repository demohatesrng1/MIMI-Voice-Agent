#!/usr/bin/env bash
# Fetch VOICEVOX CORE: the offline neural TTS engine Mimi speaks with, plus its
# ONNX Runtime, the Open JTalk dictionary and the voice models (including
# 冥鳴ひまり, style 14, in models/vvms/1.vvm). This is what lets Mimi keep her
# voice with the VOICEVOX app closed.
#
# Idempotent: the downloader only pulls what's missing. The tree is gitignored.
#
# Note: the VOICEVOX voice-model and ONNX Runtime terms of use must be accepted
# (free for commercial and non-commercial use; a "VOICEVOX" credit is required
# wherever the generated voice is used). Pass -y to accept them non-interactively.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${MIMI_VOICEVOX_DIR:-$REPO_ROOT/voicevox_core}"
VERSION="${VOICEVOX_CORE_VERSION:-0.16.4}"

ARCH="$(uname -m)"
case "$ARCH" in
  arm64) ASSET="download-osx-arm64" ;;
  x86_64) ASSET="download-osx-x64" ;;
  *) echo "unsupported arch: $ARCH" >&2; exit 1 ;;
esac

ACCEPT=""
[[ "${1:-}" == "-y" || "${MIMI_ACCEPT_VOICEVOX_TERMS:-}" == "1" ]] && ACCEPT="1"

DL="$(mktemp -t vv-download)"
trap 'rm -f "$DL"' EXIT
echo "fetching downloader ($ASSET, core $VERSION)"
curl -fsSL "https://github.com/VOICEVOX/voicevox_core/releases/download/$VERSION/$ASSET" -o "$DL"
chmod +x "$DL"
xattr -d com.apple.quarantine "$DL" 2>/dev/null || true

echo "downloading engine + models -> $OUT"
if [[ -n "$ACCEPT" ]]; then
  yes y | "$DL" -o "$OUT"
else
  "$DL" -o "$OUT"
fi

# The prebuilt core dylib carries the CI machine's absolute path as its install
# name; rewrite it to @rpath so the linked binaries can find it. Idempotent, and
# CMake does the same at configure time as a safety net.
LIB="$OUT/c_api/lib/libvoicevox_core.dylib"
if [[ -f "$LIB" ]]; then
  install_name_tool -id "@rpath/libvoicevox_core.dylib" "$LIB" 2>/dev/null || true
fi

echo
echo "done. 冥鳴ひまり (style 14) is in $OUT/models/vvms/1.vvm"
echo "Reconfigure CMake so the build picks it up: cmake -S . -B build"
