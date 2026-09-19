#include "protocol/handler.h"
#include "core/iproject_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::IProjectState>;
namespace c = rmms::backend::core;

static flatbuffers::Offset<rmms::StatusResponse> ok(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, true);
}

static flatbuffers::Offset<rmms::StatusResponse> err(flatbuffers::FlatBufferBuilder& fbb,
                                                     const char* code, const char* msg) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString(code), fbb.CreateString(msg));
}

void register_track_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("track.add", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackAddRequest>(req.payload()->data());
        if (!r) { resp.Finish(rmms::CreateTrackAddResponse(resp, err(resp, "BAD_REQ", "invalid"))); return; }
        auto id = s->track_add(r->type(), r->name()->string_view());
        auto sid = resp.CreateString(id);
        resp.Finish(rmms::CreateTrackAddResponse(resp, ok(resp), sid));
    });

    r.register_handler("track.remove", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackRemoveRequest>(req.payload()->data());
        bool ok = r && s->track_remove(r->track_id()->string_view());
        resp.Finish(rmms::CreateTrackRemoveResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.list", [s](uint32_t, auto&, auto& resp) {
        auto tracks = s->track_list();
        std::vector<flatbuffers::Offset<rmms::Track>> offsets;
        for (auto* t : tracks)
            offsets.push_back(c::build_track(resp, *t));
        auto vec = resp.CreateVector(offsets);
        resp.Finish(rmms::CreateTrackListResponse(resp, vec));
    });

    r.register_handler("track.get", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackGetRequest>(req.payload()->data());
        auto* t = r ? s->track_get(r->track_id()->string_view()) : nullptr;
        auto track_off = t ? c::build_track(resp, *t) : flatbuffers::Offset<rmms::Track>();
        resp.Finish(rmms::CreateTrackGetResponse(resp,
            t ? ok(resp) : err(resp, "NOT_FOUND", "track not found"), track_off));
    });

    r.register_handler("track.set_name", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetNameRequest>(req.payload()->data());
        bool ok = r && s->track_set_name(r->track_id()->string_view(),
                                         r->name()->string_view());
        resp.Finish(rmms::CreateTrackSetNameResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_volume", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetVolumeRequest>(req.payload()->data());
        bool ok = r && s->track_set_volume(r->track_id()->string_view(), r->volume());
        resp.Finish(rmms::CreateTrackSetVolumeResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_pan", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetPanRequest>(req.payload()->data());
        bool ok = r && s->track_set_pan(r->track_id()->string_view(), r->pan());
        resp.Finish(rmms::CreateTrackSetPanResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_mute", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetMuteRequest>(req.payload()->data());
        bool ok = r && s->track_set_mute(r->track_id()->string_view(), r->mute());
        resp.Finish(rmms::CreateTrackSetMuteResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_solo", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetSoloRequest>(req.payload()->data());
        bool ok = r && s->track_set_solo(r->track_id()->string_view(), r->solo());
        resp.Finish(rmms::CreateTrackSetSoloResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_arm", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetArmRequest>(req.payload()->data());
        bool ok = r && s->track_set_arm(r->track_id()->string_view(), r->arm());
        resp.Finish(rmms::CreateTrackSetArmResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });

    r.register_handler("track.set_color", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TrackSetColorRequest>(req.payload()->data());
        bool ok = r && s->track_set_color(r->track_id()->string_view(), r->color());
        resp.Finish(rmms::CreateTrackSetColorResponse(resp,
            ok ? ::ok(resp) : err(resp, "NOT_FOUND", "track not found")));
    });
}
