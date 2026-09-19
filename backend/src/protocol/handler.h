#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <flatbuffers/flatbuffers.h>

namespace rmms {

struct Envelope;

}  // namespace rmms

namespace rmms::backend::protocol {

class HandlerRegistry {
public:
    using HandlerFunc = std::function<void(
        uint32_t client_id,
        const Envelope& request_envelope,
        flatbuffers::FlatBufferBuilder& response_builder)>;

    void register_handler(std::string_view method, HandlerFunc func);

    // Removes a method (no-op if absent). Used by embedders that expose only a
    // subset of the protocol, e.g. the LMMS bridge during staged bring-up.
    void unregister_handler(std::string_view method);

    const HandlerFunc* find_handler(std::string_view method) const;

    bool has_handler(std::string_view method) const;

    std::vector<std::string_view> registered_methods() const;

private:
    std::unordered_map<std::string, HandlerFunc> m_handlers;
};

}  // namespace rmms::backend::protocol
