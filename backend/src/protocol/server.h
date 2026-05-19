#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "transport/unix_socket.h"

namespace rmms::backend::protocol {

class HandlerRegistry;
class SubscriptionManager;

class ProtocolServer {
public:
    ProtocolServer(std::string_view socket_path,
                   std::shared_ptr<HandlerRegistry> registry,
                   std::shared_ptr<SubscriptionManager> subscriptions = nullptr);

    ~ProtocolServer();

    ProtocolServer(const ProtocolServer&) = delete;
    ProtocolServer& operator=(const ProtocolServer&) = delete;

    void start();

    void stop();

    void push_event(std::string_view event_name,
                    const uint8_t* payload, size_t payload_size);

    bool is_running() const;

private:
    void handle_message(const std::vector<uint8_t>& frame);
    void send_response(uint32_t seq_id, std::string_view method,
                       const uint8_t* payload, size_t payload_size);
    void send_event(std::string_view event_name,
                    const uint8_t* payload, size_t payload_size);
    void send_error(uint32_t seq_id, std::string_view method,
                    std::string_view code, std::string_view message);

    transport::UnixSocketServer              m_socket;
    std::shared_ptr<HandlerRegistry>        m_registry;
    std::shared_ptr<SubscriptionManager>    m_subscriptions;
    std::string                             m_socket_path;
    bool                                    m_running;
};

}  // namespace rmms::backend::protocol
