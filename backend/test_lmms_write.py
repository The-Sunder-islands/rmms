#!/usr/bin/env python3
"""M2 acceptance: write path against a real (headless) LMMS over the socket.

Assumes `lmms --rmms-server <project>` is running. Creates a track, clip and
notes, edits them, re-reads to prove the engine holds the changes, then
exercises subscription events (track.added / transport.state_changed /
transport.position_changed). Finally saves the project so the caller can render
it and check that it is audible.

Environment:
  RMMS_SOCKET        socket path (default /tmp/rmms.sock)
  RMMS_PY_BINDINGS   flatc --python output dir (default /tmp/fb/py)
  RMMS_SAVE_PATH     where project.save should write (default /tmp/rmms-m2.mmpz)
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
import rmms.TrackType
import rmms.ClipType
import rmms.TrackAddRequest
import rmms.TrackAddResponse
import rmms.TrackListRequest
import rmms.TrackListResponse
import rmms.TrackSetNameRequest
import rmms.TrackSetVolumeRequest
import rmms.ClipAddRequest
import rmms.ClipAddResponse
import rmms.ClipListRequest
import rmms.ClipListResponse
import rmms.NoteAddRequest
import rmms.NoteAddResponse
import rmms.NoteListRequest
import rmms.NoteListResponse
import rmms.NoteSetVelocityRequest
import rmms.NoteMoveRequest
import rmms.NoteRemoveRequest
import rmms.TransportPlayRequest
import rmms.TransportStopRequest
import rmms.TransportGetStateRequest
import rmms.TransportGetStateResponse
import rmms.ProjectSaveRequest
import rmms.ProjectSaveResponse
import rmms.ProjectGetStateRequest
import rmms.ProjectGetStateResponse
import rmms.SubscriptionSubscribeRequest
import rmms.SubscriptionSubscribeResponse
import rmms.EventPositionChanged
import rmms.EventStateChanged
import rmms.EventTrackAdded

SOCKET = os.environ.get("RMMS_SOCKET", "/tmp/rmms.sock")
SAVE_PATH = os.environ.get("RMMS_SAVE_PATH", "/tmp/rmms-m2.mmpz")

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
    b = flatbuffers.Builder(2048)
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

    def wait_event(self, name, timeout=15.0, predicate=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for payload in self.events.get(name, []):
                if predicate is None or predicate(payload):
                    return payload
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
    print(f"Connecting to {SOCKET} ...")
    try:
        sock = connect(SOCKET)
    except ConnectionError as e:
        print(f"   FAIL: {e}")
        return 1
    c = Client(sock)
    print("Connected.")

    try:
        # subscribe before mutating so events can be asserted later
        b = flatbuffers.Builder(256)
        names = [b.CreateString(n) for n in (
            "track.added", "transport.state_changed", "transport.position_changed")]
        rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestStartEventsVector(b, len(names))
        for off in reversed(names):
            b.PrependUOffsetTRelative(off)
        vec = b.EndVector()
        rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestStart(b)
        rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestAddEvents(b, vec)
        r = rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestEnd(b)
        b.Finish(r)
        payload = c.call("subscription.subscribe", b.Output())
        sub = rmms.SubscriptionSubscribeResponse.SubscriptionSubscribeResponse.GetRootAs(payload, 0)
        check(sub.Status() is not None and sub.Status().Success(),
              "subscription.subscribe succeeded")

        # 1. create an instrument track
        print("\n1. track.add + track.set_name/volume")
        b = flatbuffers.Builder(128)
        nm = b.CreateString("M2 Synth")
        rmms.TrackAddRequest.TrackAddRequestStart(b)
        rmms.TrackAddRequest.TrackAddRequestAddType(b, rmms.TrackType.TrackType.INSTRUMENT)
        rmms.TrackAddRequest.TrackAddRequestAddName(b, nm)
        r = rmms.TrackAddRequest.TrackAddRequestEnd(b)
        b.Finish(r)
        payload = c.call("track.add", b.Output())
        add = rmms.TrackAddResponse.TrackAddResponse.GetRootAs(payload, 0)
        track_id = add.TrackId().decode()
        check(bool(track_id), "track.add returned an id")
        check(add.Status() is not None and add.Status().Success(), "track.add success")

        b = flatbuffers.Builder(128)
        v = b.CreateString(track_id)
        n2 = b.CreateString("M2 Renamed")
        rmms.TrackSetNameRequest.TrackSetNameRequestStart(b)
        rmms.TrackSetNameRequest.TrackSetNameRequestAddTrackId(b, v)
        rmms.TrackSetNameRequest.TrackSetNameRequestAddName(b, n2)
        r = rmms.TrackSetNameRequest.TrackSetNameRequestEnd(b)
        b.Finish(r)
        c.call("track.set_name", b.Output())

        b = flatbuffers.Builder(128)
        v = b.CreateString(track_id)
        rmms.TrackSetVolumeRequest.TrackSetVolumeRequestStart(b)
        rmms.TrackSetVolumeRequest.TrackSetVolumeRequestAddTrackId(b, v)
        rmms.TrackSetVolumeRequest.TrackSetVolumeRequestAddVolume(b, 0.8)
        r = rmms.TrackSetVolumeRequest.TrackSetVolumeRequestEnd(b)
        b.Finish(r)
        c.call("track.set_volume", b.Output())

        # 2. track.added event
        print("\n2. track.added event")
        ev = c.wait_event("track.added")
        check(ev is not None, "track.added event received")
        if ev is not None:
            evt = rmms.EventTrackAdded.EventTrackAdded.GetRootAs(ev, 0)
            tid = evt.Track().Id().decode() if evt.Track() and evt.Track().Id() else ""
            check(tid == track_id, "track.added carries the new track")

        # 3. clip + notes
        print("\n3. clip.add + note.add")
        b = flatbuffers.Builder(128)
        v = b.CreateString(track_id)
        rmms.ClipAddRequest.ClipAddRequestStart(b)
        rmms.ClipAddRequest.ClipAddRequestAddTrackId(b, v)
        rmms.ClipAddRequest.ClipAddRequestAddType(b, rmms.ClipType.ClipType.MIDI)
        rmms.ClipAddRequest.ClipAddRequestAddStartTick(b, 0)
        rmms.ClipAddRequest.ClipAddRequestAddLengthTicks(b, 768)
        r = rmms.ClipAddRequest.ClipAddRequestEnd(b)
        b.Finish(r)
        payload = c.call("clip.add", b.Output())
        clip = rmms.ClipAddResponse.ClipAddResponse.GetRootAs(payload, 0)
        clip_id = clip.ClipId().decode() if clip.ClipId() else ""
        check(bool(clip_id), "clip.add returned an id")

        note_ids = []
        for key, start in ((69, 0), (72, 192)):
            b = flatbuffers.Builder(128)
            v = b.CreateString(clip_id)
            rmms.NoteAddRequest.NoteAddRequestStart(b)
            rmms.NoteAddRequest.NoteAddRequestAddClipId(b, v)
            rmms.NoteAddRequest.NoteAddRequestAddKey(b, key)
            rmms.NoteAddRequest.NoteAddRequestAddStartTick(b, start)
            rmms.NoteAddRequest.NoteAddRequestAddLengthTicks(b, 192)
            rmms.NoteAddRequest.NoteAddRequestAddVelocity(b, 100)
            rmms.NoteAddRequest.NoteAddRequestAddPan(b, 64)
            r = rmms.NoteAddRequest.NoteAddRequestEnd(b)
            b.Finish(r)
            payload = c.call("note.add", b.Output())
            resp = rmms.NoteAddResponse.NoteAddResponse.GetRootAs(payload, 0)
            nid = resp.NoteId().decode() if resp.NoteId() else ""
            check(bool(nid), f"note.add key={key} returned an id")
            note_ids.append(nid)

        # 4. re-read proves the engine holds the edits
        print("\n4. re-read (single source of truth)")
        payload = c.call("track.list", empty_request(
            rmms.TrackListRequest.TrackListRequestStart,
            rmms.TrackListRequest.TrackListRequestEnd))
        tracks = rmms.TrackListResponse.TrackListResponse.GetRootAs(payload, 0)
        found = None
        for i in range(tracks.TracksLength()):
            t = tracks.Tracks(i)
            if t.Id().decode() == track_id:
                found = t
                break
        check(found is not None, "new track present in track.list")
        if found is not None:
            check(found.Name().decode() == "M2 Renamed", "track name persisted")
            check(abs(found.Volume() - 0.8) < 0.02, "track volume persisted")

        b = flatbuffers.Builder(128)
        v = b.CreateString(track_id)
        rmms.ClipListRequest.ClipListRequestStart(b)
        rmms.ClipListRequest.ClipListRequestAddTrackId(b, v)
        r = rmms.ClipListRequest.ClipListRequestEnd(b)
        b.Finish(r)
        payload = c.call("clip.list", b.Output())
        clips = rmms.ClipListResponse.ClipListResponse.GetRootAs(payload, 0)
        for i in range(clips.ClipsLength()):
            ci = clips.Clips(i)
            print(f"     clip[{i}] id={ci.Id().decode()} type={ci.Type()} "
                  f"start={ci.StartTick()} len={ci.LengthTicks()}")
        check(clips.ClipsLength() == 1,
              f"one clip on the new track (got {clips.ClipsLength()})")

        b = flatbuffers.Builder(128)
        v = b.CreateString(clip_id)
        rmms.NoteListRequest.NoteListRequestStart(b)
        rmms.NoteListRequest.NoteListRequestAddClipId(b, v)
        r = rmms.NoteListRequest.NoteListRequestEnd(b)
        b.Finish(r)
        payload = c.call("note.list", b.Output())
        notes = rmms.NoteListResponse.NoteListResponse.GetRootAs(payload, 0)
        check(notes.NotesLength() == 2, "two notes on the new clip")
        keys = sorted(notes.Notes(i).Key() for i in range(notes.NotesLength()))
        check(keys == [69, 72], f"note keys are {keys}")

        # 5. note edits
        print("\n5. note.set_velocity / note.move / note.remove")
        b = flatbuffers.Builder(128)
        v = b.CreateString(note_ids[0])
        rmms.NoteSetVelocityRequest.NoteSetVelocityRequestStart(b)
        rmms.NoteSetVelocityRequest.NoteSetVelocityRequestAddNoteId(b, v)
        rmms.NoteSetVelocityRequest.NoteSetVelocityRequestAddVelocity(b, 120)
        r = rmms.NoteSetVelocityRequest.NoteSetVelocityRequestEnd(b)
        b.Finish(r)
        c.call("note.set_velocity", b.Output())

        b = flatbuffers.Builder(128)
        v = b.CreateString(note_ids[0])
        rmms.NoteMoveRequest.NoteMoveRequestStart(b)
        rmms.NoteMoveRequest.NoteMoveRequestAddNoteId(b, v)
        rmms.NoteMoveRequest.NoteMoveRequestAddKey(b, 70)
        rmms.NoteMoveRequest.NoteMoveRequestAddStartTick(b, 48)
        r = rmms.NoteMoveRequest.NoteMoveRequestEnd(b)
        b.Finish(r)
        c.call("note.move", b.Output())

        b = flatbuffers.Builder(128)
        v = b.CreateString(note_ids[1])
        rmms.NoteRemoveRequest.NoteRemoveRequestStart(b)
        rmms.NoteRemoveRequest.NoteRemoveRequestAddNoteId(b, v)
        r = rmms.NoteRemoveRequest.NoteRemoveRequestEnd(b)
        b.Finish(r)
        c.call("note.remove", b.Output())

        b = flatbuffers.Builder(128)
        v = b.CreateString(clip_id)
        rmms.NoteListRequest.NoteListRequestStart(b)
        rmms.NoteListRequest.NoteListRequestAddClipId(b, v)
        r = rmms.NoteListRequest.NoteListRequestEnd(b)
        b.Finish(r)
        payload = c.call("note.list", b.Output())
        notes = rmms.NoteListResponse.NoteListResponse.GetRootAs(payload, 0)
        check(notes.NotesLength() == 1, "note removed")
        if notes.NotesLength() == 1:
            n0 = notes.Notes(0)
            check(n0.Key() == 70, f"note moved to key 70 (got {n0.Key()})")
            check(n0.StartTick() == 48, f"note moved to start 48 (got {n0.StartTick()})")
            check(n0.Velocity() == 120, f"velocity 120 (got {n0.Velocity()})")

        # 6. events while playing
        print("\n6. transport events (state_changed + position_changed)")
        b = flatbuffers.Builder(32)
        rmms.TransportPlayRequest.TransportPlayRequestStart(b)
        r = rmms.TransportPlayRequest.TransportPlayRequestEnd(b)
        b.Finish(r)
        c.call("transport.play", b.Output())

        ev = c.wait_event("transport.state_changed", timeout=5.0,
                          predicate=lambda p: rmms.EventStateChanged.EventStateChanged
                          .GetRootAs(p, 0).State() == rmms.TransportState.TransportState.PLAYING)
        check(ev is not None, "state_changed PLAYING received")
        first = c.wait_event("transport.position_changed", timeout=5.0)
        check(first is not None, "position_changed received while playing")
        # Keep draining the socket for a while: events already queued must be
        # read before they can be counted.
        drain_deadline = time.time() + 0.8
        while time.time() < drain_deadline:
            try:
                c._read(0.2)
            except (TimeoutError, ConnectionError):
                pass
        count = len(c.events.get("transport.position_changed", []))
        check(count >= 3, f"position stream flows ({count} events)")

        b = flatbuffers.Builder(32)
        rmms.TransportStopRequest.TransportStopRequestStart(b)
        r = rmms.TransportStopRequest.TransportStopRequestEnd(b)
        b.Finish(r)
        c.call("transport.stop", b.Output())
        ev = c.wait_event("transport.state_changed", timeout=5.0,
                          predicate=lambda p: rmms.EventStateChanged.EventStateChanged
                          .GetRootAs(p, 0).State() == rmms.TransportState.TransportState.STOPPED)
        check(ev is not None, "state_changed STOPPED received")

        # 7. save for the render check
        print(f"\n7. project.save -> {SAVE_PATH}")
        b = flatbuffers.Builder(256)
        v = b.CreateString(SAVE_PATH)
        rmms.ProjectSaveRequest.ProjectSaveRequestStart(b)
        rmms.ProjectSaveRequest.ProjectSaveRequestAddFilePath(b, v)
        r = rmms.ProjectSaveRequest.ProjectSaveRequestEnd(b)
        b.Finish(r)
        payload = c.call("project.save", b.Output())
        resp = rmms.ProjectSaveResponse.ProjectSaveResponse.GetRootAs(payload, 0)
        check(resp.Status() is not None and resp.Status().Success(),
              "project.save reported success")
        check(os.path.exists(SAVE_PATH), f"saved project exists at {SAVE_PATH}")

    except (TimeoutError, ConnectionError) as e:
        print(f"   FAIL: {e}")
        FAILURES.append(str(e))
    finally:
        sock.close()

    print()
    if FAILURES:
        print(f"FAILED ({len(FAILURES)} assertion(s))")
        return 1
    print("All M2 write-path checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
