#include "transport/unix_socket.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <span>
#include <stdexcept>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace rmms::backend::transport {

UnixSocketServer::UnixSocketServer(std::string_view socket_path)
    : m_listen_fd(-1)
    , m_path(socket_path)
    , m_running(false)
    , m_next_client_id(1)
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

    if (listen(m_listen_fd, k_listen_backlog) < 0)
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
}

UnixSocketServer::~UnixSocketServer() {
    stop();
    if (m_listen_fd >= 0) {
        close(m_listen_fd);
        m_listen_fd = -1;
    }
    unlink(m_path.c_str());
}

void UnixSocketServer::run(MessageHandler on_message, DisconnectHandler on_disconnect) {
    m_running = true;
    accept_loop(std::move(on_message), std::move(on_disconnect));
    m_running = false;
}

void UnixSocketServer::signal_stop() {
    m_running = false;
    if (m_listen_fd >= 0)
        shutdown(m_listen_fd, SHUT_RDWR);
}

void UnixSocketServer::stop() {
    m_running = false;

    if (m_listen_fd >= 0)
        shutdown(m_listen_fd, SHUT_RDWR);

    // Kick all connected clients so their reader threads exit.
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (auto& [id, client] : m_clients)
            clients.push_back(client);
    }
    for (auto& client : clients) {
        if (client->fd >= 0)
            shutdown(client->fd, SHUT_RDWR);
    }

    // Join every thread, active or already retired. join() is idempotent
    // (joinable() becomes false afterwards), so repeated stop() is safe.
    for (auto& client : clients) {
        if (client->thread.joinable())
            client->thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (auto& client : m_retired) {
            if (client->thread.joinable())
                client->thread.join();
        }
        m_retired.clear();
    }
}

void UnixSocketServer::accept_loop(MessageHandler on_message,
                                   DisconnectHandler on_disconnect)
{
    while (m_running) {
        sockaddr_un client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int fd = accept(m_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (fd < 0) {
            if (errno == EINTR) {
                if (!m_running) break;  // signal_stop() woke us up
                continue;
            }
            if (!m_running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto client = std::make_shared<Client>();
        client->id = m_next_client_id++;
        client->fd = fd;

        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_clients[client->id] = client;
        }

        client->thread = std::thread(&UnixSocketServer::client_loop, this,
                                     client->id, on_message, on_disconnect);
    }
}

void UnixSocketServer::client_loop(uint32_t client_id, MessageHandler on_message,
                                   DisconnectHandler on_disconnect)
{
    auto client = [this, client_id]() -> std::shared_ptr<Client> {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        auto it = m_clients.find(client_id);
        return it != m_clients.end() ? it->second : nullptr;
    }();

    if (!client) return;

    while (m_running) {
        auto frame = read_frame(client->fd);
        if (frame.empty()) {
            if (m_running) {
                // Real disconnect or error: drop the client.
                break;
            }
            break;
        }
        if (on_message)
            on_message(client_id, frame);
    }

    retire_client(client_id);
    if (on_disconnect)
        on_disconnect(client_id);
}

void UnixSocketServer::retire_client(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    auto it = m_clients.find(client_id);
    if (it == m_clients.end()) return;
    if (it->second->fd >= 0) {
        close(it->second->fd);
        it->second->fd = -1;
    }
    m_retired.push_back(it->second);
    m_clients.erase(it);
}

void UnixSocketServer::send(uint32_t client_id, const std::vector<uint8_t>& data) {
    std::shared_ptr<Client> client;
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        auto it = m_clients.find(client_id);
        if (it == m_clients.end()) return;
        client = it->second;
    }

    if (client->fd < 0) return;

    std::lock_guard<std::mutex> lock(client->write_mutex);

    uint32_t size_le = static_cast<uint32_t>(data.size());
    uint8_t header[k_frame_header_size];
    header[0] = static_cast<uint8_t>(size_le);
    header[1] = static_cast<uint8_t>(size_le >> 8);
    header[2] = static_cast<uint8_t>(size_le >> 16);
    header[3] = static_cast<uint8_t>(size_le >> 24);

    // Retry loop: handle partial writes and EINTR. Give up on hard errors
    // rather than spin on a dead connection.
    size_t written = 0;
    const size_t total = data.size() + k_frame_header_size;
    while (written < total) {
        size_t offset = written;
        const void* buf;
        size_t len;
        if (offset < k_frame_header_size) {
            buf = header + offset;
            len = std::min<size_t>(k_frame_header_size - offset, total - offset);
        } else {
            buf = data.data() + (offset - k_frame_header_size);
            len = total - offset;
        }

        ssize_t sent = ::send(client->fd, buf, len, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += static_cast<size_t>(sent);
    }
}

void UnixSocketServer::send_all(const std::vector<uint8_t>& data) {
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (auto& [id, client] : m_clients)
            clients.push_back(client);
    }
    for (auto& client : clients)
        send(client->id, data);
}

bool UnixSocketServer::is_running() const {
    return m_running;
}

size_t UnixSocketServer::client_count() const {
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    return m_clients.size();
}

bool UnixSocketServer::read_exact(int fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t remaining = n;

    while (remaining > 0) {
        ssize_t r = recv(fd, p, remaining, 0);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return false;
        }
        p += r;
        remaining -= static_cast<size_t>(r);
    }
    return true;
}

std::vector<uint8_t> UnixSocketServer::read_frame(int fd) {
    uint8_t header[k_frame_header_size];
    if (!read_exact(fd, header, k_frame_header_size))
        return {};

    uint32_t size = static_cast<uint32_t>(header[0]) |
                    (static_cast<uint32_t>(header[1]) << 8) |
                    (static_cast<uint32_t>(header[2]) << 16) |
                    (static_cast<uint32_t>(header[3]) << 24);

    if (size == 0 || size > k_max_frame_size)
        return {};

    std::vector<uint8_t> frame(size);
    if (!read_exact(fd, frame.data(), size))
        return {};

    return frame;
}

}  // namespace rmms::backend::transport
