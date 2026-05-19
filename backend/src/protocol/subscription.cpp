#include "protocol/subscription.h"

namespace rmms::backend::protocol {

void SubscriptionManager::subscribe(std::string_view event_name, uint32_t client_id) {
    m_subs[std::string(event_name)].insert(client_id);
}

void SubscriptionManager::unsubscribe(std::string_view event_name, uint32_t client_id) {
    auto it = m_subs.find(std::string(event_name));
    if (it != m_subs.end()) {
        it->second.erase(client_id);
        if (it->second.empty())
            m_subs.erase(it);
    }
}

void SubscriptionManager::unsubscribe_all(uint32_t client_id) {
    for (auto it = m_subs.begin(); it != m_subs.end(); ) {
        it->second.erase(client_id);
        if (it->second.empty())
            it = m_subs.erase(it);
        else
            ++it;
    }
}

bool SubscriptionManager::is_subscribed(
    std::string_view event_name, uint32_t client_id) const
{
    auto it = m_subs.find(std::string(event_name));
    if (it == m_subs.end()) return false;
    return it->second.count(client_id) > 0;
}

std::vector<uint32_t> SubscriptionManager::subscribers(
    std::string_view event_name) const
{
    auto it = m_subs.find(std::string(event_name));
    if (it == m_subs.end()) return {};
    return {it->second.begin(), it->second.end()};
}

}  // namespace rmms::backend::protocol
