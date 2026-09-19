#!/usr/bin/env python3
"""End-to-end test for the RMMS AI bridge (request dispatch + SSE forwarding).

Starts a local fake AI server on 127.0.0.1:8420, then drives a running
rmms_mock_backend over /tmp/rmms.sock and verifies:

  1. ai.get_capabilities -> RESPONSE ack + "ai.get_capabilities.result" event
  2. ai.submit_pipeline  -> POST /api/v1/tasks (fake server rejects GET/other
                            with 405, and validates the JSON body)
  3. SSE stream          -> ai.progress / ai.partial_result / ai.final_result
  4. ai.get_task_status  -> GET /api/v1/tasks/{id}
  5. ai.list_tasks       -> GET /api/v1/tasks?...
  6. ai.cancel_task      -> DELETE /api/v1/tasks/{id} (GET on the same path
                            returns 405, so a GET-based bug fails the test)
  7. unreachable server  -> AI_SERVER_UNREACHABLE error status

Requires rmms_mock_backend to be running (see run_tests.py).
"""
import http.server
import json
import os
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
import rmms.AIGetCapabilitiesRequest
import rmms.AIGetCapabilitiesResponse
import rmms.AISubmitPipelineRequest
import rmms.AISubmitPipelineResponse
import rmms.AIGetTaskStatusRequest
import rmms.AIGetTaskStatusResponse
import rmms.AICancelTaskRequest
import rmms.AICancelTaskResponse
import rmms.AIListTasksRequest
import rmms.AIListTasksResponse
import rmms.AIPipeline
import rmms.AIStep
import rmms.AIEventProgress
import rmms.AIEventPartialResult
import rmms.AIEventFinalResult

SOCKET = "/tmp/rmms.sock"
FAKE_HOST, FAKE_PORT = "127.0.0.1", 8420
TASK_POST = "task-123"
TASK_GET = "task-456"
TASK_HOLD = "task-789"  # long-running stream, used for cancel-while-active

CAPABILITIES = {
    "protocol_version": "1.0.0",
    "server_version": "fake-1.0",
    "capabilities": [
        {
            "id": "stem_split",
            "label": "Stem Split",
            "description": "Separate a mix into stems",
            "status": "implemented",
            "models": ["demucs"],
            "default_model": "demucs",
            "param_defs": [],
        }
    ],
    "devices": [],
    "scheduler": {"max_concurrent_tasks": 2, "max_queue_size": 8},
    "output_formats": ["wav"],
    "output_packages": ["separate"],
    "max_upload_bytes": 104857600,
}

FAILURES = []


def check(cond, msg):
    if cond:
        print(f"   ok: {msg}")
    else:
        print(f"   FAIL: {msg}")
        FAILURES.append(msg)
    return cond


