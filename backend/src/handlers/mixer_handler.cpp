#include "protocol/handler.h"
#include "core/iproject_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::IProjectState>;
namespace c = rmms::backend::core;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }

void register_mixer_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("mixer.list_channels", [s](uint32_t, auto&, auto& resp) {
        auto channels = s->channel_list();
        std::vector<flatbuffers::Offset<rmms::MixerChannel>> offs;
        for (auto* ch : channels) offs.push_back(c::build_channel(resp, *ch));
        resp.Finish(rmms::CreateMixerListChannelsResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("mixer.add_channel", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MixerAddChannelRequest>(req.payload()->data());
        auto id = s->channel_add(mr ? mr->name()->string_view() : "Channel");
        resp.Finish(rmms::CreateMixerAddChannelResponse(resp, ok(resp), resp.CreateString(id)));
    });

    r.register_handler("mixer.remove_channel", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MixerRemoveChannelRequest>(req.payload()->data());
        bool ok = mr && s->channel_remove(mr->channel_id()->string_view());
        resp.Finish(rmms::CreateMixerRemoveChannelResponse(resp, ok ? ::ok(resp) :
            rmms::CreateStatusResponse(resp, false, resp.CreateString("NOT_FOUND"),
                                       resp.CreateString("channel not found"))));
    });

    r.register_handler("mixer.set_volume", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MixerSetVolumeRequest>(req.payload()->data());
        if (mr && s->channel_get(mr->channel_id()->string_view()))
            s->channel_get(mr->channel_id()->string_view())->volume = mr->volume();
        resp.Finish(rmms::CreateMixerSetVolumeResponse(resp, ok(resp)));
    });

    r.register_handler("mixer.set_pan", [s](uint32_t, auto& req, auto& resp) {
        auto* mr = flatbuffers::GetRoot<rmms::MixerSetPanRequest>(req.payload()->data());
        if (mr && s->channel_get(mr->channel_id()->string_view()))
            s->channel_get(mr->channel_id()->string_view())->pan = mr->pan();
        resp.Finish(rmms::CreateMixerSetPanResponse(resp, ok(resp)));
    });

    r.register_handler("mixer.set_route", [s](uint32_t, auto&, auto& resp) {
        resp.Finish(rmms::CreateMixerSetRouteResponse(resp, ok(resp)));
    });
}
