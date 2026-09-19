#include "protocol/handler.h"
#include "core/iproject_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::IProjectState>;
namespace c = rmms::backend::core;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }
static auto nf(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("not found"));
}

void register_tempo_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("tempo.add_point", [s](uint32_t, auto& req, auto& resp) {
        auto* tr = flatbuffers::GetRoot<rmms::TempoAddPointRequest>(req.payload()->data());
        if (!tr) { resp.Finish(rmms::CreateTempoAddPointResponse(resp, ok(resp))); return; }
        auto id = s->tempo_add(tr->track_id()->string_view(), tr->tick(), tr->bpm());
        resp.Finish(rmms::CreateTempoAddPointResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("tempo.remove_point", [s](uint32_t, auto& req, auto& resp) {
        auto* tr = flatbuffers::GetRoot<rmms::TempoRemovePointRequest>(req.payload()->data());
        bool ok = tr && s->tempo_remove(tr->point_id()->string_view());
        resp.Finish(rmms::CreateTempoRemovePointResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });

    r.register_handler("tempo.list_points", [s](uint32_t, auto&, auto& resp) {
        std::vector<flatbuffers::Offset<rmms::TempoPoint>> offs;
        for (auto* tp : s->tempo_list())
            offs.push_back(c::build_tempo_point(resp, *tp));
        resp.Finish(rmms::CreateTempoListPointsResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("tempo.update_point", [s](uint32_t, auto& req, auto& resp) {
        auto* tr = flatbuffers::GetRoot<rmms::TempoUpdatePointRequest>(req.payload()->data());
        auto* tp = tr ? s->tempo_get(tr->point_id()->string_view()) : nullptr;
        if (tp) { tp->tick = tr->tick(); tp->bpm = tr->bpm(); }
        resp.Finish(rmms::CreateTempoUpdatePointResponse(resp, tp ? ok(resp) : nf(resp)));
    });
}
