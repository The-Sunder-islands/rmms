#!/usr/bin/env python3
"""M3 acceptance: download AI results through the bridge and import them.

Starts a fake AI server that serves two real WAV stems, then asks the RMMS
backend (mock or LMMS, whichever owns RMMS_SOCKET) to import the results of a
completed task:

  ai.import_results(task_id) ->
      GET /api/v1/tasks/{id}      -> completed_urls
      GET /files/{task}/{f}.wav   -> download to import_dir
      create Audio track + clip, load the sample

Verifies the completion event, the created tracks/clips (incl. audio_url) and
that the files really landed on disk.

Environment:
  RMMS_SOCKET        socket path (default /tmp/rmms.sock)
  RMMS_PY_BINDINGS   flatc --python output dir (default /tmp/fb/py)
  RMMS_IMPORT_DIR    download directory (default /tmp/rmms-import-test)
  RMMS_SAVE_AFTER    if set, save the project to this path after import
"""
import http.server
import json
import math
import os
import shutil
import socket
import struct
import sys
import threading
import time

BINDINGS = os.environ.get("RMMS_PY_BINDINGS", "/tmp/fb/py")
sys.path.insert(0, BINDINGS)

import flatbuffers

import rmms.Envelope
import rmms.MsgType
import rmms.TrackType
import rmms.ClipType
import rmms.TrackListRequest
import rmms.TrackListResponse
import rmms.ClipListRequest
import rmms.ClipListResponse
import rmms.AIImportResultsRequest
import rmms.AIImportResultsResponse
import rmms.ProjectSaveRequest
import rmms.ProjectSaveResponse

SOCKET = os.environ.get("RMMS_SOCKET", "/tmp/rmms.sock")
IMPORT_DIR = os.environ.get("RMMS_IMPORT_DIR", "/tmp/rmms-import-test")
SAVE_AFTER = os.environ.get("RMMS_SAVE_AFTER", "")
FAKE_HOST, FAKE_PORT = "127.0.0.1", 8420
TASK_ID = "task-900"
STEMS = {"vocals.wav": 440.0, "drums.wav": 220.0}

FAILURES = []


def check(cond, msg):
    if cond:
        print(f"   ok: {msg}")
    else:
        print(f"   FAIL: {msg}")
        FAILURES.append(msg)
    return cond


def make_wav(freq, seconds=1.0, rate=44100):
    frames = int(seconds * rate)
    data = bytearray()
    for i in range(frames):
        v = int(20000 * math.sin(2 * math.pi * freq * i / rate))
        data += struct.pack("<h", v)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    hdr += b"data" + struct.pack("<I", len(data))
    return bytes(hdr + data)


class FakeAIHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == f"/api/v1/tasks/{TASK_ID}":
            self._json(200, {
                "task_id": TASK_ID, "status": "done", "current_step": 1,
                "completed_urls": [
                    {"step_index": 0, "step_type": "split",
                     "urls": [f"http://{FAKE_HOST}:{FAKE_PORT}/files/{TASK_ID}/{n}"]}
                    for n in STEMS
                ],
                "errors": [],
            })
        elif self.path.startswith(f"/files/{TASK_ID}/"):
            name = self.path.rsplit("/", 1)[-1]
            wav = make_wav(STEMS.get(name, 440.0))
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(len(wav)))
            self.end_headers()
            self.wfile.write(wav)
        else:
            self._json(404, {"detail": f"no route {self.path}"})


def send_frame(sock, raw):
    sock.sendall(struct.pack("<I", len(raw)) + raw)


def recv_frame(sock):
    hdr = sock.recv(4)
    if not hdr:
        return None
    n = struct.unpack("<I", hdr)[0]
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def envelope(method, payload, seq):
    b = flatbuffers.Builder(256)
    m = b.CreateString(method)
    p = b.CreateByteVector(payload)
    rmms.Envelope.EnvelopeStart(b)
    rmms.Envelope.EnvelopeAddMsgType(b, rmms.MsgType.MsgType.REQUEST)
    rmms.Envelope.EnvelopeAddSeqId(b, seq)
    rmms.Envelope.EnvelopeAddMethod(b, m)
    rmms.Envelope.EnvelopeAddPayload(b, p)
    env = rmms.Envelope.EnvelopeEnd(b)
    b.Finish(env)
    return b.Output()