# ── Fake AI server ──────────────────────────────────────────────────────────

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

    def _read_body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(n) if n else b""

    def do_GET(self):
        if self.path == "/api/v1/capabilities":
            self._json(200, CAPABILITIES)
        elif self.path == f"/api/v1/tasks/{TASK_GET}":
            self._json(200, {
                "task_id": TASK_GET, "status": "processing",
                "current_step": 1, "step_type": "midi", "percent": 50,
                "completed_urls": [], "errors": [],
            })
        elif self.path.startswith("/api/v1/tasks?"):
            self._json(200, {"tasks": [{"task_id": TASK_POST, "status": "done"}],
                             "total": 1})
        elif self.path == f"/api/v1/tasks/{TASK_POST}/events":
            self._sse()
        elif self.path == f"/api/v1/tasks/{TASK_HOLD}/events":
            self._sse_hold()
        elif self.path in (f"/api/v1/tasks/{TASK_POST}", f"/api/v1/tasks/{TASK_HOLD}"):
            # Proves ai.cancel_task uses DELETE, not GET.
            self._json(405, {"detail": "use DELETE"})
        else:
            self._json(404, {"detail": f"no route {self.path}"})

    def do_POST(self):
        if self.path != "/api/v1/tasks":
            self._json(404, {"detail": f"no route {self.path}"})
            return
        body = self._read_body()
        try:
            parsed = json.loads(body or b"{}")
        except ValueError:
            parsed = {}
        steps = parsed.get("pipeline", {}).get("steps", [])
        if not steps or steps[0].get("capability") != "split":
            self._json(400, {"detail": "missing pipeline step"})
            return
        task_id = TASK_HOLD if "hold" in parsed.get("input_url", "") else TASK_POST
        self._json(200, {"task_id": task_id, "cached": False, "status": "queued"})

    def do_DELETE(self):
        if self.path in (f"/api/v1/tasks/{TASK_POST}", f"/api/v1/tasks/{TASK_HOLD}"):
            self._json(200, {"success": True})
        else:
            self._json(404, {"detail": f"no route {self.path}"})

    def _sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()

        def emit(event, data):
            self.wfile.write(f"event: {event}\ndata: {json.dumps(data)}\n\n".encode())
            self.wfile.flush()

        emit("progress", {"task_id": TASK_POST, "step_index": 0, "step_type": "split",
                          "status": "running", "percent": 30, "urls": []})
        time.sleep(0.05)
        emit("partial_result", {
            "task_id": TASK_POST, "step_index": 0, "step_type": "split",
            "track": {"stem": "vocals", "track_type": "audio", "label": "Vocals",
                      "url": f"http://{FAKE_HOST}:{FAKE_PORT}/files/{TASK_POST}/vocals.wav",
                      "format": "wav", "sample_rate": 44100,
                      "duration": 3.5, "size_bytes": 12345},
        })
        time.sleep(0.05)
        emit("final_result", {
            "task_id": TASK_POST, "status": "done",
            "urls": [{"step_index": 0, "step_type": "split",
                      "url": f"http://{FAKE_HOST}:{FAKE_PORT}/files/{TASK_POST}/vocals.wav"}],
            "errors": [],
        })
        self.close_connection = True

    def _sse_hold(self):
        # Emits one event, then keeps the stream open until the client goes
        # away (or 30 s). Used to verify cancel-while-streaming.
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write((
            "event: progress\n"
            'data: {"task_id": "' + TASK_HOLD + '", "step_index": 0, '
            '"step_type": "split", "status": "running", "percent": 5, "urls": []}\n\n'
        ).encode())
        self.wfile.flush()
        deadline = time.time() + 30
        while time.time() < deadline:
            time.sleep(1.0)
            try:
                self.wfile.write(b": keepalive\n\n")
                self.wfile.flush()
            except OSError:
                break
        self.close_connection = True


# ── FlatBuffers framing helpers ─────────────────────────────────────────────

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
    b = flatbuffers.Builder(1024)
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


def unpack(raw):
    env = rmms.Envelope.Envelope.GetRootAs(raw, 0)
    mt = env.MsgType()
    seq = env.SeqId()
    method = env.Method().decode() if env.Method() else ""
    payload = env.PayloadAsNumpy().tobytes() if not env.PayloadIsNone() else b""
    return mt, seq, method, payload


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.responses = {}
        self.events = {}

    def send(self, method, payload, seq):
        send_frame(self.sock, envelope(method, payload, seq))

    def pump(self, timeout):
        self.sock.settimeout(timeout)
        try:
            raw = recv_frame(self.sock)
        except socket.timeout:
            return None
        if raw is None:
            raise ConnectionError("backend closed the connection")
        mt, seq, m, p = unpack(raw)
        if mt == rmms.MsgType.MsgType.RESPONSE:
            self.responses.setdefault(m, []).append((seq, p))
        elif mt == rmms.MsgType.MsgType.EVENT:
            self.events.setdefault(m, []).append((seq, p))
        return mt, seq, m, p

    def wait(self, table, name, timeout=15.0):
        deadline = time.time() + timeout
        while name not in table:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for '{name}'")
            self.pump(min(0.5, remaining))
        return table[name]


