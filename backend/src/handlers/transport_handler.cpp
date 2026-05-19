#include "protocol/handler.h"
#include "core/project_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>

using State = std::shared_ptr<rmms::backend::core::ProjectState>;

static flatbuffers::Offset<rmms::StatusResponse> ok(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, true);
}

void register_transport_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("transport.play", [s](auto&, auto& resp) {
        auto status = ok(resp);
        resp.Finish(rmms::CreateTransportPlayResponse(resp, status, s->transport.position));
    });

    r.register_handler("transport.stop", [s](auto&, auto& resp) {
        s->transport.state = rmms::TransportState_STOPPED;
        resp.Finish(rmms::CreateTransportStopResponse(resp, ok(resp)));
    });

    r.register_handler("transport.pause", [s](auto&, auto& resp) {
        s->transport.state = rmms::TransportState_PAUSED;
        resp.Finish(rmms::CreateTransportPauseResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_position", [s](auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetPositionRequest>(req.payload()->data());
        if (!r) { resp.Finish(rmms::CreateTransportSetPositionResponse(resp, ok(resp))); return; }
        s->transport.position = r->tick();
        resp.Finish(rmms::CreateTransportSetPositionResponse(resp, ok(resp)));
    });

    r.register_handler("transport.get_position", [s](auto&, auto& resp) {
        resp.Finish(rmms::CreateTransportGetPositionResponse(resp, s->transport.position));
    });

    r.register_handler("transport.set_tempo", [s](auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetTempoRequest>(req.payload()->data());
        if (r) s->transport.bpm = r->bpm();
        resp.Finish(rmms::CreateTransportSetTempoResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_time_sig", [s](auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetTimeSigRequest>(req.payload()->data());
        if (r) { s->transport.time_sig_num = r->numerator(); s->transport.time_sig_den = r->denominator(); }
        resp.Finish(rmms::CreateTransportSetTimeSigResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_loop", [s](auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetLoopRequest>(req.payload()->data());
        if (r) { s->transport.loop_start = r->start_tick(); s->transport.loop_end = r->end_tick(); }
        resp.Finish(rmms::CreateTransportSetLoopResponse(resp, ok(resp)));
    });

    r.register_handler("transport.get_state", [s](auto&, auto& resp) {
        resp.Finish(rmms::CreateTransportGetStateResponse(
            resp, s->transport.state, s->transport.position));
    });
}
