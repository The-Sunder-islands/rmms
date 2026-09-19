#include "protocol/handler.h"
#include "core/iproject_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>

using State = std::shared_ptr<rmms::backend::core::IProjectState>;

static flatbuffers::Offset<rmms::StatusResponse> ok(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, true);
}

void register_transport_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("transport.play", [s](uint32_t, auto&, auto& resp) {
        s->transport_play();
        auto status = ok(resp);
        resp.Finish(rmms::CreateTransportPlayResponse(resp, status, s->transport().position));
    });

    r.register_handler("transport.stop", [s](uint32_t, auto&, auto& resp) {
        s->transport_stop();
        resp.Finish(rmms::CreateTransportStopResponse(resp, ok(resp)));
    });

    r.register_handler("transport.pause", [s](uint32_t, auto&, auto& resp) {
        s->transport_pause();
        resp.Finish(rmms::CreateTransportPauseResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_position", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetPositionRequest>(req.payload()->data());
        if (!r) { resp.Finish(rmms::CreateTransportSetPositionResponse(resp, ok(resp))); return; }
        s->transport_set_position(r->tick());
        resp.Finish(rmms::CreateTransportSetPositionResponse(resp, ok(resp)));
    });

    r.register_handler("transport.get_position", [s](uint32_t, auto&, auto& resp) {
        resp.Finish(rmms::CreateTransportGetPositionResponse(resp, s->transport().position));
    });

    r.register_handler("transport.set_tempo", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetTempoRequest>(req.payload()->data());
        if (r) s->transport_set_tempo(r->bpm());
        resp.Finish(rmms::CreateTransportSetTempoResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_time_sig", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetTimeSigRequest>(req.payload()->data());
        if (r) { s->transport().time_sig_num = r->numerator(); s->transport().time_sig_den = r->denominator(); }
        resp.Finish(rmms::CreateTransportSetTimeSigResponse(resp, ok(resp)));
    });

    r.register_handler("transport.set_loop", [s](uint32_t, auto& req, auto& resp) {
        auto* r = flatbuffers::GetRoot<rmms::TransportSetLoopRequest>(req.payload()->data());
        if (r) { s->transport().loop_start = r->start_tick(); s->transport().loop_end = r->end_tick(); }
        resp.Finish(rmms::CreateTransportSetLoopResponse(resp, ok(resp)));
    });

    r.register_handler("transport.get_state", [s](uint32_t, auto&, auto& resp) {
        resp.Finish(rmms::CreateTransportGetStateResponse(
            resp, s->transport().state, s->transport().position));
    });
}