def build_capabilities_request():
    b = flatbuffers.Builder(32)
    rmms.AIGetCapabilitiesRequest.AIGetCapabilitiesRequestStart(b)
    r = rmms.AIGetCapabilitiesRequest.AIGetCapabilitiesRequestEnd(b)
    b.Finish(r)
    return b.Output()


def build_submit_request(input_url="http://127.0.0.1:8420/files/x/mix.wav"):
    b = flatbuffers.Builder(256)
    step_type = b.CreateString("split")
    rmms.AIStep.AIStepStart(b)
    rmms.AIStep.AIStepAddType(b, step_type)
    step = rmms.AIStep.AIStepEnd(b)

    rmms.AIPipeline.AIPipelineStartStepsVector(b, 1)
    b.PrependUOffsetTRelative(step)
    steps = b.EndVector()

    rmms.AIPipeline.AIPipelineStart(b)
    rmms.AIPipeline.AIPipelineAddSteps(b, steps)
    pipeline = rmms.AIPipeline.AIPipelineEnd(b)

    input_url = b.CreateString(input_url)
    rmms.AISubmitPipelineRequest.AISubmitPipelineRequestStart(b)
    rmms.AISubmitPipelineRequest.AISubmitPipelineRequestAddInputUrl(b, input_url)
    rmms.AISubmitPipelineRequest.AISubmitPipelineRequestAddPipeline(b, pipeline)
    rmms.AISubmitPipelineRequest.AISubmitPipelineRequestAddDeviceIndex(b, -1)
    rmms.AISubmitPipelineRequest.AISubmitPipelineRequestAddPriority(b, 5)
    r = rmms.AISubmitPipelineRequest.AISubmitPipelineRequestEnd(b)
    b.Finish(r)
    return b.Output()


def build_task_id_request(table_start, table_add_id, table_end, task_id):
    b = flatbuffers.Builder(128)
    tid = b.CreateString(task_id)
    table_start(b)
    table_add_id(b, tid)
    r = table_end(b)
    b.Finish(r)
    return b.Output()


def build_list_tasks_request(limit):
    b = flatbuffers.Builder(64)
    rmms.AIListTasksRequest.AIListTasksRequestStart(b)
    rmms.AIListTasksRequest.AIListTasksRequestAddLimit(b, limit)
    r = rmms.AIListTasksRequest.AIListTasksRequestEnd(b)
    b.Finish(r)
    return b.Output()


# ── Test flow ───────────────────────────────────────────────────────────────

