#!/usr/bin/env python3
"""M1 acceptance test: drive a real (headless) LMMS through the RMMS socket.

Assumes an `lmms --rmms-server [project]` instance is running. Verifies that
the protocol reads a *real* project (not the mock) and that transport commands
reach the real engine:

  1. project.get_state  -> real project name/path, at least one track
  2. track.list         -> track ids/names/types from the live Song
  3. clip.list / note.list -> real clips and MIDI notes (when present)
  4. transport.get_state -> current engine state
  5. transport.play     -> position advances while playing
  6. transport.stop     -> state returns to STOPPED

Environment:
  RMMS_SOCKET          socket path (default /tmp/rmms.sock)
  RMMS_PY_BINDINGS     flatc --python output dir (default /tmp/fb/py)
  RMMS_TEST_PROJECT    expected project base name (optional assertion)
"""
import os
import socket
import struct
import sys
import time

BINDINGS = os.environ.get("RMMS_PY_BINDINGS", "/tmp/fb/py")
sys.path.insert(0, BINDINGS)

import flatbuffers

import rmms.Envelope
import rmms.MsgType
import rmms.TransportState
import rmms.ProjectGetStateRequest
import rmms.ProjectGetStateResponse
import rmms.TrackListRequest
import rmms.TrackListResponse
import rmms.ClipListRequest
import rmms.ClipListResponse
import rmms.NoteListRequest
import rmms.NoteListResponse
import rmms.TransportPlayRequest
import rmms.TransportStopRequest
import rmms.TransportGetPositionRequest
import rmms.TransportGetPositionResponse
import rmms.TransportGetStateRequest
import rmms.TransportGetStateResponse

SOCKET = os.environ.get("RMMS_SOCKET", "/tmp/rmms.sock")
EXPECT_PROJECT = os.environ.get("RMMS_TEST_PROJECT", "")

FAILURES = []


def check(cond, msg):
    if cond:
        print(f"   ok: {msg}")
    else:
        print(f"   FAIL: {msg}")
        FAILURES.append(msg)
    return cond


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
    b = flatbuffers.Builder(512)
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


