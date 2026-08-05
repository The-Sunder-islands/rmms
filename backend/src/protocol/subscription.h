#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rmms::backend::protocol {

class SubscriptionManager {
public:
    void subscribe(std::string_view event_name, uint32_t client_id);

    void unsubscribe(std::string_view event_name, uint32_t client_id);

    void unsubscribe_all(uint32_t client_id);

    bool is_subscribed(std::string_view event_name, uint32_t client_id) const;

    std::vector<uint32_t> subscribers(std::string_view event_name) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unordered_set<uint32_t>> m_subs;
};

}  // namespace rmms::backend::protocol
