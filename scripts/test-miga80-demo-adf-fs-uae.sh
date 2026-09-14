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
MIGA80_TEST_STARTUP="$MIGA80_RUN_DIR/Startup-Sequence.test"
MIGA80_TIMEOUT_SECONDS="${MIGA80_FS_UAE_TIMEOUT_SECONDS:-45}"
MIGA80_FAST_MEMORY="${MIGA80_FS_UAE_FAST_MEMORY:-0}"
MIGA80_C2P_BACKEND="${MIGA80_C2P_BACKEND:-DEFAULT}"
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
MIGA80_KICKSTART="${MIGA80_FS_UAE_KICKSTART:-\
${MIGA80_KICKSTART_30:-${MIGA80_KICKSTART_31:-}}}"
if [ -z "$MIGA80_KICKSTART" ] ||
   { [ "$MIGA80_KICKSTART" != AROS ] &&
     [ ! -f "$MIGA80_KICKSTART" ]; }; then
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
case "$MIGA80_FAST_MEMORY" in
  ''|*[!0-9]*)
    printf 'MIGA80_FS_UAE_FAST_MEMORY must be a non-negative integer.\n' >&2
    exit 1
    ;;
esac
MIGA80_C2P_ARGUMENT="C2P=$MIGA80_C2P_BACKEND"
case "$MIGA80_C2P_BACKEND" in
  DEFAULT) MIGA80_C2P_REPORT_NAME=default; MIGA80_C2P_ARGUMENT="" ;;
  MASK32) MIGA80_C2P_REPORT_NAME=mask32 ;;
  KALMS) MIGA80_C2P_REPORT_NAME=kalms ;;
  REFERENCE) MIGA80_C2P_REPORT_NAME=reference ;;
  *) printf 'Unknown C2P backend: %s\n' "$MIGA80_C2P_BACKEND" >&2; exit 1 ;;
esac
MIGA80_C2P_EXPECTED="$MIGA80_C2P_REPORT_NAME"
if [ "$MIGA80_C2P_BACKEND" = DEFAULT ]; then MIGA80_C2P_EXPECTED=kalms; fi

/bin/mkdir -p "$MIGA80_RUN_DIR" "$(dirname "$MIGA80_REPORT")"
/bin/cp "$MIGA80_SOURCE_ADF" "$MIGA80_RUN_ADF"
/bin/rm -f "$MIGA80_REPORT" "$MIGA80_CANDIDATE_REPORT" \
  "$MIGA80_SNAPSHOT_ADF"

case "$MIGA80_MODE" in
  SOURCE)
    printf '%s\n' \
      'MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA MIGA80:BOOTED.TXT' \
      >"$MIGA80_TEST_STARTUP"
    ;;
  INTROTEST)
    printf '%s\n' \
      'SYS:MIGA80 SYS:demos/default.lua SYS:BOOTED.TXT INTROTEST' \
      >"$MIGA80_TEST_STARTUP"
    ;;
  BROWSERTEST)
    printf '%s\n' \
      'SYS:MIGA80 SYS:demos/default.lua SYS:BOOTED.TXT BROWSERTEST' \
      >"$MIGA80_TEST_STARTUP"
    python3 "$MIGA80_PROJECT_ROOT/scripts/prepare-browser-test.py" "$MIGA80_RUN_ADF"
    ;;
  CUBETEST|CUBEPIXELTEST)
    printf '%s\n' \
      "MIGA80:MIGA80 MIGA80:DATA/CUBE.LUA MIGA80:BOOTED.TXT $MIGA80_MODE $MIGA80_C2P_ARGUMENT" \
      >"$MIGA80_TEST_STARTUP"
    ;;
  AUTORUN|SELFTEST|STOPTEST|GRAPHICSTEST)
    printf '%s\n' \
      "MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA MIGA80:BOOTED.TXT $MIGA80_MODE $MIGA80_C2P_ARGUMENT" \
      >"$MIGA80_TEST_STARTUP"
    ;;
  GRAPHICSTEST_DIRECT)
    printf '%s\n' \
      'MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA MIGA80:BOOTED.TXT GRAPHICSTEST NOSUPERVISOR' \
      >"$MIGA80_TEST_STARTUP"
    ;;
  AUTORUN_DIRECT)
    printf '%s\n' \
      'MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA MIGA80:BOOTED.TXT AUTORUN NOSUPERVISOR' \
      >"$MIGA80_TEST_STARTUP"
    ;;
  *)
    printf 'Unknown source-view test mode: %s\n' "$MIGA80_MODE" >&2
    exit 1
    ;;