def id_request(start, add_id, end, value):
    b = flatbuffers.Builder(128)
    v = b.CreateString(value)
    start(b)
    add_id(b, v)
    r = end(b)
    b.Finish(r)
    return b.Output()


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.seq = 0

    def call(self, method, payload, timeout=10.0):
        self.seq += 1
        send_frame(self.sock, envelope(method, payload, self.seq))
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.sock.settimeout(max(0.1, deadline - time.time()))
            raw = recv_frame(self.sock)
            if raw is None:
                raise ConnectionError("backend closed the connection")
            env = rmms.Envelope.Envelope.GetRootAs(raw, 0)
            if env.Method().decode() != method:
                continue  # event or other reply
            p = env.PayloadAsNumpy().tobytes() if not env.PayloadIsNone() else b""
            return p
        raise TimeoutError(f"no response for {method}")


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
    print(f"Connecting to {SOCKET} ...")
    try:
        sock = connect(SOCKET)
    except ConnectionError as e:
        print(f"   FAIL: {e}")
        return 1
    c = Client(sock)
    print("Connected.")

    try:
        # 1. real project state
        print("\n1. project.get_state (real Song)")
        payload = c.call("project.get_state", empty_request(
            rmms.ProjectGetStateRequest.ProjectGetStateRequestStart,
            rmms.ProjectGetStateRequest.ProjectGetStateRequestEnd))
        state = rmms.ProjectGetStateResponse.ProjectGetStateResponse.GetRootAs(payload, 0)
        proj = state.Project()
        name = proj.Name().decode() if proj and proj.Name() else ""
        path = proj.FilePath().decode() if proj and proj.FilePath() else ""
        print(f"   project name: {name!r} path: {path!r}")
        check(bool(name), "project name is non-empty")
        if EXPECT_PROJECT:
            check(name == EXPECT_PROJECT, f"project name == {EXPECT_PROJECT!r}")
        check(state.TracksLength() > 0, f"project has tracks ({state.TracksLength()})")

        # 2. track list
        print("\n2. track.list")
        payload = c.call("track.list", empty_request(
            rmms.TrackListRequest.TrackListRequestStart,
            rmms.TrackListRequest.TrackListRequestEnd))
        tracks = rmms.TrackListResponse.TrackListResponse.GetRootAs(payload, 0)
        print(f"   tracks: {tracks.TracksLength()}")
        track_ids = []
        for i in range(tracks.TracksLength()):
            t = tracks.Tracks(i)
            tid = t.Id().decode()
            track_ids.append(tid)
            print(f"     [{tid}] {t.Name().decode()!r} type={t.Type()}")
        check(len(track_ids) > 0, "track ids present")

        # 3. clips + notes (best effort — depends on the project)
        print("\n3. clip.list / note.list")
        clips_found = 0
        notes_found = 0
        for tid in track_ids:
            payload = c.call("clip.list", id_request(
                rmms.ClipListRequest.ClipListRequestStart,
                rmms.ClipListRequest.ClipListRequestAddTrackId,
                rmms.ClipListRequest.ClipListRequestEnd, tid))
            clips = rmms.ClipListResponse.ClipListResponse.GetRootAs(payload, 0)
            for i in range(clips.ClipsLength()):
                clip = clips.Clips(i)
                clips_found += 1
                payload = c.call("note.list", id_request(
                    rmms.NoteListRequest.NoteListRequestStart,
                    rmms.NoteListRequest.NoteListRequestAddClipId,
                    rmms.NoteListRequest.NoteListRequestEnd, clip.Id().decode()))
                notes = rmms.NoteListResponse.NoteListResponse.GetRootAs(payload, 0)
                if notes.NotesLength() > 0:
                    notes_found += notes.NotesLength()
                    n0 = notes.Notes(0)
                    print(f"     clip {clip.Id().decode()}: {notes.NotesLength()} notes, "
                          f"first key={n0.Key()} start={n0.StartTick()} len={n0.LengthTicks()}")
                    check(0 <= n0.Key() <= 127, "note key within MIDI range")
        check(clips_found > 0, f"real clips read ({clips_found})")
        check(notes_found > 0, f"real MIDI notes read ({notes_found})")

        # 4. transport state before playing
        print("\n4. transport.get_state")
        payload = c.call("transport.get_state", empty_request(
            rmms.TransportGetStateRequest.TransportGetStateRequestStart,
            rmms.TransportGetStateRequest.TransportGetStateRequestEnd))
        ts = rmms.TransportGetStateResponse.TransportGetStateResponse.GetRootAs(payload, 0)
        print(f"   state={ts.State()} position={ts.Position()}")
        check(ts.State() == rmms.TransportState.TransportState.STOPPED,
              "engine starts STOPPED")

        # 5. play -> position advances
        print("\n5. transport.play")
        start_pos = ts.Position()
        payload = c.call("transport.play", empty_request(
            rmms.TransportPlayRequest.TransportPlayRequestStart,
            rmms.TransportPlayRequest.TransportPlayRequestEnd))
        time.sleep(1.0)
        payload = c.call("transport.get_position", empty_request(
            rmms.TransportGetPositionRequest.TransportGetPositionRequestStart,
            rmms.TransportGetPositionRequest.TransportGetPositionRequestEnd))
        pos = rmms.TransportGetPositionResponse.TransportGetPositionResponse.GetRootAs(payload, 0)
        now = pos.Tick()
        print(f"   position after 1s: {now} (started at {start_pos})")
        check(now > start_pos, "position advanced while playing")

        payload = c.call("transport.get_state", empty_request(
            rmms.TransportGetStateRequest.TransportGetStateRequestStart,
            rmms.TransportGetStateRequest.TransportGetStateRequestEnd))
        ts = rmms.TransportGetStateResponse.TransportGetStateResponse.GetRootAs(payload, 0)
        check(ts.State() == rmms.TransportState.TransportState.PLAYING,
              "state == PLAYING while playing")

        # 6. stop
        print("\n6. transport.stop")
        c.call("transport.stop", empty_request(
            rmms.TransportStopRequest.TransportStopRequestStart,
            rmms.TransportStopRequest.TransportStopRequestEnd))
        time.sleep(0.5)
        payload = c.call("transport.get_state", empty_request(
            rmms.TransportGetStateRequest.TransportGetStateRequestStart,
            rmms.TransportGetStateRequest.TransportGetStateRequestEnd))
        ts = rmms.TransportGetStateResponse.TransportGetStateResponse.GetRootAs(payload, 0)
        print(f"   state={ts.State()} position={ts.Position()}")
        check(ts.State() == rmms.TransportState.TransportState.STOPPED,
              "state == STOPPED after stop")

    except (TimeoutError, ConnectionError) as e:
        print(f"   FAIL: {e}")
        FAILURES.append(str(e))
    finally:
        sock.close()

    print()
    if FAILURES:
        print(f"FAILED ({len(FAILURES)} assertion(s))")
        return 1
    print("All M1 LMMS checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
