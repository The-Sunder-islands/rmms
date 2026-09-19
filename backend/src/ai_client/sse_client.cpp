#include "ai_client/sse_client.h"

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/tcp.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace rmms::backend::ai_client {

SseClient::SseClient()
	: m_fd(-1)
	, m_timeout_sec(30)
	, m_host("127.0.0.1")
	, m_port(8420)
	, m_connected(false)
	, m_stop_requested(false)
{
}

SseClient::~SseClient()
{
	disconnect();
	close_stream();
}

void SseClient::set_server(std::string_view host, int port)
{
	m_host = std::string(host);
	m_port = port;
}

void SseClient::set_timeout(int seconds)
{ m_timeout_sec = seconds; }

int SseClient::connect_to_server(const std::string& host, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	struct timeval tv{};
	tv.tv_sec = m_timeout_sec;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* result = nullptr;
	std::string port_str = std::to_string(port);
	int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
	if (ret != 0 || !result)
	{
		if (result) freeaddrinfo(result);
		close(fd);
		return -1;
	}

	ret = ::connect(fd, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);
	if (ret < 0)
	{
		close(fd);
		return -1;
	}

	return fd;
}

void SseClient::connect(std::string_view task_id, EventCallback on_event)
{
	if (m_stop_requested.load() || m_connected.load()) return;

	int fd = connect_to_server(m_host, m_port);
	if (fd < 0) return;
	m_fd.store(fd);

	if (m_stop_requested.load())
	{
		close_stream();
		return;
	}

	std::string path = "/api/v1/tasks/" + std::string(task_id) + "/events";
	std::string req;
	req += "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + m_host;
	if (m_port != 80) req += ":" + std::to_string(m_port);
	req += "\r\n";
	req += "Accept: text/event-stream\r\n";
	req += "Connection: close\r\n";
	req += "Cache-Control: no-cache\r\n";
	req += "\r\n";

	size_t sent = 0;
	while (sent < req.size())
	{
		ssize_t n = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
		if (n <= 0)
		{
			close_stream();
			return;
		}
		sent += static_cast<size_t>(n);
	}

	m_connected.store(true);
	process_stream(fd, on_event);

	m_connected.store(false);
	close_stream();
}

void SseClient::close_stream()
{
	int fd = m_fd.exchange(-1);
	if (fd >= 0) ::close(fd);
}

void SseClient::process_stream(int fd, EventCallback& on_event)
{
	std::string buf;
	buf.reserve(8192);
	char raw[4096];
	std::string current_event = "message";
	std::string current_data;
	// -1 = undecided, 0 = skipping HTTP response headers, 1 = SSE body
	int header_state = -1;

	auto dispatch = [&]() {
		if (!current_data.empty()) on_event(current_event, current_data);
		current_event = "message";
		current_data.clear();
	};

	while (m_connected.load() && !m_stop_requested.load())
	{
		ssize_t n = recv(fd, raw, sizeof(raw), 0);
		if (n <= 0) break;

		buf.append(raw, static_cast<size_t>(n));

		// Line-based parsing handles both LF and CRLF streams.
		size_t pos = 0;
		while (true)
		{
			auto nl = buf.find('\n', pos);
			if (nl == std::string::npos) break;

			std::string_view line(buf.data() + pos, nl - pos);
			pos = nl + 1;
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

			if (header_state != 1)
			{
				if (header_state == -1) header_state = (line.rfind("HTTP/", 0) == 0) ? 0 : 1;
				if (header_state == 0)
				{
					// Skip status line and response headers up to the blank line.
					if (line.empty()) header_state = 1;
					continue;
				}
			}

			if (line.empty())
			{
				dispatch();
				continue;
			}
			if (line[0] == ':') // heartbeat comment
				continue;

			auto colon = line.find(':');
			if (colon == std::string_view::npos) continue;

			std::string_view fname = line.substr(0, colon);
			std::string_view fvalue = line.substr(colon + 1);
			if (!fvalue.empty() && fvalue[0] == ' ') fvalue.remove_prefix(1);

			if (fname == "event") { current_event = std::string(fvalue); }
			else if (fname == "data")
			{
				if (!current_data.empty()) current_data += '\n';
				current_data += std::string(fvalue);
			}
		}

		if (pos > 0) buf.erase(0, pos);
	}
}

void SseClient::disconnect()
{
	m_stop_requested.store(true);
	m_connected.store(false);
	int fd = m_fd.load();
	if (fd >= 0) shutdown(fd, SHUT_RDWR); // the connect() thread closes the fd
}

} // namespace rmms::backend::ai_client
