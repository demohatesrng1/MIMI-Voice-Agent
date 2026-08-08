#!/usr/bin/env bash
# Bundle the avatar's JavaScript into one self-contained file.
#
# The app ships offline and its Qt WebEngine page is served from a custom
# scheme with no network access at all, so three.js and three-vrm cannot come
# from a CDN and an import map would still leave a dozen files to serve. esbuild
# flattens the lot into src/ui/web/avatar.bundle.js, which is what index.html
# loads and what CMake compiles into the binary as a Qt resource.
#
# The bundle is committed, so this only needs running when avatar.js changes or
# a dependency is bumped. Requires node/npm; nothing else in the build does.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB="$ROOT/src/ui/web"
WORK="${TMPDIR:-/tmp}/mimi-avatar-build"

THREE_VERSION="0.170.0"
VRM_VERSION="3.5.0"

command -v npm >/dev/null || { echo "npm is required to rebuild the avatar bundle" >&2; exit 1; }

mkdir -p "$WORK"
cd "$WORK"
[[ -f package.json ]] || npm init -y >/dev/null
echo "  installing three@$THREE_VERSION, @pixiv/three-vrm@$VRM_VERSION, esbuild"
npm install --silent --no-audit --no-fund \
  "three@$THREE_VERSION" "@pixiv/three-vrm@$VRM_VERSION" esbuild >/dev/null

# esbuild resolves bare imports from the *entry file's* directory upward, so the
# source is copied next to the installed node_modules rather than bundled in
# place -- otherwise it walks up from src/ui/web and finds nothing.
echo "  bundling $WEB/avatar.js"
cp "$WEB/avatar.js" "$WORK/avatar.entry.js"
./node_modules/.bin/esbuild "$WORK/avatar.entry.js" \
  --bundle \
  --format=esm \
  --target=safari16 \
  --minify \
  --legal-comments=none \
  --outfile="$WEB/avatar.bundle.js"

printf '  wrote %s (%s)\n' "src/ui/web/avatar.bundle.js" \
  "$(du -h "$WEB/avatar.bundle.js" | cut -f1)"
