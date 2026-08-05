#include "protocol/handler.h"
#include "core/project_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::ProjectState>;
namespace c = rmms::backend::core;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }

void register_project_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("project.new", [s](uint32_t, auto&, auto& resp) {
        s->project.name.clear(); s->project.file_path.clear();
        s->project.modified = false;
        auto proj = rmms::CreateProject(resp,
            resp.CreateString(""),
            resp.CreateString(""),
            120.0f, 4, 4, 44100.0f, false);
        resp.Finish(rmms::CreateProjectNewResponse(resp, ok(resp), proj));
    });

    r.register_handler("project.open", [s](uint32_t, auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::ProjectOpenRequest>(req.payload()->data());
        if (pr) { s->project.name = pr->file_path()->str(); s->project.file_path = pr->file_path()->str(); }
        s->project.modified = false;
        auto proj = rmms::CreateProject(resp,
            resp.CreateString(pr ? pr->file_path()->str() : ""),
            resp.CreateString(pr ? pr->file_path()->str() : ""),
            120.0f, 4, 4, 44100.0f, false);
        resp.Finish(rmms::CreateProjectOpenResponse(resp, ok(resp), proj));
    });

    r.register_handler("project.save", [s](uint32_t, auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::ProjectSaveRequest>(req.payload()->data());
        if (pr && pr->file_path()->size()) s->project.file_path = pr->file_path()->str();
        s->project.modified = false;
        resp.Finish(rmms::CreateProjectSaveResponse(resp, ok(resp)));
    });

    r.register_handler("project.save_as", [s](uint32_t, auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::ProjectSaveAsRequest>(req.payload()->data());
        if (pr) s->project.file_path = pr->file_path()->str();
        s->project.modified = false;
        resp.Finish(rmms::CreateProjectSaveAsResponse(resp, ok(resp)));
    });

    r.register_handler("project.close", [s](uint32_t, auto&, auto& resp) {
        resp.Finish(rmms::CreateProjectCloseResponse(resp, ok(resp), s->project.modified));
    });

    r.register_handler("project.get_state", [s](uint32_t, auto&, auto& resp) {
        auto name = resp.CreateString(s->project.name);
        auto path = resp.CreateString(s->project.file_path);
        auto proj = rmms::CreateProject(resp, name, path, s->transport.bpm,
            s->transport.time_sig_num, s->transport.time_sig_den,
            s->project.sample_rate, s->project.modified);
        std::vector<flatbuffers::Offset<rmms::Track>> toffs;
        for (auto* t : s->track_list()) toffs.push_back(c::build_track(resp, *t));
        resp.Finish(rmms::CreateProjectGetStateResponse(resp, proj,
            resp.CreateVector(toffs), s->project.modified));
    });
}
