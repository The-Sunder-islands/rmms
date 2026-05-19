#include "protocol/handler.h"
#include "core/project_state.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <memory>

using State = std::shared_ptr<rmms::backend::core::ProjectState>;

static auto ok(flatbuffers::FlatBufferBuilder& fbb) { return rmms::CreateStatusResponse(fbb, true); }
static auto nf(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, false,
        fbb.CreateString("NOT_FOUND"), fbb.CreateString("plugin not found"));
}

void register_plugin_handlers(
    rmms::backend::protocol::HandlerRegistry& r, State s)
{
    r.register_handler("plugin.list", [s](auto&, auto& resp) {
        auto plugins = s->plugin_list();
        std::vector<flatbuffers::Offset<rmms::PluginDescriptor>> offs;
        for (auto* p : plugins) {
            auto id = resp.CreateString(p->id);
            auto name = resp.CreateString(p->name);
            auto path = resp.CreateString(p->path);
            auto entry = resp.CreateString(p->entry);
            auto empty_str = resp.CreateString("");
            offs.push_back(rmms::CreatePluginDescriptor(resp, id,
                p->category, path, entry, false, 0, false, false, false, false, 0,
                name, empty_str, empty_str));
        }
        resp.Finish(rmms::CreatePluginListResponse(resp, resp.CreateVector(offs)));
    });

    r.register_handler("plugin.get_descriptor", [s](auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::PluginGetDescriptorRequest>(req.payload()->data());
        auto* p = pr ? s->plugin_get(pr->plugin_id()->string_view()) : nullptr;
        flatbuffers::Offset<rmms::PluginDescriptor> off;
        if (p) {
            auto id = resp.CreateString(p->id);
            auto name = resp.CreateString(p->name);
            auto path = resp.CreateString(p->path);
            auto entry = resp.CreateString(p->entry);
            auto empty_str = resp.CreateString("");
            off = rmms::CreatePluginDescriptor(resp, id, p->category, path, entry,
                false, 0, false, false, false, false, 0, name, empty_str, empty_str);
        }
        resp.Finish(rmms::CreatePluginGetDescriptorResponse(resp,
            p ? ok(resp) : nf(resp), off));
    });

    r.register_handler("plugin.set_param", [s](auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::PluginSetParamRequest>(req.payload()->data());
        if (pr) s->plugin_set_param(pr->plugin_id()->string_view(),
                                    pr->key()->string_view(), pr->value()->string_view());
        resp.Finish(rmms::CreatePluginSetParamResponse(resp, ok(resp)));
    });

    r.register_handler("plugin.get_params", [s](auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::PluginGetParamsRequest>(req.payload()->data());
        auto json = pr ? s->plugin_get_params(pr->plugin_id()->string_view()) : "{}";
        resp.Finish(rmms::CreatePluginGetParamsResponse(resp, ok(resp), resp.CreateString(json)));
    });

    r.register_handler("plugin.enable", [s](auto& req, auto& resp) {
        auto* pr = flatbuffers::GetRoot<rmms::PluginEnableRequest>(req.payload()->data());
        bool ok = pr && s->plugin_get(pr->plugin_id()->string_view());
        if (ok) s->plugin_set_enabled(pr->plugin_id()->string_view(), pr->enabled());
        resp.Finish(rmms::CreatePluginEnableResponse(resp, ok ? ::ok(resp) : nf(resp)));
    });
}
