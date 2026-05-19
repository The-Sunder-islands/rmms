#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rmms::backend::transport {

class UnixSocketServer {
public:
    explicit UnixSocketServer(std::string_view socket_path = "/tmp/rmms.sock");
    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;
    UnixSocketServer(UnixSocketServer&&) noexcept;
    UnixSocketServer& operator=(UnixSocketServer&&) noexcept;

    using MessageHandler = std::function<void(const std::vector<uint8_t>&)>;

    void run(MessageHandler on_message);

    void stop();

    void send(const std::vector<uint8_t>& data);
    void send(std::span<const uint8_t> data);

    bool is_connected() const;

private:
    void cleanup();
    bool read_exact(void* buf, size_t n);
    std::vector<uint8_t> read_frame();

    int                          m_listen_fd;
    int                          m_client_fd;
    std::string                  m_path;
    bool                         m_running;
    bool                         m_connected;

    static constexpr size_t      k_frame_header_size = 4;
};

}  // namespace rmms::backend::transport
