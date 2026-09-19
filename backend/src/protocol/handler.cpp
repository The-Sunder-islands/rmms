#include "protocol/handler.h"
#include "rmms_generated.h"

namespace rmms::backend::protocol {

void HandlerRegistry::register_handler(std::string_view method, HandlerFunc func) {
    m_handlers[std::string(method)] = std::move(func);
}

void HandlerRegistry::unregister_handler(std::string_view method) {
    m_handlers.erase(std::string(method));
}

const HandlerRegistry::HandlerFunc* HandlerRegistry::find_handler(
    std::string_view method) const
{
    auto it = m_handlers.find(std::string(method));
    if (it != m_handlers.end())
        return &it->second;
    return nullptr;
}

bool HandlerRegistry::has_handler(std::string_view method) const {
    return m_handlers.find(std::string(method)) != m_handlers.end();
}

std::vector<std::string_view> HandlerRegistry::registered_methods() const {
    std::vector<std::string_view> methods;
    methods.reserve(m_handlers.size());
    for (const auto& [name, _] : m_handlers)
        methods.push_back(name);
    return methods;
}

}  // namespace rmms::backend::protocol