esac
xdftool -f "$MIGA80_RUN_ADF" \
  delete S/Startup-Sequence + \
  write "$MIGA80_TEST_STARTUP" S/Startup-Sequence >/dev/null

{
  printf '[fs-uae]\n'
  printf 'amiga_model = A1200\n'
  printf 'accuracy = 1\n'
  printf 'chip_memory = 2048\n'
  printf 'fast_memory = %s\n' "$MIGA80_FAST_MEMORY"
  printf 'ntsc_mode = 0\n'
  printf 'joystick_port_1 = none\n'
  printf 'automatic_input_grab = 0\n'
  if [ "$MIGA80_KICKSTART" != AROS ]; then
    printf 'kickstart_file = %s\n' "$MIGA80_KICKSTART"
  fi
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
    if [ "$MIGA80_MODE" = GRAPHICSTEST ] || [ "$MIGA80_MODE" = GRAPHICSTEST_DIRECT ]; then
      if /usr/bin/grep -q '^miga80_graphics_report=1$' \
           "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" |
           /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = INTROTEST ]; then
      if /usr/bin/grep -q '^miga80_intro_report=1$' "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" | /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = BROWSERTEST ]; then
      if /usr/bin/grep -q '^miga80_browser_report=1$' "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" | /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = CUBETEST ] || [ "$MIGA80_MODE" = CUBEPIXELTEST ]; then
      if /usr/bin/grep -q '^miga80_cube_report=1$' "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" | /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = STOPTEST ]; then
      if /usr/bin/grep -q '^miga80_stop_report=1$' \
           "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" |
           /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = SELFTEST ]; then
      if /usr/bin/grep -q '^miga80_workflow_report=1$' \
           "$MIGA80_CANDIDATE_REPORT" &&
         /usr/bin/tail -n 1 "$MIGA80_CANDIDATE_REPORT" |
           /usr/bin/grep -Eq '^result=(pass|fail)$'; then
        break
      fi
    elif [ "$MIGA80_MODE" = AUTORUN ] || [ "$MIGA80_MODE" = AUTORUN_DIRECT ]; then
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

if [ "$MIGA80_MODE" = CUBETEST ] || [ "$MIGA80_MODE" = CUBEPIXELTEST ]; then
  if [ "$MIGA80_MODE" = CUBEPIXELTEST ]; then
    /bin/cp "$MIGA80_REPORT" "$MIGA80_PROJECT_ROOT/build/reports/cube-chunky-fs-uae.txt"
    /bin/cp "$MIGA80_REPORT" "$MIGA80_PROJECT_ROOT/build/reports/cube-chunky-$MIGA80_C2P_REPORT_NAME-fs-uae.txt"
  else
    /bin/cp "$MIGA80_REPORT" "$MIGA80_PROJECT_ROOT/build/reports/cube-fs-uae.txt"
  fi
  python3 - "$MIGA80_REPORT" "$MIGA80_C2P_EXPECTED" "$MIGA80_MODE" "$MIGA80_SOURCE_ADF" "$MIGA80_FAST_MEMORY" "$MIGA80_C2P_REPORT_NAME" <<'PY_CHECK'
