#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C
export LANG=C

MIGA80_PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$MIGA80_PROJECT_ROOT"

if command -v gmake >/dev/null 2>&1; then
  MIGA80_MAKE="$(command -v gmake)"
elif command -v make >/dev/null 2>&1 &&
    make --version 2>/dev/null | /usr/bin/grep -q '^GNU Make'; then
  MIGA80_MAKE="$(command -v make)"
else
  printf 'GNU Make is required (install gmake on macOS).\n' >&2
  exit 1
fi

MIGA80_VERSION="$(/bin/cat "$MIGA80_PROJECT_ROOT/VERSION")"
case "$MIGA80_VERSION" in
  ''|*[!0-9A-Za-z.-]*)
    printf 'Invalid MIGA-80 version: %s\n' "$MIGA80_VERSION" >&2
    exit 1
    ;;
esac

printf 'Rebuilding MIGA-80 %s and its bootable ADF...\n' "$MIGA80_VERSION"
"$MIGA80_MAKE" --always-make release

MIGA80_ADF="$MIGA80_PROJECT_ROOT/release/miga80-$MIGA80_VERSION.adf"
MIGA80_MANIFEST="$MIGA80_PROJECT_ROOT/release/miga80-$MIGA80_VERSION.manifest.json"

if [ ! -f "$MIGA80_ADF" ] || [ ! -f "$MIGA80_MANIFEST" ]; then
  printf 'Build completed without the expected release files.\n' >&2
  exit 1
fi

printf 'Bootable ADF: %s\n' "$MIGA80_ADF"
printf 'Manifest:     %s\n' "$MIGA80_MANIFEST"
