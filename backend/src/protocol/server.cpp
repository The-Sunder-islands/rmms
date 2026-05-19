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

    m_socket.run([this](const std::vector<uint8_t>& frame) {
        handle_message(frame);
    });

    m_running = false;
}

void ProtocolServer::stop() {
    m_running = false;
    m_socket.stop();
}

void ProtocolServer::push_event(std::string_view event_name,
                                const uint8_t* payload, size_t payload_size)
{
    if (!is_running() || !m_socket.is_connected()) return;

    send_event(event_name, payload, payload_size);
}

void ProtocolServer::handle_message(const std::vector<uint8_t>& frame) {
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
        send_error(seq_id, method, "METHOD_NOT_FOUND",
                   "no handler registered for this method");
        return;
    }

    flatbuffers::FlatBufferBuilder builder(4096);
    try {
        (*handler)(*env, builder);
    } catch (const std::exception& e) {
        send_error(seq_id, method, "HANDLER_ERROR", e.what());
        return;
    }

    uint8_t* resp_data = builder.GetBufferPointer();
    size_t resp_size = builder.GetSize();

    flatbuffers::FlatBufferBuilder envelope_builder(4096 + resp_size);
    auto method_str = envelope_builder.CreateString(method);
    auto payload_vec = envelope_builder.CreateVector(resp_data, resp_size);

    auto envelope = rmms::CreateEnvelope(
        envelope_builder,
        rmms::MsgType_RESPONSE,
        seq_id,
        method_str,
        payload_vec);

    envelope_builder.Finish(envelope);
    m_socket.send(std::span(envelope_builder.GetBufferPointer(),
                            envelope_builder.GetSize()));
}

void ProtocolServer::send_response(uint32_t seq_id, std::string_view method,
                                   const uint8_t* payload, size_t payload_size)
{
    flatbuffers::FlatBufferBuilder builder(4096 + payload_size);
    auto method_str = builder.CreateString(method);
    auto payload_vec = builder.CreateVector(payload, payload_size);

    auto envelope = rmms::CreateEnvelope(
        builder,
        rmms::MsgType_RESPONSE,
        seq_id,
        method_str,
        payload_vec);

    builder.Finish(envelope);
    m_socket.send(std::span(builder.GetBufferPointer(),
                            builder.GetSize()));
}

void ProtocolServer::send_event(std::string_view event_name,
                                const uint8_t* payload, size_t payload_size)
{
    flatbuffers::FlatBufferBuilder builder(4096 + payload_size);
    auto event_str = builder.CreateString(event_name);
    auto payload_vec = builder.CreateVector(payload, payload_size);

    auto envelope = rmms::CreateEnvelope(
        builder,
        rmms::MsgType_EVENT,
        0,
        event_str,
        payload_vec);

    builder.Finish(envelope);
    m_socket.send(std::span(builder.GetBufferPointer(),
                            builder.GetSize()));
}

void ProtocolServer::send_error(uint32_t seq_id, std::string_view method,
                                std::string_view code, std::string_view message)
{
    flatbuffers::FlatBufferBuilder builder(512);
    auto success = false;
    auto error_code = builder.CreateString(code);
    auto error_message = builder.CreateString(message);

    auto status = rmms::CreateStatusResponse(builder, success, error_code, error_message);
    builder.Finish(status);

    send_response(seq_id, method,
                  builder.GetBufferPointer(), builder.GetSize());
}

bool ProtocolServer::is_running() const {
    return m_running;
}

}  // namespace rmms::backend::protocol
