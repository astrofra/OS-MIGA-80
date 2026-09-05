#!/usr/bin/env -S LC_ALL=C LANG=C bash

set -euo pipefail

if [ "$#" -ne 7 ]; then
  printf 'Usage: %s program source font startup readme license output.adf\n' "$0" >&2
  exit 1
fi

MIGA80_PROGRAM="$1"
MIGA80_SOURCE="$2"
MIGA80_FONT="$3"
MIGA80_STARTUP="$4"
MIGA80_README="$5"
MIGA80_LICENSE="$6"
MIGA80_ADF="$7"
MIGA80_MANIFEST="${MIGA80_ADF%.adf}.manifest.txt"
MIGA80_OUTPUT_DIR="$(dirname "$MIGA80_ADF")"

for command in xdftool xdfscan; do
  if ! command -v "$command" >/dev/null 2>&1; then
    printf 'Required command not found: %s\n' "$command" >&2
    exit 1
  fi
done

for input in \
  "$MIGA80_PROGRAM" "$MIGA80_SOURCE" "$MIGA80_FONT" \
  "$MIGA80_STARTUP" "$MIGA80_README" "$MIGA80_LICENSE"; do
  if [ ! -f "$input" ]; then
    printf 'Required ADF input not found: %s\n' "$input" >&2
    exit 1
  fi
done

/bin/mkdir -p "$MIGA80_OUTPUT_DIR"

xdftool -f "$MIGA80_ADF" \
  create \
  + format MIGA80 ofs \
  + makedir S \
  + makedir DATA \
  + write "$MIGA80_STARTUP" S/Startup-Sequence \
  + write "$MIGA80_PROGRAM" MIGA80 \
  + write "$MIGA80_SOURCE" DATA/DEFAULT.LUA \
  + write "$MIGA80_FONT" DATA/FONT4X8.BIN \
  + write "$MIGA80_README" README.TXT \
  + write "$MIGA80_LICENSE" LICENSE.TXT \
  + boot install >/dev/null

xdfscan "$MIGA80_ADF" >/dev/null

MIGA80_ADF_SHA256="$(/usr/bin/shasum -a 256 "$MIGA80_ADF" | /usr/bin/awk '{print $1}')"
MIGA80_PROGRAM_SHA256="$(/usr/bin/shasum -a 256 "$MIGA80_PROGRAM" | /usr/bin/awk '{print $1}')"
MIGA80_SOURCE_SHA256="$(/usr/bin/shasum -a 256 "$MIGA80_SOURCE" | /usr/bin/awk '{print $1}')"
MIGA80_FONT_SHA256="$(/usr/bin/shasum -a 256 "$MIGA80_FONT" | /usr/bin/awk '{print $1}')"

{
  printf 'miga80_source_view_adf_manifest=1\n'
  printf 'volume=MIGA80\n'
  printf 'filesystem=ofs\n'
  printf 'adf_bytes=%s\n' "$(/usr/bin/stat -f '%z' "$MIGA80_ADF")"
  printf 'adf_sha256=%s\n' "$MIGA80_ADF_SHA256"
  printf 'program_sha256=%s\n' "$MIGA80_PROGRAM_SHA256"
  printf 'source_sha256=%s\n' "$MIGA80_SOURCE_SHA256"
  printf 'font_sha256=%s\n' "$MIGA80_FONT_SHA256"
  printf 'source_path=DATA/DEFAULT.LUA\n'
  printf 'font_path=DATA/FONT4X8.BIN\n'
  printf 'boot_report_path=BOOTED.TXT\n'
  printf '\nfilesystem_listing:\n'
  xdftool "$MIGA80_ADF" list
} >"$MIGA80_MANIFEST"

printf 'Built MIGA-80 source-view ADF: %s\n' "$MIGA80_ADF"
printf 'Wrote manifest: %s\n' "$MIGA80_MANIFEST"
