#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <thread>
#include <vector>

#include "ai_client/rest_client.h"
#include "ai_client/sse_client.h"
#include "core/iproject_state.h"

namespace rmms::backend::protocol {
class HandlerRegistry;
class ProtocolServer;
}  // namespace rmms::backend::protocol

namespace rmms::backend::handlers {

// Asynchronous bridge between the RMMS protocol and the AI server (Protocol B).
//
// RPC semantics: all five `ai.*` methods return an "accepted" response
// immediately; the actual REST call runs on a background worker pool, and the
// result is pushed to the requesting client as an event named
// "<method>.result" (e.g. "ai.get_capabilities.result") whose Envelope echoes
// the request seq_id, so the client can pair results with requests. The
// immediate response only acknowledges acceptance and carries no result data.
//
// SSE: after a successful task submission the bridge opens an SSE stream for
// the task on a dedicated thread and forwards partial_result / progress /
// final_result as events "ai.partial_result" / "ai.progress" /
// "ai.final_result" (seq_id 0). Streams auto-reconnect with backoff until the
// task reaches a terminal state or the task is cancelled.
class AiBridge
{
public:
    AiBridge(protocol::ProtocolServer* server,
             std::shared_ptr<core::IProjectState> state,
             std::string_view base_url = "http://127.0.0.1:8420");
    ~AiBridge();
	AiBridge(const AiBridge&) = delete;
	AiBridge& operator=(const AiBridge&) = delete;

	void register_handlers(protocol::HandlerRegistry& registry);

	void start();
	void stop();

private:
	struct PendingRequest
	{
		uint32_t client_id;
		uint32_t seq_id;
		std::string method;		 // RMMS method, e.g. "ai.submit_pipeline"
		std::string http_method; // "GET" | "POST" | "DELETE"
		std::string path;		 // REST path
		std::string body;		 // POST body (empty otherwise)
		std::string import_dir;  // ai.import_results target directory (empty = temp)
		std::string file_path;   // UPLOAD: local file to send
		std::vector<std::pair<std::string, std::string>> fields; // UPLOAD: form fields
	};

	// One active SSE session per task. The session is owned by m_sse while the
	// stream thread runs, then moved to m_sse_retired once that thread has
	// returned. The thread itself never holds a strong reference to the
	// session, so a session can never be destroyed (and its std::jthread
	// self-joined) from inside the stream thread.
	struct SseSession
	{
		std::shared_ptr<ai_client::SseClient> client;
		std::jthread thread;
	};

	bool enqueue(PendingRequest req);
	void worker_loop();
	void handle_rest_result(const PendingRequest& req, const ai_client::HttpResponse& resp);

	// ai.import_results: download task result URLs and add them to the project.
	bool import_results(const std::string& task_json, const std::string& import_dir,
						std::vector<std::string>& created_tracks,
						std::vector<std::string>& imported_files);

	void start_sse(uint32_t client_id, std::string task_id);
	void sse_loop(
		uint32_t client_id, std::string task_id, std::shared_ptr<ai_client::SseClient> client, SseSession* session);
	void cancel_sse(const std::string& task_id);

	// Join SSE threads that have already returned. Must never be called from
	// an SSE thread.
	void drain_retired();

    protocol::ProtocolServer* m_server;
    std::shared_ptr<core::IProjectState> m_state;
    ai_client::RestClient     m_rest;

	std::mutex m_queue_mutex;
	std::condition_variable m_queue_cv;
	std::deque<PendingRequest> m_queue;
	std::atomic<bool> m_running;

	std::vector<std::thread> m_workers;
	std::mutex m_sse_mutex;
	std::map<std::string, std::shared_ptr<SseSession>> m_sse;
	std::vector<std::shared_ptr<SseSession>> m_sse_retired;

	static constexpr size_t kWorkerCount = 4;
	static constexpr size_t kMaxQueueSize = 128;
};

// Registers "ai.get_capabilities", "ai.submit_pipeline", "ai.get_task_status",
// "ai.cancel_task", "ai.list_tasks".
void register_ai_bridge_handlers(protocol::HandlerRegistry& registry, AiBridge& bridge);

} // namespace rmms::backend::handlers
