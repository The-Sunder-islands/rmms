#!/usr/bin/env python3
"""Test RMMS mock backend via Unix socket + FlatBuffers."""
import os, socket, struct, sys
sys.path.insert(0, "/tmp/fb/py")

import flatbuffers

# Import every type we need
import rmms.Envelope
import rmms.MsgType
import rmms.TrackType
import rmms.TransportPlayRequest
import rmms.TransportGetStateRequest
import rmms.TransportGetStateResponse
import rmms.TrackAddRequest
import rmms.TrackListRequest
import rmms.TrackListResponse
import rmms.TrackSetVolumeRequest
import rmms.MixerListChannelsRequest
import rmms.MixerListChannelsResponse
import rmms.PluginListRequest
import rmms.PluginListResponse
import rmms.ProjectGetStateRequest
import rmms.ProjectGetStateResponse
import rmms.TrackAddResponse
import rmms.SubscriptionSubscribeRequest
import rmms.SubscriptionSubscribeResponse
import rmms.SubscriptionUnsubscribeRequest

SOCKET = "/tmp/rmms.sock"

def send_frame(sock, raw):
    sock.sendall(struct.pack("<I", len(raw)) + raw)

def recv_frame(sock):
    hdr = sock.recv(4)
    if not hdr: return None
    n = struct.unpack("<I", hdr)[0]
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk: return None
        data += chunk
    return data

def req(method, payload):
    """Wrap payload bytes in an Envelope with REQUEST type."""
    b = flatbuffers.Builder(512)
    m = b.CreateString(method)
    p = b.CreateByteVector(payload)
    rmms.Envelope.EnvelopeStart(b)
    rmms.Envelope.EnvelopeAddMsgType(b, rmms.MsgType.MsgType.REQUEST)
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

