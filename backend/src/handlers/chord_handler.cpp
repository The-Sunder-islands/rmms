#include "protocol/handler.h"
#include "core/project_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::ProjectState>;
namespace c = rmms::backend::core;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }
static auto nf(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("not found"));
}

void register_chord_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("chord.add", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ChordAddRequest>(req.payload()->data());
        if (!cr) { resp.Finish(rmms::CreateChordAddResponse(resp, ok(resp))); return; }
        auto id = s->chord_add(cr->track_id()->string_view(), cr->tick(),
                               cr->root(), cr->type(), cr->duration());
        resp.Finish(rmms::CreateChordAddResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("chord.remove", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ChordRemoveRequest>(req.payload()->data());
        bool ok = cr && s->chord_remove(cr->chord_id()->string_view());
        resp.Finish(rmms::CreateChordRemoveResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });

    r.register_handler("chord.list", [s](uint32_t, auto&, auto& resp) {
        std::vector<flatbuffers::Offset<rmms::ChordEvent>> offs;
        for (auto* cd : s->chord_list())
            offs.push_back(c::build_chord(resp, *cd));
        resp.Finish(rmms::CreateChordListResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("chord.update", [s](uint32_t, auto& req, auto& resp) {
        auto* cr = flatbuffers::GetRoot<rmms::ChordUpdateRequest>(req.payload()->data());
        auto* cd = cr ? s->chord_get(cr->chord_id()->string_view()) : nullptr;
        if (cd) { cd->tick = cr->tick(); cd->root = cr->root();
                  cd->type = cr->type(); cd->duration = cr->duration(); }
        resp.Finish(rmms::CreateChordUpdateResponse(resp, cd ? ok(resp) : nf(resp)));
    });
}
