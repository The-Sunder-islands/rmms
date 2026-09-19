#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rmms::backend::ai_client {

class SseClient
{
public:
	using EventCallback = std::function<void(std::string_view event_type, std::string_view data)>;

	SseClient();
	~SseClient();

	SseClient(const SseClient&) = delete;
	SseClient& operator=(const SseClient&) = delete;

	void set_server(std::string_view host, int port);
	void set_timeout(int seconds);

	// Blocking: reads the event stream until the stream ends, the server
	// closes it, or request_stop()/disconnect() is called from another thread.
	// Safe to call repeatedly (reconnect); a stopped client never reconnects.
	void connect(std::string_view task_id, EventCallback on_event);

	// Unblocks an in-flight connect()/recv() and permanently disables further
	// reconnects. Can be called from any thread; the socket is closed by the
	// connect() thread when it returns.
	void disconnect();

	// Same as disconnect(), named for the bridge's intent.
	void request_stop() { disconnect(); }
	bool stop_requested() const { return m_stop_requested.load(); }

	bool is_connected() const { return m_connected.load(); }

private:
	int connect_to_server(const std::string& host, int port);
	void process_stream(int fd, EventCallback& on_event);
	void close_stream();

	std::atomic<int> m_fd;
	int m_timeout_sec;
	std::string m_host;
	int m_port;
	std::atomic<bool> m_connected;
	std::atomic<bool> m_stop_requested;
};

} // namespace rmms::backend::ai_client
