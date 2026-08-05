#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "transport/unix_socket.h"
#include "rmms_generated.h"

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

    // Async-signal-safe: wakes up start() so it returns promptly.
    void signal_stop();

    // Push an event to all clients subscribed to `event_name`.
    // If `event_name` is empty, pushes to all connected clients.
    // Thread-safe: may be called from any thread (e.g. the audio/event thread).
    void push_event(std::string_view event_name,
                    const uint8_t* payload, size_t payload_size);

    // Push an event to a single client regardless of subscription.
    void push_event_to(uint32_t client_id, std::string_view event_name,
                       const uint8_t* payload, size_t payload_size);

    bool is_running() const;

    size_t client_count() const;

private:
    void handle_message(uint32_t client_id, const std::vector<uint8_t>& frame);
    void on_client_disconnect(uint32_t client_id);
    void send_response(uint32_t client_id, uint32_t seq_id, std::string_view method,
                       const uint8_t* payload, size_t payload_size);
    void send_event(uint32_t client_id, std::string_view event_name,
                    const uint8_t* payload, size_t payload_size);
    void send_event_broadcast(std::string_view event_name,
                              const uint8_t* payload, size_t payload_size);
    void send_error(uint32_t client_id, uint32_t seq_id, std::string_view method,
                    std::string_view code, std::string_view message);

    static std::vector<uint8_t> build_envelope(
        rmms::MsgType msg_type, uint32_t seq_id, std::string_view method,
        const uint8_t* payload, size_t payload_size);

    transport::UnixSocketServer              m_socket;
    std::shared_ptr<HandlerRegistry>        m_registry;
    std::shared_ptr<SubscriptionManager>    m_subscriptions;
    std::string                             m_socket_path;
    bool                                    m_running;
    // Serializes handler execution across client reader threads. ProjectState
    // is shared mutable state; DAW control-plane requests are fast, so a
    // single lock is sufficient and keeps handlers free of locking concerns.
    std::mutex                              m_handler_mutex;
};

}  // namespace rmms::backend::protocol
