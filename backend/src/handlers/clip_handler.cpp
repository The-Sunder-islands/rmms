#include "protocol/handler.h"
#include "core/project_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::ProjectState>;
namespace c = rmms::backend::core;

static flatbuffers::Offset<rmms::StatusResponse> ok(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, true);
}
static flatbuffers::Offset<rmms::StatusResponse> not_found(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("clip not found"));
}

void register_clip_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("clip.add", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipAddRequest>(req.payload()->data());
        if (!cr) { resp.Finish(rmms::CreateClipAddResponse(resp, ok(resp))); return; }
        auto id = s->clip_add(cr->track_id()->string_view(), cr->type(),
                              cr->start_tick(), cr->length_ticks());
        resp.Finish(rmms::CreateClipAddResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("clip.remove", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipRemoveRequest>(req.payload()->data());
        bool ok = cr && s->clip_remove(cr->clip_id()->string_view());
        resp.Finish(rmms::CreateClipRemoveResponse(resp, ok ? ::ok(resp) : not_found(resp)));
    });

    r.register_handler("clip.move", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipMoveRequest>(req.payload()->data());
        auto* c = cr ? s->clip_get(cr->clip_id()->string_view()) : nullptr;
        if (c) {
            if (s->track_get(cr->track_id()->string_view())) {
                c->track_id = cr->track_id()->str();
                c->start_tick = cr->start_tick();
            }
        }
        resp.Finish(rmms::CreateClipMoveResponse(resp, c ? ok(resp) : not_found(resp)));
    });

    r.register_handler("clip.resize", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipResizeRequest>(req.payload()->data());
        bool ok = cr && s->clip_get(cr->clip_id()->string_view()) &&
                  (s->clip_get(cr->clip_id()->string_view())->length_ticks = cr->length_ticks(), true);
        resp.Finish(rmms::CreateClipResizeResponse(resp, ok ? ::ok(resp) : not_found(resp)));
    });

    r.register_handler("clip.set_loop", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipSetLoopRequest>(req.payload()->data());
        bool ok = cr && s->clip_get(cr->clip_id()->string_view()) &&
                  (s->clip_get(cr->clip_id()->string_view())->loop_start = cr->loop_start(),
                   s->clip_get(cr->clip_id()->string_view())->loop_end = cr->loop_end(), true);
        resp.Finish(rmms::CreateClipSetLoopResponse(resp, ok ? ::ok(resp) : not_found(resp)));
    });

    r.register_handler("clip.split", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipSplitRequest>(req.payload()->data());
        if (!cr) { resp.Finish(rmms::CreateClipSplitResponse(resp, not_found(resp))); return; }
        auto new_id = s->clip_split(cr->clip_id()->string_view(), cr->split_tick());
        resp.Finish(rmms::CreateClipSplitResponse(resp,
            new_id.empty() ? not_found(resp) : ok(resp), resp.CreateString(new_id)));
    });

    r.register_handler("clip.list", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipListRequest>(req.payload()->data());
        std::vector<flatbuffers::Offset<rmms::Clip>> offsets;
        if (cr)
            for (auto* cl : s->clip_list(cr->track_id()->string_view()))
                offsets.push_back(c::build_clip(resp, *cl));
        resp.Finish(rmms::CreateClipListResponse(resp, resp.CreateVector(offsets)));
    });

    r.register_handler("clip.get", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ClipGetRequest>(req.payload()->data());
        auto* cl = cr ? s->clip_get(cr->clip_id()->string_view()) : nullptr;
        auto off = cl ? c::build_clip(resp, *cl) : flatbuffers::Offset<rmms::Clip>();
        resp.Finish(rmms::CreateClipGetResponse(resp, cl ? ok(resp) : not_found(resp), off));
    });
}
