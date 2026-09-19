#!/usr/bin/env bash
# End-to-end acceptance against a real headless LMMS instance:
#   M1: read a real project + transport commands (test_lmms_e2e.py)
#   M2: write path + events + save + render audibility (test_lmms_write.py)
#
# Env (all optional):
#   LMMS_BIN          lmms binary            (default: ./build-clang/lmms)
#   RMMS_PY_BINDINGS  flatc --python output  (default: ./build-clang/py)
#   RMMS_SOCKET       socket path            (default: /tmp/rmms-test.sock)
#   M1_PROJECT        project for the M1 test (default: data/projects/demos/DnB.mmpz)
#   M2_SAVE_PATH      where M2 saves the project (default: /tmp/rmms-m2.mmpz)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LMMS_BIN="${LMMS_BIN:-$ROOT/build-clang/lmms}"
PY_BINDINGS="${RMMS_PY_BINDINGS:-$ROOT/build-clang/py}"
SOCKET="${RMMS_SOCKET:-/tmp/rmms-test.sock}"
M1_PROJECT="${M1_PROJECT:-$ROOT/data/projects/demos/DnB.mmpz}"
M2_SAVE_PATH="${M2_SAVE_PATH:-/tmp/rmms-m2.mmpz}"
RENDER_OUT="${M2_SAVE_PATH%.*}.wav"

fail() { echo "FAIL: $*"; exit 1; }

[ -x "$LMMS_BIN" ] || fail "lmms binary not found: $LMMS_BIN"
[ -d "$PY_BINDINGS/rmms" ] || fail "python bindings not found: $PY_BINDINGS"

export RMMS_SOCKET="$SOCKET"
export RMMS_PY_BINDINGS="$PY_BINDINGS"

run_server() {  # $1 = project file ("" = empty project)
    rm -f "$SOCKET"
    if [ -n "$1" ]; then
        RMMS_SOCKET="$SOCKET" nohup "$LMMS_BIN" --rmms-server "$1" > /tmp/rmms-server.log 2>&1 &
    else
        RMMS_SOCKET="$SOCKET" nohup "$LMMS_BIN" --rmms-server > /tmp/rmms-server.log 2>&1 &
    fi
    SERVER_PID=$!
    for _ in $(seq 1 200); do [ -S "$SOCKET" ] && return 0; sleep 0.2; done
    tail -20 /tmp/rmms-server.log
    return 1
}

stop_server() {
    kill "${SERVER_PID:-0}" 2>/dev/null || true
    wait "${SERVER_PID:-0}" 2>/dev/null || true
}

echo "=== M1: read real project + transport ($M1_PROJECT) ==="
run_server "$M1_PROJECT" || fail "LMMS did not open $SOCKET"
python3 "$ROOT/backend/test_lmms_e2e.py" || { stop_server; fail "M1 checks"; }
stop_server

echo
echo "=== M2: write path + events + save ==="
rm -f "$M2_SAVE_PATH" "$RENDER_OUT"
run_server "" || fail "LMMS did not open $SOCKET"
RMMS_SAVE_PATH="$M2_SAVE_PATH" python3 "$ROOT/backend/test_lmms_write.py" || { stop_server; fail "M2 checks"; }
stop_server

[ -f "$M2_SAVE_PATH" ] || fail "saved project missing: $M2_SAVE_PATH"

echo
echo "=== M2: render the saved project and check it is audible ==="
timeout 300 "$LMMS_BIN" render "$M2_SAVE_PATH" -o "$RENDER_OUT" > /tmp/rmms-render.log 2>&1 \
    || { tail -20 /tmp/rmms-render.log; fail "render"; }
python3 - "$RENDER_OUT" <<'PY' || exit 1
import array, sys, wave
w = wave.open(sys.argv[1], 'rb')
frames = w.getnframes()
data = w.readframes(min(frames, w.getframerate()))
peak = max((abs(x) for x in array.array('h', data)), default=0)
print(f"WAV frames={frames} channels={w.getnchannels()} peak={peak}")
sys.exit(0 if peak > 100 else 1)
PY

echo
echo "All LMMS end-to-end checks passed."
