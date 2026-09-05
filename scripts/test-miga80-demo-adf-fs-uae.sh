#!/usr/bin/env -S LC_ALL=C LANG=C bash

set -euo pipefail

MIGA80_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIGA80_LOCAL_CONFIG="${MIGA80_LOCAL_CONFIG:-$MIGA80_PROJECT_ROOT/config/fs-uae/local.env}"
MIGA80_SOURCE_ADF="${1:-$MIGA80_PROJECT_ROOT/build/distribution/miga80-source-view.adf}"
MIGA80_EXPECTED="${2:-$MIGA80_PROJECT_ROOT/tests/smoke/source-view-adf/expected.txt}"
MIGA80_MODE="${3:-SOURCE}"
MIGA80_RUN_DIR="$MIGA80_PROJECT_ROOT/build/fs-uae-demo-adf"
MIGA80_RUN_ADF="$MIGA80_RUN_DIR/miga80-source-view-run.adf"
MIGA80_SNAPSHOT_ADF="$MIGA80_RUN_DIR/poll-snapshot.adf"
MIGA80_CONFIG="$MIGA80_RUN_DIR/a1200-pal-source-view.fs-uae"
MIGA80_REPORT="$MIGA80_PROJECT_ROOT/build/reports/source-view-adf-fs-uae.txt"
MIGA80_CANDIDATE_REPORT="$MIGA80_RUN_DIR/candidate-report.txt"
MIGA80_AUTORUN_STARTUP="$MIGA80_RUN_DIR/Startup-Sequence.autorun"
MIGA80_TIMEOUT_SECONDS="${MIGA80_FS_UAE_TIMEOUT_SECONDS:-45}"
MIGA80_EMULATOR_PID=""

stop_emulator() {
  local attempt

  if [ -z "$MIGA80_EMULATOR_PID" ] ||
     ! /bin/kill -0 "$MIGA80_EMULATOR_PID" 2>/dev/null; then
    return
  fi
  /bin/kill -INT "$MIGA80_EMULATOR_PID" 2>/dev/null || true
  for attempt in {1..20}; do
    if ! /bin/kill -0 "$MIGA80_EMULATOR_PID" 2>/dev/null; then
      wait "$MIGA80_EMULATOR_PID" 2>/dev/null || true
      return
    fi
    /bin/sleep 0.25
  done
  /bin/kill -TERM "$MIGA80_EMULATOR_PID" 2>/dev/null || true
  wait "$MIGA80_EMULATOR_PID" 2>/dev/null || true
}

cleanup() {
  stop_emulator
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for command in fs-uae xdftool xdfscan; do
  if ! command -v "$command" >/dev/null 2>&1; then
    printf 'Required command not found: %s\n' "$command" >&2
    exit 1
  fi
done

if [ ! -f "$MIGA80_LOCAL_CONFIG" ]; then
  printf 'Local FS-UAE configuration not found: %s\n' "$MIGA80_LOCAL_CONFIG" >&2
  exit 1
fi
if [ ! -f "$MIGA80_SOURCE_ADF" ]; then
  printf 'MIGA-80 demo ADF not found: %s\n' "$MIGA80_SOURCE_ADF" >&2
  exit 1
fi
if [ ! -f "$MIGA80_EXPECTED" ]; then
  printf 'Expected boot report not found: %s\n' "$MIGA80_EXPECTED" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$MIGA80_LOCAL_CONFIG"
MIGA80_KICKSTART="${MIGA80_KICKSTART_30:-${MIGA80_KICKSTART_31:-}}"
if [ -z "$MIGA80_KICKSTART" ] || [ ! -f "$MIGA80_KICKSTART" ]; then
  printf 'Configure a licensed A1200 Kickstart 3.0 or 3.1 ROM.\n' >&2
  exit 1
fi

case "$MIGA80_TIMEOUT_SECONDS" in
  ''|*[!0-9]*)
    printf 'MIGA80_FS_UAE_TIMEOUT_SECONDS must be a positive integer.\n' >&2
    exit 1
    ;;
