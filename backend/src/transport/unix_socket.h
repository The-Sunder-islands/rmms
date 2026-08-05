#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rmms::backend::transport {

// Multi-client Unix Domain Socket server.
//
// Model:
//   - One accept loop thread accepts new connections, each connection gets a
//     unique client_id and its own reader thread.
//   - MessageHandler is invoked on the connection's reader thread.
//   - send()/send_all() are thread-safe; each connection has its own write
//     mutex, so the audio/event thread may push concurrently with the
//     protocol loop.
//   - OnDisconnectHandler is invoked from the connection's reader thread
//     when the client disconnects or is kicked.
class UnixSocketServer {
public:
    using MessageHandler = std::function<void(
        uint32_t client_id, const std::vector<uint8_t>& frame)>;
    using DisconnectHandler = std::function<void(uint32_t client_id)>;

    explicit UnixSocketServer(std::string_view socket_path = "/tmp/rmms.sock");
    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    // Blocking: accept loop until stop(). Returns when stopped.
    void run(MessageHandler on_message, DisconnectHandler on_disconnect = {});

    void stop();

    // Async-signal-safe stop: only touches the atomic flag and shuts down the
    // listening fd, so it can be called from a signal handler. Joining of
    // client threads is left to stop() / the destructor.
    void signal_stop();

    // Send to one client. No-op if the client is no longer connected.
    void send(uint32_t client_id, const std::vector<uint8_t>& data);

    // Send to all currently connected clients.
    void send_all(const std::vector<uint8_t>& data);

    bool is_running() const;
    size_t client_count() const;

private:
    struct Client {
        uint32_t    id;
        int         fd;
        std::thread thread;
        std::mutex  write_mutex;
    };

    void accept_loop(MessageHandler on_message, DisconnectHandler on_disconnect);
    void client_loop(uint32_t client_id, MessageHandler on_message,
                     DisconnectHandler on_disconnect);
    bool read_exact(int fd, void* buf, size_t n);
    std::vector<uint8_t> read_frame(int fd);
    // Move a finished client from m_clients to m_retired so its thread can be
    // joined by stop(). The Client must NOT be destroyed here: its thread is
    // the caller and destroying a joinable std::thread terminates the program.
    void retire_client(uint32_t client_id);

    int                          m_listen_fd;
    std::string                  m_path;
    std::atomic<bool>            m_running;
    uint32_t                     m_next_client_id;

    mutable std::mutex             m_clients_mutex;
    std::unordered_map<uint32_t, std::shared_ptr<Client>> m_clients;
    // Disconnected clients waiting for stop() to join their threads.
    std::vector<std::shared_ptr<Client>> m_retired;

    static constexpr size_t      k_frame_header_size = 4;
    static constexpr uint32_t    k_max_frame_size = 64 * 1024 * 1024;
    static constexpr int         k_listen_backlog = 16;
};

}  // namespace rmms::backend::transport