def main():
    if not os.path.exists(SOCKET):
        print(f"Socket {SOCKET} not found. Start the backend first.")
        return 1

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(SOCKET)
    print(f"Connected to {SOCKET}")

    def call(method, payload=b""):
        send_frame(sock, req(method, payload))
        raw = recv_frame(sock)
        if not raw:
            print(f"  NO RESPONSE for {method}")
            return None
        mt, seq, m, p = unpack(raw)
        print(f"  seq={seq} method={m} msg_type={mt} ({len(raw)} bytes)")
        return p

    # Helper: parse a response table from payload bytes
    def parse(typ, raw):
        obj = typ()
        obj.Init(raw, 0)
        return obj

    # 1. transport.play
    print("\n1. transport.play")
    b = flatbuffers.Builder(64)
    rmms.TransportPlayRequest.TransportPlayRequestStart(b)
    r = rmms.TransportPlayRequest.TransportPlayRequestEnd(b)
    b.Finish(r)
    call("transport.play", b.Output())

    # 2. transport.get_state
    print("\n2. transport.get_state")
    b = flatbuffers.Builder(64)
    rmms.TransportGetStateRequest.TransportGetStateRequestStart(b)
    r = rmms.TransportGetStateRequest.TransportGetStateRequestEnd(b)
    b.Finish(r)
    raw = call("transport.get_state", b.Output())
    if raw:
        resp = rmms.TransportGetStateResponse.TransportGetStateResponse.GetRootAs(raw, 0)
        print(f"   state={resp.State()} position={resp.Position()}")

    # 3. track.list
    print("\n3. track.list")
    b = flatbuffers.Builder(64)
    rmms.TrackListRequest.TrackListRequestStart(b)
    r = rmms.TrackListRequest.TrackListRequestEnd(b)
    b.Finish(r)
    raw = call("track.list", b.Output())
    if raw:
        resp = rmms.TrackListResponse.TrackListResponse.GetRootAs(raw, 0)
        print(f"   Tracks: {resp.TracksLength()}")
        for i in range(resp.TracksLength()):
            t = resp.Tracks(i)
            print(f"     [{t.Id().decode()}] {t.Name().decode()} type={t.Type()}")

    # 4. track.add
    print("\n4. track.add")
    b = flatbuffers.Builder(128)
    n = b.CreateString("Piano")
    rmms.TrackAddRequest.TrackAddRequestStart(b)
    rmms.TrackAddRequest.TrackAddRequestAddType(b, rmms.TrackType.TrackType.INSTRUMENT)
    rmms.TrackAddRequest.TrackAddRequestAddName(b, n)
    r = rmms.TrackAddRequest.TrackAddRequestEnd(b)
    b.Finish(r)
    raw = call("track.add", b.Output())
    if raw:
        resp = rmms.TrackAddResponse.TrackAddResponse.GetRootAs(raw, 0)
        rid = resp.TrackId().decode() if resp.TrackId() else "(none)"
        print(f"   New track ID: {rid}  success={resp.Status().Success()}")

    # 5. track.set_volume
    print("\n5. track.set_volume")
    raw = call("track.list", b.Output())
    if raw:
        resp = rmms.TrackListResponse.TrackListResponse.GetRootAs(raw, 0)
        if resp.TracksLength() > 0:
            first_id = resp.Tracks(0).Id().decode()
            print(f"   Target track: {first_id}")
            b = flatbuffers.Builder(128)
            tid = b.CreateString(first_id)
            rmms.TrackSetVolumeRequest.TrackSetVolumeRequestStart(b)
            rmms.TrackSetVolumeRequest.TrackSetVolumeRequestAddTrackId(b, tid)
            rmms.TrackSetVolumeRequest.TrackSetVolumeRequestAddVolume(b, 0.5)
            r2 = rmms.TrackSetVolumeRequest.TrackSetVolumeRequestEnd(b)
            b.Finish(r2)
            call("track.set_volume", b.Output())

    # 6. mixer.list_channels
    print("\n6. mixer.list_channels")
    b = flatbuffers.Builder(64)
    rmms.MixerListChannelsRequest.MixerListChannelsRequestStart(b)
    r = rmms.MixerListChannelsRequest.MixerListChannelsRequestEnd(b)
    b.Finish(r)
    raw = call("mixer.list_channels", b.Output())
    if raw:
        resp = rmms.MixerListChannelsResponse.MixerListChannelsResponse.GetRootAs(raw, 0)
        print(f"   Channels: {resp.ChannelsLength()}")
        for i in range(resp.ChannelsLength()):
            ch = resp.Channels(i)
            print(f"     [{ch.Id().decode()}] {ch.Name().decode()} vol={ch.Volume():.2f}")

    # 7. plugin.list
    print("\n7. plugin.list")
    b = flatbuffers.Builder(64)
    rmms.PluginListRequest.PluginListRequestStart(b)
    r = rmms.PluginListRequest.PluginListRequestEnd(b)
    b.Finish(r)
    raw = call("plugin.list", b.Output())
    if raw:
        resp = rmms.PluginListResponse.PluginListResponse.GetRootAs(raw, 0)
        print(f"   Plugins: {resp.PluginsLength()}")
        for i in range(resp.PluginsLength()):
            pl = resp.Plugins(i)
            print(f"     [{pl.Id().decode()}] {pl.Name().decode()}")

    # 8. project.get_state
    print("\n8. project.get_state")
    b = flatbuffers.Builder(64)
    rmms.ProjectGetStateRequest.ProjectGetStateRequestStart(b)
    r = rmms.ProjectGetStateRequest.ProjectGetStateRequestEnd(b)
    b.Finish(r)
    raw = call("project.get_state", b.Output())
    if raw:
        resp = rmms.ProjectGetStateResponse.ProjectGetStateResponse.GetRootAs(raw, 0)
        proj = resp.Project()
        if proj:
            print(f"   Project: {proj.Name().decode()} bpm={proj.Bpm()}")
        print(f"   Track count: {resp.TracksLength()}")

    # 9. subscription.subscribe + event delivery
    print("\n9. subscription.subscribe -> position_changed events")
    b = flatbuffers.Builder(128)
    ev1 = b.CreateString("transport.position_changed")
    rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestStartEventsVector(b, 1)
    b.PrependUOffsetTRelative(ev1)
    evs = b.EndVector()
    rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestStart(b)
    rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestAddEvents(b, evs)
    r = rmms.SubscriptionSubscribeRequest.SubscriptionSubscribeRequestEnd(b)
    b.Finish(r)
    call("subscription.subscribe", b.Output())

    # After transport.play (already PLAYING from test 1), events should flow.
    events_seen = []
    sock.settimeout(1.0)
    try:
        for _ in range(10):
            raw = recv_frame(sock)
            if not raw: break
            mt, seq, m, p = unpack(raw)
            if mt == rmms.MsgType.MsgType.EVENT:
                events_seen.append(m)
                print(f"   EVENT: {m} ({len(raw)} bytes)")
    except socket.timeout:
        pass
    sock.settimeout(5)
    if events_seen:
        print(f"   Received {len(events_seen)} events")
    else:
        print("   WARNING: no events received after subscribe")
        return 1

    # 10. unsubscribe stops event delivery
    print("\n10. subscription.unsubscribe -> events stop")
    b = flatbuffers.Builder(128)
    ev1 = b.CreateString("transport.position_changed")
    rmms.SubscriptionUnsubscribeRequest.SubscriptionUnsubscribeRequestStartEventsVector(b, 1)
    b.PrependUOffsetTRelative(ev1)
    evs = b.EndVector()
    rmms.SubscriptionUnsubscribeRequest.SubscriptionUnsubscribeRequestStart(b)
    rmms.SubscriptionUnsubscribeRequest.SubscriptionUnsubscribeRequestAddEvents(b, evs)
    r = rmms.SubscriptionUnsubscribeRequest.SubscriptionUnsubscribeRequestEnd(b)
    b.Finish(r)
    call("subscription.unsubscribe", b.Output())

    sock.settimeout(1.0)
    # Drain any in-flight events that were already queued before unsubscribe.
    try:
        for _ in range(20):
            raw = recv_frame(sock)
            if not raw: break
    except socket.timeout:
        pass
    got_event = False
    try:
        for _ in range(10):
            raw = recv_frame(sock)
            if not raw: break
            mt, seq, m, p = unpack(raw)
            if mt == rmms.MsgType.MsgType.EVENT:
                got_event = True
                print(f"   UNEXPECTED EVENT: {m}")
    except socket.timeout:
        pass
    sock.settimeout(5)
    if got_event:
        print("   FAIL: still receiving events after unsubscribe")
        return 1
    print("   No events after unsubscribe (as expected)")

    # 11. reconnect: server must survive disconnect + accept new clients
    print("\n11. reconnect (server must stay alive)")
    sock.close()
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(SOCKET)
    print("   Reconnected")

    b = flatbuffers.Builder(64)
    rmms.TransportGetStateRequest.TransportGetStateRequestStart(b)
    r = rmms.TransportGetStateRequest.TransportGetStateRequestEnd(b)
    b.Finish(r)
    raw = call("transport.get_state", b.Output())
    if not raw:
        print("   FAIL: no response after reconnect")
        return 1
    resp = rmms.TransportGetStateResponse.TransportGetStateResponse.GetRootAs(raw, 0)
    print(f"   state={resp.State()} position={resp.Position()} (server alive)")

    sock.close()
    print("\nDone.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
