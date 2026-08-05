#include "protocol/handler.h"
#include "protocol/subscription.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>

#include <memory>

using Subs = std::shared_ptr<rmms::backend::protocol::SubscriptionManager>;

static flatbuffers::Offset<rmms::StatusResponse> ok(flatbuffers::FlatBufferBuilder& fbb) {
    return rmms::CreateStatusResponse(fbb, true);
}

void register_subscription_handlers(
    rmms::backend::protocol::HandlerRegistry& r, Subs subs)
{
    if (!subs) return;

    r.register_handler("subscription.subscribe", [subs](uint32_t client_id, auto& req, auto& resp) {
        auto* q = flatbuffers::GetRoot<rmms::SubscriptionSubscribeRequest>(req.payload()->data());
        if (q && q->events()) {
            for (auto it = q->events()->begin(); it != q->events()->end(); ++it) {
                if (it->c_str()) subs->subscribe(it->c_str(), client_id);
            }
        }
        resp.Finish(rmms::CreateSubscriptionSubscribeResponse(resp, ok(resp)));
    });

    r.register_handler("subscription.unsubscribe", [subs](uint32_t client_id, auto& req, auto& resp) {
        auto* q = flatbuffers::GetRoot<rmms::SubscriptionUnsubscribeRequest>(req.payload()->data());
        if (q && q->events() && q->events()->size() == 0) {
            // Empty list clears all subscriptions for this client.
            subs->unsubscribe_all(client_id);
        } else if (q && q->events()) {
            for (auto it = q->events()->begin(); it != q->events()->end(); ++it) {
                if (it->c_str()) subs->unsubscribe(it->c_str(), client_id);
            }
        }
        resp.Finish(rmms::CreateSubscriptionUnsubscribeResponse(resp, ok(resp)));
    });
}
