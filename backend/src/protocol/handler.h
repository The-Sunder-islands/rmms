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
        const Envelope& request_envelope,
        flatbuffers::FlatBufferBuilder& response_builder)>;

    void register_handler(std::string_view method, HandlerFunc func);

    const HandlerFunc* find_handler(std::string_view method) const;

    bool has_handler(std::string_view method) const;

    std::vector<std::string_view> registered_methods() const;

private:
    std::unordered_map<std::string, HandlerFunc> m_handlers;
};

}  // namespace rmms::backend::protocol
