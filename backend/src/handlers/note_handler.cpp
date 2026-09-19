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
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("note not found"));
}

void register_note_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("note.add", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteAddRequest>(req.payload()->data());
        if (!nr) { resp.Finish(rmms::CreateNoteAddResponse(resp, ok(resp))); return; }
        auto id = s->note_add(nr->clip_id()->string_view(), nr->key(),
                              nr->start_tick(), nr->length_ticks(),
                              nr->velocity(), nr->pan());
        resp.Finish(rmms::CreateNoteAddResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("note.remove", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteRemoveRequest>(req.payload()->data());
        bool ok = nr && s->note_remove(nr->note_id()->string_view());
        resp.Finish(rmms::CreateNoteRemoveResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });

    r.register_handler("note.move", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteMoveRequest>(req.payload()->data());
        bool found = nr && s->note_move(nr->note_id()->string_view(),
                                        nr->key(), nr->start_tick());
        resp.Finish(rmms::CreateNoteMoveResponse(resp, found ? ok(resp) : nf(resp)));
    });

    r.register_handler("note.set_length", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteSetLengthRequest>(req.payload()->data());
        bool found = nr && s->note_set_length(nr->note_id()->string_view(),
                                              nr->length_ticks());
        resp.Finish(rmms::CreateNoteSetLengthResponse(resp, found ? ok(resp) : nf(resp)));
    });

    r.register_handler("note.set_velocity", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteSetVelocityRequest>(req.payload()->data());
        bool found = nr && s->note_set_velocity(nr->note_id()->string_view(),
                                                nr->velocity());
        resp.Finish(rmms::CreateNoteSetVelocityResponse(resp, found ? ok(resp) : nf(resp)));
    });

    r.register_handler("note.list", [s](uint32_t, auto& req, auto& resp) {
        auto* nr = flatbuffers::GetRoot<rmms::NoteListRequest>(req.payload()->data());
        std::vector<flatbuffers::Offset<rmms::Note>> offsets;
        if (nr)
            for (auto* n : s->note_list(nr->clip_id()->string_view()))
                offsets.push_back(c::build_note(resp, *n));
        resp.Finish(rmms::CreateNoteListResponse(resp, resp.CreateVector(offsets)));
    });
}