esac
if [ "$MIGA80_TIMEOUT_SECONDS" -eq 0 ]; then
  printf 'MIGA80_FS_UAE_TIMEOUT_SECONDS must be greater than zero.\n' >&2
  exit 1
fi

/bin/mkdir -p "$MIGA80_RUN_DIR" "$(dirname "$MIGA80_REPORT")"
/bin/cp "$MIGA80_SOURCE_ADF" "$MIGA80_RUN_ADF"
/bin/rm -f "$MIGA80_REPORT" "$MIGA80_CANDIDATE_REPORT" \
  "$MIGA80_SNAPSHOT_ADF"

case "$MIGA80_MODE" in
  SOURCE)
    ;;
  AUTORUN)
    printf '%s\n' \
      'MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA MIGA80:BOOTED.TXT AUTORUN' \
      >"$MIGA80_AUTORUN_STARTUP"
    xdftool -f "$MIGA80_RUN_ADF" \
      delete S/Startup-Sequence + \
      write "$MIGA80_AUTORUN_STARTUP" S/Startup-Sequence >/dev/null
    ;;
  *)
    printf 'Unknown source-view test mode: %s\n' "$MIGA80_MODE" >&2
    exit 1
    ;;
esac

{
  printf '[fs-uae]\n'
  printf 'amiga_model = A1200\n'
  printf 'chip_memory = 2048\n'
  printf 'fast_memory = 0\n'
  printf 'ntsc_mode = 0\n'
  printf 'joystick_port_1 = none\n'
  printf 'automatic_input_grab = 0\n'
  printf 'kickstart_file = %s\n' "$MIGA80_KICKSTART"
  printf 'floppy_drive_0 = %s\n' "$MIGA80_RUN_ADF"
  printf 'writable_floppy_images = 1\n'
} >"$MIGA80_CONFIG"

fs-uae "$MIGA80_CONFIG" >"$MIGA80_RUN_DIR/fs-uae-output.txt" 2>&1 &
MIGA80_EMULATOR_PID="$!"

for ((second = 0; second < MIGA80_TIMEOUT_SECONDS; ++second)); do
  if ! /bin/kill -0 "$MIGA80_EMULATOR_PID" 2>/dev/null; then
    break
  fi
  /bin/sleep 1
  /bin/cp "$MIGA80_RUN_ADF" "$MIGA80_SNAPSHOT_ADF"
  if xdftool "$MIGA80_SNAPSHOT_ADF" type BOOTED.TXT \
       >"$MIGA80_CANDIDATE_REPORT" 2>/dev/null; then
    if [ "$MIGA80_MODE" = AUTORUN ]; then
      if /usr/bin/grep -q '^miga80_source_view_report=2$' \
           "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" |
           /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" |
         /usr/bin/grep -Eq '^result=(pass|fail)$'; then
      break
    fi
  fi
done

stop_emulator
MIGA80_EMULATOR_PID=""
xdfscan "$MIGA80_RUN_ADF" >/dev/null

if ! xdftool "$MIGA80_RUN_ADF" read BOOTED.TXT "$MIGA80_REPORT" \
     >/dev/null 2>&1; then
  printf 'The booted ADF did not contain BOOTED.TXT after %s seconds.\n' \
    "$MIGA80_TIMEOUT_SECONDS" >&2
  printf 'FS-UAE log: %s\n' "$MIGA80_RUN_DIR/fs-uae-output.txt" >&2
  exit 1
fi

if ! /usr/bin/diff -u "$MIGA80_EXPECTED" "$MIGA80_REPORT"; then
  printf 'The source-view ADF report did not match the expected result.\n' >&2
  exit 1
fi

printf 'PASS  standalone MIGA-80 ADF booted and opened the source view\n'
printf 'PASS  AGA readback matches the canonical 4x8 source framebuffer\n'
if [ "$MIGA80_MODE" = AUTORUN ]; then
  printf 'PASS  on-target source compilation and native execution completed\n'
fi
