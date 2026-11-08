#!/usr/bin/env bash
# Installs the prebuilt btoon CLI tools (btoon, btoon-convert, btoon-schema)
# from a GitHub release of BTOON-project/btoon-core.
#
#   curl -fsSL https://raw.githubusercontent.com/BTOON-project/btoon-core/main/scripts/install.sh | bash
#   VERSION=0.1.3 PREFIX=$HOME/.local bash install.sh
set -euo pipefail

VERSION="${VERSION:-0.1.3}"
PREFIX="${PREFIX:-/usr/local}"
REPO="BTOON-project/btoon-core"

case "$(uname -s)" in
  Linux)
    case "$(uname -m)" in
      x86_64|amd64) TARGET=linux-x64 ;;
      aarch64|arm64) TARGET=linux-arm64 ;;
      *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
    esac ;;
  Darwin) TARGET=macos-universal ;;
  *) echo "unsupported OS: $(uname -s) (on Windows, download the .zip from the releases page)" >&2; exit 1 ;;
esac

NAME="btoon-v${VERSION}-${TARGET}"
URL="https://github.com/${REPO}/releases/download/v${VERSION}/${NAME}.tar.gz"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $URL"
curl -fsSL "$URL" -o "$TMP/btoon.tar.gz"
if curl -fsSL "https://github.com/${REPO}/releases/download/v${VERSION}/SHA256SUMS" -o "$TMP/SHA256SUMS"; then
  expected="$(grep -F "${NAME}.tar.gz" "$TMP/SHA256SUMS" | awk '{print $1}')"
  if command -v sha256sum >/dev/null; then actual="$(sha256sum "$TMP/btoon.tar.gz" | awk '{print $1}')";
  else actual="$(shasum -a 256 "$TMP/btoon.tar.gz" | awk '{print $1}')"; fi
  if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then echo "checksum mismatch for $NAME" >&2; exit 1; fi
  echo "Checksum OK"
fi
tar -xzf "$TMP/btoon.tar.gz" -C "$TMP"

SUDO=""
if [ ! -w "$PREFIX/bin" ] 2>/dev/null || { [ ! -d "$PREFIX/bin" ] && [ ! -w "$PREFIX" ]; }; then SUDO="sudo"; fi
$SUDO mkdir -p "$PREFIX/bin"
for tool in btoon btoon-convert btoon-schema; do
  $SUDO install -m 0755 "$TMP/$NAME/$tool" "$PREFIX/bin/$tool"
done
echo "Installed to $PREFIX/bin: $("$PREFIX/bin/btoon" --version)"
