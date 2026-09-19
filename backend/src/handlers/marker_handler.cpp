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

void register_marker_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("marker.add", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MarkerAddRequest>(req.payload()->data());
        if (!mr) { resp.Finish(rmms::CreateMarkerAddResponse(resp, ok(resp))); return; }
        auto id = s->marker_add(mr->track_id()->string_view(), mr->name()->string_view(),
                                mr->tick(), mr->type());
        resp.Finish(rmms::CreateMarkerAddResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("marker.remove", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MarkerRemoveRequest>(req.payload()->data());
        bool ok = mr && s->marker_remove(mr->marker_id()->string_view());
        resp.Finish(rmms::CreateMarkerRemoveResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });

    r.register_handler("marker.list", [s](uint32_t, auto&, auto& resp) {
        std::vector<flatbuffers::Offset<rmms::Marker>> offs;
        for (auto* m : s->marker_list())
            offs.push_back(c::build_marker(resp, *m));
        resp.Finish(rmms::CreateMarkerListResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("marker.update", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MarkerUpdateRequest>(req.payload()->data());
        auto* m = mr ? s->marker_get(mr->marker_id()->string_view()) : nullptr;
        if (m) { m->name = mr->name()->str(); m->tick = mr->tick(); m->type = mr->type(); }
        resp.Finish(rmms::CreateMarkerUpdateResponse(resp, m ? ok(resp) : nf(resp)));
    });
}