def empty_request(start, end):
    b = flatbuffers.Builder(32)
    start(b)
    r = end(b)
    b.Finish(r)
    return b.Output()


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.seq = 0
        self.events = {}

    def _read(self, timeout):
        self.sock.settimeout(timeout)
        raw = recv_frame(self.sock)
        if raw is None:
            raise ConnectionError("backend closed the connection")
        env = rmms.Envelope.Envelope.GetRootAs(raw, 0)
        method = env.Method().decode()
        payload = env.PayloadAsNumpy().tobytes() if not env.PayloadIsNone() else b""
        if env.MsgType() == rmms.MsgType.MsgType.EVENT:
            self.events.setdefault(method, []).append(payload)
        return method, payload

    def call(self, method, payload, timeout=15.0):
        self.seq += 1
        send_frame(self.sock, envelope(method, payload, self.seq))
        deadline = time.time() + timeout
        while time.time() < deadline:
            m, p = self._read(max(0.1, deadline - time.time()))
            if m == method:
                return p
        raise TimeoutError(f"no response for {method}")

    def wait_event(self, name, timeout=30.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.events.get(name):
                return self.events[name][0]
            self._read(min(0.5, max(0.05, deadline - time.time())))
        return None


def connect(path, timeout=15.0):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect(path)
            return s
        except OSError as e:
            last = e
            time.sleep(0.2)
    raise ConnectionError(f"cannot connect to {path}: {last}")


def main():
    try:
        fake = http.server.ThreadingHTTPServer((FAKE_HOST, FAKE_PORT), FakeAIHandler)
    except OSError as e:
        print(f"Cannot bind fake AI server on {FAKE_HOST}:{FAKE_PORT}: {e}")
        return 1
    fake.daemon_threads = True
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    print(f"Fake AI server on {FAKE_HOST}:{FAKE_PORT}")

    shutil.rmtree(IMPORT_DIR, ignore_errors=True)

    try:
        sock = connect(SOCKET)
    except ConnectionError as e:
        print(f"   FAIL: {e}")
        fake.shutdown()
        return 1
    c = Client(sock)

    try:
        # 1. import
        print("\n1. ai.import_results")
        b = flatbuffers.Builder(256)
        tid = b.CreateString(TASK_ID)
        d = b.CreateString(IMPORT_DIR)
        rmms.AIImportResultsRequest.AIImportResultsRequestStart(b)
        rmms.AIImportResultsRequest.AIImportResultsRequestAddTaskId(b, tid)
        rmms.AIImportResultsRequest.AIImportResultsRequestAddImportDir(b, d)
        r = rmms.AIImportResultsRequest.AIImportResultsRequestEnd(b)
        b.Finish(r)
        payload = c.call("ai.import_results", b.Output())
        ack = rmms.AIImportResultsResponse.AIImportResultsResponse.GetRootAs(payload, 0)
        check(ack.Status() is not None and ack.Status().Success(), "import accepted")

        ev = c.wait_event("ai.import_results.result")
        check(ev is not None, "ai.import_results.result event received")
        if ev is None:
            raise TimeoutError("no import result event")
        res = rmms.AIImportResultsResponse.AIImportResultsResponse.GetRootAs(ev, 0)
        check(res.Status() is not None and res.Status().Success(), "import succeeded")
        check(res.CreatedTracksLength() == len(STEMS),
              f"created {len(STEMS)} tracks (got {res.CreatedTracksLength()})")
        check(res.ImportedFilesLength() == len(STEMS),
              f"downloaded {len(STEMS)} files (got {res.ImportedFilesLength()})")
        track_ids = [res.CreatedTracks(i).decode() for i in range(res.CreatedTracksLength())]
        files = [res.ImportedFiles(i).decode() for i in range(res.ImportedFilesLength())]
        for f in files:
            check(os.path.exists(f), f"downloaded file exists: {f}")

        # 2. project holds the imported stems
        print("\n2. project re-read")
        payload = c.call("track.list", empty_request(
            rmms.TrackListRequest.TrackListRequestStart,
            rmms.TrackListRequest.TrackListRequestEnd))
        tracks = rmms.TrackListResponse.TrackListResponse.GetRootAs(payload, 0)
        by_id = {}
        for i in range(tracks.TracksLength()):
            t = tracks.Tracks(i)
            by_id[t.Id().decode()] = t
        names = []
        for tid_ in track_ids:
            t = by_id.get(tid_)
            if check(t is not None, f"track {tid_} present"):
                names.append(t.Name().decode())
                check(t.Type() == rmms.TrackType.TrackType.AUDIO,
                      f"track '{t.Name().decode()}' is AUDIO")
        check(sorted(names) == sorted(s.replace(".wav", "") for s in STEMS),
              f"track names from stems: {sorted(names)}")

        for tid_ in track_ids:
            b = flatbuffers.Builder(128)
            v = b.CreateString(tid_)
            rmms.ClipListRequest.ClipListRequestStart(b)
            rmms.ClipListRequest.ClipListRequestAddTrackId(b, v)
            r = rmms.ClipListRequest.ClipListRequestEnd(b)
            b.Finish(r)
            payload = c.call("clip.list", b.Output())
            clips = rmms.ClipListResponse.ClipListResponse.GetRootAs(payload, 0)
            if check(clips.ClipsLength() == 1, f"track {tid_} has one clip"):
                cl = clips.Clips(0)
                check(cl.AudioUrl().decode().startswith(IMPORT_DIR) if cl.AudioUrl() else False,
                      f"clip audio_url set ({cl.AudioUrl().decode() if cl.AudioUrl() else ''})")
                check(cl.Type() == rmms.ClipType.ClipType.AUDIO, "clip type AUDIO")

        # 3. optional save for the render check
        if SAVE_AFTER:
            print(f"\n3. project.save -> {SAVE_AFTER}")
            b = flatbuffers.Builder(256)
            v = b.CreateString(SAVE_AFTER)
            rmms.ProjectSaveRequest.ProjectSaveRequestStart(b)
            rmms.ProjectSaveRequest.ProjectSaveRequestAddFilePath(b, v)
            r = rmms.ProjectSaveRequest.ProjectSaveRequestEnd(b)
            b.Finish(r)
            payload = c.call("project.save", b.Output())
            resp = rmms.ProjectSaveResponse.ProjectSaveResponse.GetRootAs(payload, 0)
            check(resp.Status() is not None and resp.Status().Success(),
                  "project saved after import")
            check(os.path.exists(SAVE_AFTER), f"saved project exists at {SAVE_AFTER}")

    except (TimeoutError, ConnectionError) as e:
        print(f"   FAIL: {e}")
        FAILURES.append(str(e))
    finally:
        sock.close()
        try:
            fake.shutdown()
            fake.server_close()
        except Exception:
            pass

    print()
    if FAILURES:
        print(f"FAILED ({len(FAILURES)} assertion(s))")
        return 1
    print("All AI import checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
