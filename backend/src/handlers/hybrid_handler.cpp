#include "protocol/handler.h"
#include "core/iproject_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::IProjectState>;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }
static auto nf(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("not found"));
}

void register_hybrid_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("hybrid.get_clip", [s](uint32_t, auto& req, auto& resp) {
        auto* hr = flatbuffers::GetRoot<rmms::HybridGetClipRequest>(req.payload()->data());
        auto* h = hr ? s->hybrid_get(hr->clip_id()->string_view()) : nullptr;
        flatbuffers::Offset<rmms::HybridClip> hoff;
        if (h) {
            auto wf_url = resp.CreateString(h->waveform_url);
            std::vector<flatbuffers::Offset<rmms::NoteDescriptor>> nd_offsets;
            hoff = rmms::CreateHybridClip(resp,
                rmms::backend::core::build_clip(resp, h->base), wf_url,
                resp.CreateVector(nd_offsets),
                resp.CreateVector(h->freq_trajectories),
                resp.CreateVector(h->amp_trajectories),
                resp.CreateVector(h->pan_trajectories));
        }
        resp.Finish(rmms::CreateHybridGetClipResponse(resp, h ? ok(resp) : nf(resp), hoff));
    });

    r.register_handler("hybrid.update_notes", [s](uint32_t, auto& req, auto& resp) {
        auto* hr = flatbuffers::GetRoot<rmms::HybridUpdateNotesRequest>(req.payload()->data());
        if (hr && s->hybrid_get(hr->clip_id()->string_view())) {
            auto* h = s->hybrid_get(hr->clip_id()->string_view());
            if (hr->notes()) {
                auto* vec = hr->notes();
                h->notes_raw.assign(
                    reinterpret_cast<const uint8_t*>(vec->data()),
                    reinterpret_cast<const uint8_t*>(vec->data()) + vec->size() * sizeof(rmms::NoteDescriptor));
            }
        }
        resp.Finish(rmms::CreateHybridUpdateNotesResponse(resp, ok(resp)));
    });

    r.register_handler("hybrid.update_trajectories", [s](uint32_t, auto& req, auto& resp) {
        auto* hr = flatbuffers::GetRoot<rmms::HybridUpdateTrajectoriesRequest>(req.payload()->data());
        if (hr && s->hybrid_get(hr->clip_id()->string_view())) {
            auto* h = s->hybrid_get(hr->clip_id()->string_view());
            if (hr->freq_trajectories()) h->freq_trajectories.assign(
                hr->freq_trajectories()->begin(), hr->freq_trajectories()->end());
            if (hr->amp_trajectories()) h->amp_trajectories.assign(
                hr->amp_trajectories()->begin(), hr->amp_trajectories()->end());
            if (hr->pan_trajectories()) h->pan_trajectories.assign(
                hr->pan_trajectories()->begin(), hr->pan_trajectories()->end());
        }
        resp.Finish(rmms::CreateHybridUpdateTrajectoriesResponse(resp, ok(resp)));
    });
}
