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

void register_arranger_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("arranger.add_section", [s](uint32_t, auto& req, auto& resp) {
        auto* ar = flatbuffers::GetRoot<rmms::ArrangerAddSectionRequest>(req.payload()->data());
        if (!ar) { resp.Finish(rmms::CreateArrangerAddSectionResponse(resp, ok(resp))); return; }
        auto id = s->arranger_add(ar->track_id()->string_view(), ar->name()->string_view(),
                                  ar->start_tick(), ar->length_ticks(),
                                  ar->color(), ar->repeat_count());
        resp.Finish(rmms::CreateArrangerAddSectionResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("arranger.remove_section", [s](uint32_t, auto& req, auto& resp) {
        auto* ar = flatbuffers::GetRoot<rmms::ArrangerRemoveSectionRequest>(req.payload()->data());
        bool ok = ar && s->arranger_remove(ar->section_id()->string_view());
        resp.Finish(rmms::CreateArrangerRemoveSectionResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });

    r.register_handler("arranger.list_sections", [s](uint32_t, auto&, auto& resp) {
        std::vector<flatbuffers::Offset<rmms::ArrangerSection>> offs;
        for (auto* a : s->arranger_list())
            offs.push_back(c::build_arranger(resp, *a));
        resp.Finish(rmms::CreateArrangerListSectionsResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("arranger.update_section", [s](uint32_t, auto& req, auto& resp) {
        auto* ar = flatbuffers::GetRoot<rmms::ArrangerUpdateSectionRequest>(req.payload()->data());
        auto* a = ar ? s->arranger_get(ar->section_id()->string_view()) : nullptr;
        if (a) { a->name = ar->name()->str(); a->start_tick = ar->start_tick();
                 a->length_ticks = ar->length_ticks(); a->color = ar->color();
                 a->repeat_count = ar->repeat_count(); }
        resp.Finish(rmms::CreateArrangerUpdateSectionResponse(resp, a ? ok(resp) : nf(resp)));
    });
}
