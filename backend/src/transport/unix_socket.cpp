#include "transport/unix_socket.h"

#include <cerrno>
#include <cstring>
#include <span>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace rmms::backend::transport {

UnixSocketServer::UnixSocketServer(std::string_view socket_path)
    : m_listen_fd(-1)
    , m_client_fd(-1)
    , m_path(socket_path)
    , m_running(false)
    , m_connected(false)
{
    m_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listen_fd < 0)
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));

    unlink(m_path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (m_path.size() >= sizeof(addr.sun_path))
        throw std::runtime_error("socket path too long");
    strncpy(addr.sun_path, m_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));

    if (chmod(m_path.c_str(), 0666) < 0)
        throw std::runtime_error(std::string("chmod() failed: ") + strerror(errno));

    if (listen(m_listen_fd, 1) < 0)
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
}

UnixSocketServer::~UnixSocketServer() {
    cleanup();
}

UnixSocketServer::UnixSocketServer(UnixSocketServer&& other) noexcept
    : m_listen_fd(other.m_listen_fd)
    , m_client_fd(other.m_client_fd)
    , m_path(std::move(other.m_path))
    , m_running(other.m_running)
    , m_connected(other.m_connected)
{
    other.m_listen_fd = -1;
    other.m_client_fd = -1;
    other.m_running = false;
    other.m_connected = false;
}

UnixSocketServer& UnixSocketServer::operator=(UnixSocketServer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_listen_fd = other.m_listen_fd;
        m_client_fd = other.m_client_fd;
        m_path = std::move(other.m_path);
        m_running = other.m_running;
        m_connected = other.m_connected;
        other.m_listen_fd = -1;
        other.m_client_fd = -1;
        other.m_running = false;
        other.m_connected = false;
    }
    return *this;
}

void UnixSocketServer::run(MessageHandler on_message) {
    m_running = true;

    sockaddr_un client_addr{};
    socklen_t client_len = sizeof(client_addr);

    m_client_fd = accept(m_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (m_client_fd < 0) {
        m_running = false;
        if (errno == EINTR) return;
        throw std::runtime_error(std::string("accept() failed: ") + strerror(errno));
    }

    m_connected = true;

    while (m_running) {
        auto frame = read_frame();
        if (frame.empty()) {
            if (!m_running) break;
            if (errno == EAGAIN || errno == EINTR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            break;
        }
        on_message(frame);
    }

    if (m_client_fd >= 0) {
        close(m_client_fd);
        m_client_fd = -1;
    }
    m_connected = false;
}

void UnixSocketServer::stop() {
    m_running = false;
    if (m_client_fd >= 0) {
        shutdown(m_client_fd, SHUT_RDWR);
    }
}

void UnixSocketServer::send(const std::vector<uint8_t>& data) {
    send(std::span<const uint8_t>(data));
}

void UnixSocketServer::send(std::span<const uint8_t> data) {
    if (m_client_fd < 0) return;

    uint32_t size_le = static_cast<uint32_t>(data.size());
    uint8_t header[k_frame_header_size];
    header[0] = static_cast<uint8_t>(size_le);
    header[1] = static_cast<uint8_t>(size_le >> 8);
    header[2] = static_cast<uint8_t>(size_le >> 16);
    header[3] = static_cast<uint8_t>(size_le >> 24);

    struct msghdr msg{};
    struct iovec iov[2];

    iov[0].iov_base = header;
    iov[0].iov_len = k_frame_header_size;
    iov[1].iov_base = const_cast<uint8_t*>(data.data());
    iov[1].iov_len = data.size();

    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    ssize_t sent = sendmsg(m_client_fd, &msg, MSG_NOSIGNAL);
    (void)sent;
}

bool UnixSocketServer::is_connected() const {
    return m_connected && m_running;
}

void UnixSocketServer::cleanup() {
    m_running = false;
    if (m_client_fd >= 0) {
        close(m_client_fd);
        m_client_fd = -1;
    }
    if (m_listen_fd >= 0) {
        close(m_listen_fd);
        m_listen_fd = -1;
    }
    m_connected = false;
}

bool UnixSocketServer::read_exact(void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t remaining = n;

    while (remaining > 0) {
        ssize_t r = recv(m_client_fd, p, remaining, 0);
        if (r <= 0) {
            if (r == 0) {
                m_connected = false;
            } else if (errno == EINTR) {
                continue;
            } else {
                m_connected = false;
            }
            return false;
        }
        p += r;
        remaining -= static_cast<size_t>(r);
    }
    return true;
}

std::vector<uint8_t> UnixSocketServer::read_frame() {
    uint8_t header[k_frame_header_size];
    if (!read_exact(header, k_frame_header_size))
        return {};

    uint32_t size = static_cast<uint32_t>(header[0]) |
                    (static_cast<uint32_t>(header[1]) << 8) |
                    (static_cast<uint32_t>(header[2]) << 16) |
                    (static_cast<uint32_t>(header[3]) << 24);

    if (size == 0 || size > 64 * 1024 * 1024)  // 64 MB max
        return {};

    std::vector<uint8_t> frame(size);
    if (!read_exact(frame.data(), size))
        return {};

    return frame;
}

}  // namespace rmms::backend::transport
