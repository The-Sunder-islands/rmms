#include "protocol/server.h"
#include "protocol/handler.h"
#include "protocol/subscription.h"
#include "rmms_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <cstring>
#include <memory>
#include <span>

namespace rmms::backend::protocol {

ProtocolServer::ProtocolServer(
    std::string_view socket_path,
    std::shared_ptr<HandlerRegistry> registry,
    std::shared_ptr<SubscriptionManager> subscriptions)
    : m_socket(socket_path)
    , m_registry(std::move(registry))
    , m_subscriptions(std::move(subscriptions))
    , m_socket_path(socket_path)
    , m_running(false)
{
}

ProtocolServer::~ProtocolServer() {
    stop();
}

void ProtocolServer::start() {
    m_running = true;

    m_socket.run(
        [this](uint32_t client_id, const std::vector<uint8_t>& frame) {
            handle_message(client_id, frame);
        },
        [this](uint32_t client_id) {
            on_client_disconnect(client_id);
        });

    m_running = false;
}

void ProtocolServer::stop() {
    m_running = false;
    m_socket.stop();
}

void ProtocolServer::signal_stop() {
    m_running = false;
    m_socket.signal_stop();
}

void ProtocolServer::push_event(std::string_view event_name,
                                const uint8_t* payload, size_t payload_size)
{
    if (!is_running() || client_count() == 0) return;

    if (event_name.empty() || !m_subscriptions) {
        // Broadcast to everyone.
        send_event_broadcast(event_name, payload, payload_size);
        return;
    }

    for (uint32_t client_id : m_subscriptions->subscribers(event_name))
        send_event(client_id, 0, event_name, payload, payload_size);
}

void ProtocolServer::push_event_to(uint32_t client_id, std::string_view event_name,
                                   const uint8_t* payload, size_t payload_size,
                                   uint32_t seq_id)
{
    if (!is_running()) return;
    send_event(client_id, seq_id, event_name, payload, payload_size);
}

void ProtocolServer::handle_message(uint32_t client_id, const std::vector<uint8_t>& frame) {
    auto verifier = flatbuffers::Verifier(frame.data(), frame.size());
    if (!rmms::VerifyEnvelopeBuffer(verifier)) return;

    auto* env = rmms::GetEnvelope(frame.data());
    if (!env || !env->method()) return;

    std::string_view method(env->method()->c_str(), env->method()->size());
    auto msg_type = env->msg_type();
    uint32_t seq_id = env->seq_id();

    if (msg_type != rmms::MsgType_REQUEST) return;

    auto* handler = m_registry->find_handler(method);
    if (!handler) {
        send_error(client_id, seq_id, method, "METHOD_NOT_FOUND",
                   "no handler registered for this method");
        return;
    }

    flatbuffers::FlatBufferBuilder builder(4096);
    try {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        (*handler)(client_id, *env, builder);
    } catch (const std::exception& e) {
        send_error(client_id, seq_id, method, "HANDLER_ERROR", e.what());
        return;
    }

    send_response(client_id, seq_id, method,
                  builder.GetBufferPointer(), builder.GetSize());
}

void ProtocolServer::on_client_disconnect(uint32_t client_id) {
    if (m_subscriptions)
        m_subscriptions->unsubscribe_all(client_id);
}

void ProtocolServer::send_response(uint32_t client_id, uint32_t seq_id,
                                   std::string_view method,
                                   const uint8_t* payload, size_t payload_size)
{
    m_socket.send(client_id,
                  build_envelope(rmms::MsgType_RESPONSE, seq_id, method,
                                 payload, payload_size));
}

void ProtocolServer::send_event(uint32_t client_id, uint32_t seq_id,
                                std::string_view event_name,
                                const uint8_t* payload, size_t payload_size)
{
    m_socket.send(client_id, build_envelope(rmms::MsgType_EVENT, seq_id,
                                            event_name, payload, payload_size));
}

void ProtocolServer::send_event_broadcast(std::string_view event_name,
                                          const uint8_t* payload, size_t payload_size)
{
    m_socket.send_all(build_envelope(rmms::MsgType_EVENT, 0,
                                     event_name, payload, payload_size));
}

std::vector<uint8_t> ProtocolServer::build_envelope(
    rmms::MsgType msg_type, uint32_t seq_id, std::string_view method,
    const uint8_t* payload, size_t payload_size)
{
    flatbuffers::FlatBufferBuilder builder(4096 + payload_size);
    auto method_str = builder.CreateString(method);
    auto payload_vec = builder.CreateVector(payload, payload_size);

    auto envelope = rmms::CreateEnvelope(
        builder, msg_type, seq_id, method_str, payload_vec);

    builder.Finish(envelope);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

void ProtocolServer::send_error(uint32_t client_id, uint32_t seq_id,
                                std::string_view method,
                                std::string_view code, std::string_view message)
{
    flatbuffers::FlatBufferBuilder builder(512);
    auto success = false;
    auto error_code = builder.CreateString(code);
    auto error_message = builder.CreateString(message);

    auto status = rmms::CreateStatusResponse(builder, success, error_code, error_message);
    builder.Finish(status);

    send_response(client_id, seq_id, method,
                  builder.GetBufferPointer(), builder.GetSize());
}

bool ProtocolServer::is_running() const {
    return m_running;
}

size_t ProtocolServer::client_count() const {
    return m_socket.client_count();
}

}  // namespace rmms::backend::protocol
