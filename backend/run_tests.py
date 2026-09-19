#!/usr/bin/env python3
"""Run the RMMS backend end-to-end tests.

Starts rmms_mock_backend, runs test_mock.py (protocol contract) and
test_ai.py (AI bridge + fake AI server), then shuts the backend down.

Environment:
  RMMS_BACKEND_BIN   path to rmms_mock_backend (default: backend/build/...)
  RMMS_PY_BINDINGS   path to flatc --python output (default: build/py, then
                     /tmp/fb/py)
"""
import os
import signal
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
SOCKET = "/tmp/rmms.sock"


def default_backend_bin():
    for candidate in (
        os.path.join(ROOT, "build", "rmms_mock_backend"),
        os.path.join(ROOT, "build", "Debug", "rmms_mock_backend"),
    ):
        if os.path.exists(candidate):
            return candidate
    return os.path.join(ROOT, "build", "rmms_mock_backend")


def wait_for_socket(path, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(1)
                s.connect(path)
                s.close()
                return True
            except OSError:
                pass
        time.sleep(0.05)
    return False


def pick_bindings(env):
    if env.get("RMMS_PY_BINDINGS"):
        return env
    for candidate in (os.path.join(ROOT, "build", "py"), "/tmp/fb/py"):
        if os.path.isdir(os.path.join(candidate, "rmms")):
            env["RMMS_PY_BINDINGS"] = candidate
            break
    return env


def main():
    backend_bin = os.environ.get("RMMS_BACKEND_BIN") or default_backend_bin()
    if not os.path.exists(backend_bin):
        print(f"Backend binary not found: {backend_bin}")
        print("Build it first: cmake --build backend/build -j$(nproc)")
        return 1

    env = pick_bindings(os.environ.copy())

    if os.path.exists(SOCKET):
        os.unlink(SOCKET)  # stale socket from a previous run

    proc = subprocess.Popen([backend_bin], env=env)
    results = {}
    try:
        if not wait_for_socket(SOCKET):
            print("Backend did not open /tmp/rmms.sock")
            return 1

        for name, script in (("test_mock", "test_mock.py"),
                             ("test_ai", "test_ai.py"),
                             ("test_ai_import", "test_ai_import.py")):
            print(f"\n{'=' * 70}\n{name}: {script}\n{'=' * 70}")
            rc = subprocess.call([sys.executable, os.path.join(ROOT, script)], env=env)
            results[name] = rc
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    print(f"\n{'=' * 70}")
    for name, rc in results.items():
        print(f"{name}: {'PASS' if rc == 0 else 'FAIL'}")
    return 0 if results and all(rc == 0 for rc in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