import hashlib, json, pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
values = dict(line.split('=', 1) for line in s.splitlines())
assert int(values.get('frames', '0')) >= 2, s
assert 10 * 65536 <= int(values.get('elapsed_q16', '0')) < 11 * 65536, s
if sys.argv[3] == 'CUBEPIXELTEST':
    assert values['c2p_backend'] == sys.argv[2].lower(), s
    for sample in range(2):
        v = {k.removeprefix(f'sample{sample}_'): int(n) for k, n in values.items()
             if k.startswith(f'sample{sample}_')}
        assert v['frames'] == v['c2p_calls'] >= 2, s
        assert 10 * 65536 <= v['elapsed_q16'] < 11 * 65536, s
        assert v['eclock_hz'] > 0, s
        assert 0 < v['c2p_min_ticks'] <= v['c2p_max_ticks'], s
        assert v['c2p_min_ticks'] * v['c2p_calls'] <= v['c2p_ticks'] <= v['c2p_max_ticks'] * v['c2p_calls'], s
        print(f"{values['c2p_backend']} sample {sample}: {v['frames']} frames, "
              f"{v['elapsed_q16']/65536:.4f} s, C2P mean "
              f"{1000*v['c2p_ticks']/v['c2p_calls']/v['eclock_hz']:.3f} ms")
    p.with_name(f'cube-chunky-{sys.argv[6]}-fs-uae.json').write_text(json.dumps({
        'adf_sha256': hashlib.sha256(pathlib.Path(sys.argv[4]).read_bytes()).hexdigest(),
        'fast_kib': int(sys.argv[5]), 'chip_kib': 2048, 'accuracy': 1, 'model': 'A1200 PAL'
    }, indent=2) + '\n')
p.write_text(''.join(line + '\n' for line in s.splitlines()
                     if not line.startswith(('frames=', 'elapsed_q16=', 'c2p_backend=', 'sample0_', 'sample1_'))))
PY_CHECK
fi
if ! /usr/bin/diff -u "$MIGA80_EXPECTED" "$MIGA80_REPORT"; then
  printf 'The source-view ADF report did not match the expected result.\n' >&2
  exit 1
fi

printf 'PASS  standalone MIGA-80 ADF booted and opened the source view\n'
printf 'PASS  AGA readback matches the canonical 4x8 source framebuffer\n'
if [ "$MIGA80_MODE" = AUTORUN ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/source-view-adf-autorun-fs-uae.txt"
  printf 'PASS  on-target source compilation and native execution completed\n'
fi

if [ "$MIGA80_MODE" = AUTORUN_DIRECT ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/source-view-adf-direct-fs-uae.txt"
  printf 'PASS  native execution with NOSUPERVISOR completed\n'
fi

if [ "$MIGA80_MODE" = SELFTEST ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/source-view-adf-workflow-fs-uae.txt"
  printf 'PASS  repeated compile/run/fault/source cycles and cleanup completed\n'
fi

if [ "$MIGA80_MODE" = STOPTEST ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/source-view-adf-stop-fs-uae.txt"
  printf 'PASS  input.device Escape stopped guarded, unguarded, and stalled code\n'
fi

if [ "$MIGA80_MODE" = GRAPHICSTEST ] || [ "$MIGA80_MODE" = GRAPHICSTEST_DIRECT ]; then
  if [ "$MIGA80_MODE" = GRAPHICSTEST ]; then
    MIGA80_GRAPHICS_REPORT=source-view-adf-graphics-fs-uae.txt
  else
    MIGA80_GRAPHICS_REPORT=source-view-adf-graphics-direct-fs-uae.txt
  fi
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/$MIGA80_GRAPHICS_REPORT"
  printf 'PASS  native PLANAR blitter and PIXEL CPU match reference and both playfields\n'
fi

if [ "$MIGA80_MODE" = BROWSERTEST ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/release-browser-fs-uae.txt"
  printf 'PASS  SYS: browser, mouse/keyboard loading, errors, execution and cleanup\n'
fi

if [ "$MIGA80_MODE" = INTROTEST ]; then
  /bin/cp "$MIGA80_REPORT" \
    "$MIGA80_PROJECT_ROOT/build/reports/release-intro-fs-uae.txt"
  printf 'PASS  logo intro, generated PCM on Paula, Escape, palette and resource cleanup\n'
fi