def main():
    if not os.path.exists(SOCKET):
        print(f"Socket {SOCKET} not found. Start rmms_mock_backend first.")
        return 1

    try:
        fake = http.server.ThreadingHTTPServer((FAKE_HOST, FAKE_PORT), FakeAIHandler)
    except OSError as e:
        print(f"Cannot bind fake AI server on {FAKE_HOST}:{FAKE_PORT}: {e}")
        print("Stop any AI server using that port and retry.")
        return 1
    fake.daemon_threads = True
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    print(f"Fake AI server listening on {FAKE_HOST}:{FAKE_PORT}")

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(SOCKET)
    print(f"Connected to {SOCKET}")
    c = Client(sock)

    try:
        # 1. capabilities
        print("\n1. ai.get_capabilities")
        c.send("ai.get_capabilities", build_capabilities_request(), 42)
        c.wait(c.events, "ai.get_capabilities.result")
        seq, payload = c.events["ai.get_capabilities.result"][0]
        check(seq == 42, "result event echoes seq_id 42")
        resp = rmms.AIGetCapabilitiesResponse.AIGetCapabilitiesResponse.GetRootAs(payload, 0)
        check(resp.Status() is not None and resp.Status().Success(),
              "capabilities result status.success")
        caps = resp.Capabilities()
        check(caps is not None and caps.CapabilitiesLength() == 1,
              "one capability parsed from JSON")
        if caps and caps.CapabilitiesLength() == 1:
            check(caps.Capabilities(0).Id().decode() == "stem_split",
                  "capability id == stem_split")
        check("ai.get_capabilities" in c.responses,
              "immediate acceptance RESPONSE received")

        # 2. submit pipeline (must be POST)
        print("\n2. ai.submit_pipeline (POST)")
        c.send("ai.submit_pipeline", build_submit_request(), 43)
        c.wait(c.events, "ai.submit_pipeline.result")
        seq, payload = c.events["ai.submit_pipeline.result"][0]
        check(seq == 43, "result event echoes seq_id 43")
        sub = rmms.AISubmitPipelineResponse.AISubmitPipelineResponse.GetRootAs(payload, 0)
        check(sub.Status() is not None and sub.Status().Success(),
              "submit result status.success")
        check(sub.TaskId() is not None and sub.TaskId().decode() == TASK_POST,
              f"task_id == {TASK_POST} (proves POST body reached server)")

        # 3. SSE forwarding
        print("\n3. SSE forwarding (progress / partial_result / final_result)")
        c.wait(c.events, "ai.progress")
        seq, payload = c.events["ai.progress"][0]
        prog = rmms.AIEventProgress.AIEventProgress.GetRootAs(payload, 0)
        check(prog.Percent() == 30, "progress percent == 30")
        check(prog.StepType().decode() == "split", "progress step_type == split")

        c.wait(c.events, "ai.partial_result")
        seq, payload = c.events["ai.partial_result"][0]
        part = rmms.AIEventPartialResult.AIEventPartialResult.GetRootAs(payload, 0)
        check(part.Url().decode().endswith("vocals.wav"),
              "partial_result url parsed")
        check(part.TrackType().decode() == "audio", "partial_result track_type")

        c.wait(c.events, "ai.final_result")
        seq, payload = c.events["ai.final_result"][0]
        fin = rmms.AIEventFinalResult.AIEventFinalResult.GetRootAs(payload, 0)
        check(fin.TaskId().decode() == TASK_POST, "final_result task_id")
        check(fin.UrlsLength() == 1, "final_result carries one urls entry")

        # 4. get_task_status (GET)
        print("\n4. ai.get_task_status (GET)")
        payload = build_task_id_request(
            rmms.AIGetTaskStatusRequest.AIGetTaskStatusRequestStart,
            rmms.AIGetTaskStatusRequest.AIGetTaskStatusRequestAddTaskId,
            rmms.AIGetTaskStatusRequest.AIGetTaskStatusRequestEnd, TASK_GET)
        c.send("ai.get_task_status", payload, 44)
        c.wait(c.events, "ai.get_task_status.result")
        _, payload = c.events["ai.get_task_status.result"][0]
        st = rmms.AIGetTaskStatusResponse.AIGetTaskStatusResponse.GetRootAs(payload, 0)
        check(st.Percent() == 50, "task status percent == 50")

        # 5. list_tasks (GET with query string)
        print("\n5. ai.list_tasks (GET)")
        c.send("ai.list_tasks", build_list_tasks_request(5), 45)
        c.wait(c.events, "ai.list_tasks.result")
        _, payload = c.events["ai.list_tasks.result"][0]
        lst = rmms.AIListTasksResponse.AIListTasksResponse.GetRootAs(payload, 0)
        check(lst.TasksLength() == 1, "list_tasks returned one task")
        check(lst.Total() == 1, "list_tasks total == 1")

        # 6. cancel_task (DELETE)
        print("\n6. ai.cancel_task (DELETE)")
        payload = build_task_id_request(
            rmms.AICancelTaskRequest.AICancelTaskRequestStart,
            rmms.AICancelTaskRequest.AICancelTaskRequestAddTaskId,
            rmms.AICancelTaskRequest.AICancelTaskRequestEnd, TASK_POST)
        c.send("ai.cancel_task", payload, 46)
        c.wait(c.events, "ai.cancel_task.result")
        _, payload = c.events["ai.cancel_task.result"][0]
        can = rmms.AICancelTaskResponse.AICancelTaskResponse.GetRootAs(payload, 0)
        check(can.Status() is not None and can.Status().Success(),
              "cancel result success (DELETE, not GET)")

        # 7. cancel while the SSE stream is still open
        print("\n7. ai.cancel_task while streaming")
        c.send("ai.submit_pipeline",
               build_submit_request(input_url="http://127.0.0.1:8420/files/x/hold.wav"), 47)
        deadline = time.time() + 10
        hold_task = None
        while time.time() < deadline and hold_task is None:
            c.pump(0.5)
            for seq, payload in c.events.get("ai.submit_pipeline.result", []):
                if seq == 47:
                    hold_task = rmms.AISubmitPipelineResponse.AISubmitPipelineResponse \
                        .GetRootAs(payload, 0).TaskId().decode()
        check(hold_task == TASK_HOLD, f"hold task_id == {TASK_HOLD}")

        # Wait for the hold task's first progress event (the stream stays open).
        got_hold_progress = False
        deadline = time.time() + 10
        while time.time() < deadline and not got_hold_progress:
            c.pump(0.5)
            for _, payload in c.events.get("ai.progress", []):
                prog = rmms.AIEventProgress.AIEventProgress.GetRootAs(payload, 0)
                if prog.TaskId().decode() == TASK_HOLD:
                    got_hold_progress = True
                    break
        check(got_hold_progress, "hold stream is active (progress received)")

        payload = build_task_id_request(
            rmms.AICancelTaskRequest.AICancelTaskRequestStart,
            rmms.AICancelTaskRequest.AICancelTaskRequestAddTaskId,
            rmms.AICancelTaskRequest.AICancelTaskRequestEnd, TASK_HOLD)
        start = time.time()
        c.send("ai.cancel_task", payload, 48)
        entry = None
        while time.time() - start < 10:
            c.pump(0.5)
            for e in c.events.get("ai.cancel_task.result", []):
                if e[0] == 48:
                    entry = e
                    break
            if entry:
                break
        elapsed = time.time() - start
        check(entry is not None, "cancel-while-streaming result received")
        if entry:
            can = rmms.AICancelTaskResponse.AICancelTaskResponse.GetRootAs(entry[1], 0)
            check(can.Status() is not None and can.Status().Success(),
                  "cancel-while-streaming result success")
            check(elapsed < 5.0,
                  f"cancel did not hang on the SSE thread ({elapsed:.2f}s)")

        # 8. unreachable AI server error mapping
        print("\n8. AI server unreachable -> AI_SERVER_UNREACHABLE")
        fake.shutdown()
        fake.server_close()
        time.sleep(0.1)
        c.send("ai.get_capabilities", build_capabilities_request(), 49)
        deadline = time.time() + 10
        result = None
        while time.time() < deadline:
            c.pump(0.5)
            entries = c.events.get("ai.get_capabilities.result", [])
            for seq, payload in entries:
                if seq == 49:
                    result = payload
                    break
            if result is not None:
                break
        if check(result is not None, "unreachable result event received"):
            resp = rmms.AIGetCapabilitiesResponse.AIGetCapabilitiesResponse.GetRootAs(result, 0)
            status = resp.Status()
            code = status.ErrorCode().decode() if status and status.ErrorCode() else ""
            check(not status.Success() and code == "AI_SERVER_UNREACHABLE",
                  f"error_code == AI_SERVER_UNREACHABLE (got '{code}')")
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
    print("All AI bridge checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
