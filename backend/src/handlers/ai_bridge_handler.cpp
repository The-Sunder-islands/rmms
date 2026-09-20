#include "handlers/ai_bridge_handler.h"

#include <chrono>
#include <filesystem>
#include <flatbuffers/flatbuffers.h>
#include <string>
#include <utility>

#include "ai_client/pipeline_translator.h"
#include "protocol/handler.h"
#include "protocol/server.h"
#include "rmms_generated.h"

namespace rmms::backend::handlers {

namespace {

using protocol::HandlerRegistry;

flatbuffers::Offset<rmms::StatusResponse> ok_status(flatbuffers::FlatBufferBuilder& fbb)
{ return rmms::CreateStatusResponse(fbb, true); }

flatbuffers::Offset<rmms::StatusResponse> err_status(
	flatbuffers::FlatBufferBuilder& fbb, std::string_view code, std::string_view msg)
{
	auto c = fbb.CreateString(code);
	auto m = fbb.CreateString(msg);
	return rmms::CreateStatusResponse(fbb, false, c, m);
}

std::string error_code_from_http(int status)
{
	if (status == -2) return "FILE_NOT_FOUND";
	if (status <= 0) return "AI_SERVER_UNREACHABLE";
	if (status == 404) return "AI_NOT_FOUND";
	if (status == 429) return "AI_QUOTA_EXCEEDED";
	if (status == 503) return "AI_QUEUE_FULL";
	return "AI_SERVER_ERROR";
}

std::string completion_event(std::string_view method)
{
	std::string name(method);
	name += ".result";
	return name;
}

// Synthetic response used when the request queue is saturated.
const ai_client::HttpResponse kQueueFull{503, "", {}};

} // namespace

AiBridge::AiBridge(protocol::ProtocolServer* server,
				   std::shared_ptr<core::IProjectState> state, std::string_view base_url)
	: m_server(server)
	, m_state(std::move(state))
	, m_rest(base_url)
	, m_running(false)
{
}

AiBridge::~AiBridge()
{ stop(); }

void AiBridge::register_handlers(HandlerRegistry& registry)
{
	// ── ai.get_capabilities ────────────────────────────────────────────────
	registry.register_handler("ai.get_capabilities", [this](uint32_t client_id, auto& req, auto& resp) {
		(void)flatbuffers::GetRoot<rmms::AIGetCapabilitiesRequest>(req.payload()->data());
		resp.Finish(rmms::CreateAIGetCapabilitiesResponse(resp, ok_status(resp), 0));

		PendingRequest pr{client_id, req.seq_id(), "ai.get_capabilities", "GET", "/api/v1/capabilities", ""};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});

	// ── ai.submit_pipeline ─────────────────────────────────────────────────
	registry.register_handler("ai.submit_pipeline", [this](uint32_t client_id, auto& req, auto& resp) {
		auto* q = flatbuffers::GetRoot<rmms::AISubmitPipelineRequest>(req.payload()->data());
		auto empty = resp.CreateString("");
		resp.Finish(rmms::CreateAISubmitPipelineResponse(resp, ok_status(resp), empty, false, rmms::TaskStatus_QUEUED));

		// Local file input -> multipart upload; http(s) URL -> JSON body and
		// the AI server fetches it itself.
		std::string input = (q && q->input_url()) ? q->input_url()->str() : "";
		const bool is_url = input.rfind("http://", 0) == 0 || input.rfind("https://", 0) == 0;
		std::string file_path;
		if (!input.empty() && !is_url)
		{
			file_path = input;
			if (file_path.rfind("file://", 0) == 0)
				file_path = file_path.substr(7);
		}

		if (!file_path.empty() && q)
		{
			if (!std::filesystem::exists(file_path))
			{
				handle_rest_result(PendingRequest{client_id, req.seq_id(), "ai.submit_pipeline",
												  "POST", "/api/v1/tasks", ""},
								   ai_client::HttpResponse{-2, "local input file not found", {}});
				return;
			}
			const auto form = ai_client::PipelineTranslator::submit_pipeline_form(*q);
			std::vector<std::pair<std::string, std::string>> fields;
			fields.emplace_back("pipeline", form.steps_json);
			if (!form.device_preference.empty())
				fields.emplace_back("device_preference", form.device_preference);
			fields.emplace_back("priority", form.priority);
			if (!form.output_format.empty())
				fields.emplace_back("output_format", form.output_format);
			if (!form.output_package.empty())
				fields.emplace_back("output_package", form.output_package);
			fields.emplace_back("force_refresh", form.force_refresh);

			PendingRequest pr{client_id, req.seq_id(), "ai.submit_pipeline", "UPLOAD",
							  "/api/v1/tasks", "", "", file_path, std::move(fields)};
			if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
			return;
		}

		std::string body;
		if (q) body = ai_client::PipelineTranslator::submit_pipeline_request_to_json(*q);
		PendingRequest pr{client_id, req.seq_id(), "ai.submit_pipeline", "POST", "/api/v1/tasks", std::move(body)};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});

	// ── ai.get_task_status ─────────────────────────────────────────────────
	registry.register_handler("ai.get_task_status", [this](uint32_t client_id, auto& req, auto& resp) {
		auto* q = flatbuffers::GetRoot<rmms::AIGetTaskStatusRequest>(req.payload()->data());
		std::string path = "/api/v1/tasks/";
		if (q && q->task_id()) path += q->task_id()->str();
		auto empty = resp.CreateString("");
		resp.Finish(rmms::CreateAIGetTaskStatusResponse(
			resp, ok_status(resp), empty, rmms::TaskStatus_QUEUED, 0, empty, 0, 0, 0, 0, 0));

		PendingRequest pr{client_id, req.seq_id(), "ai.get_task_status", "GET", std::move(path), ""};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});

	// ── ai.cancel_task ─────────────────────────────────────────────────────
	registry.register_handler("ai.cancel_task", [this](uint32_t client_id, auto& req, auto& resp) {
		auto* q = flatbuffers::GetRoot<rmms::AICancelTaskRequest>(req.payload()->data());
		std::string task_id;
		std::string path = "/api/v1/tasks/";
		if (q && q->task_id())
		{
			task_id = q->task_id()->str();
			path += task_id;
		}
		resp.Finish(rmms::CreateAICancelTaskResponse(resp, ok_status(resp)));

		// Stop forwarding SSE events for this task immediately.
		cancel_sse(task_id);
		PendingRequest pr{client_id, req.seq_id(), "ai.cancel_task", "DELETE", std::move(path), ""};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});

	// ── ai.list_tasks ──────────────────────────────────────────────────────
	registry.register_handler("ai.list_tasks", [this](uint32_t client_id, auto& req, auto& resp) {
		auto* q = flatbuffers::GetRoot<rmms::AIListTasksRequest>(req.payload()->data());
		std::string path = "/api/v1/tasks?";
		bool first = true;
		if (q && q->status_filter() && q->status_filter()->size() > 0)
		{
			path += "status=";
			path += q->status_filter()->str();
			first = false;
		}
		int limit = q ? q->limit() : 20;
		int offset = q ? q->offset() : 0;
		if (limit <= 0) limit = 20;
		if (offset < 0) offset = 0;
		if (!first) path += "&";
		path += "limit=" + std::to_string(limit);
		path += "&offset=" + std::to_string(offset);
		resp.Finish(rmms::CreateAIListTasksResponse(resp, ok_status(resp), 0, 0));

		PendingRequest pr{client_id, req.seq_id(), "ai.list_tasks", "GET", std::move(path), ""};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});

	// ── ai.import_results ──────────────────────────────────────────────────
	registry.register_handler("ai.import_results", [this](uint32_t client_id, auto& req, auto& resp) {
		auto* q = flatbuffers::GetRoot<rmms::AIImportResultsRequest>(req.payload()->data());
		const std::string task_id = (q && q->task_id()) ? q->task_id()->str() : "";
		const std::string import_dir = (q && q->import_dir()) ? q->import_dir()->str() : "";
		resp.Finish(rmms::CreateAIImportResultsResponse(resp, ok_status(resp), 0, 0));

		PendingRequest pr{client_id, req.seq_id(), "ai.import_results", "GET",
						  "/api/v1/tasks/" + task_id, "", import_dir};
		if (!enqueue(pr)) handle_rest_result(pr, kQueueFull);
	});
}

void AiBridge::start()
{
	if (m_running) return;
	m_running = true;
	m_workers.reserve(kWorkerCount);
	for (size_t i = 0; i < kWorkerCount; ++i)
		m_workers.emplace_back(&AiBridge::worker_loop, this);
}

void AiBridge::stop()
{
	if (!m_running) return;
	m_running = false;
	m_queue_cv.notify_all();
	for (auto& w : m_workers)
		if (w.joinable()) w.join();
	m_workers.clear();

	// Unblock every active SSE stream; the stream threads then exit and move
	// their sessions to the retired list (see sse_loop).
	std::vector<std::shared_ptr<ai_client::SseClient>> clients;
	{
		std::lock_guard<std::mutex> lock(m_sse_mutex);
		for (auto& [id, s] : m_sse)
			clients.push_back(s->client);
	}
	for (auto& c : clients)
		c->disconnect();

	// Wait for streams to retire themselves (bounded; disconnect() guarantees
	// recv() returns and the backoff sleep is interruptible).
	for (int i = 0; i < 300; ++i)
	{
		{
			std::lock_guard<std::mutex> lock(m_sse_mutex);
			if (m_sse.empty()) break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	drain_retired();
}

bool AiBridge::enqueue(PendingRequest req)
{
	{
		std::lock_guard<std::mutex> lock(m_queue_mutex);
		if (m_queue.size() >= kMaxQueueSize) return false;
		m_queue.push_back(std::move(req));
	}
	m_queue_cv.notify_one();
	return true;
}

void AiBridge::worker_loop()
{
	while (true)
	{
		PendingRequest req;
		{
			std::unique_lock<std::mutex> lock(m_queue_mutex);
			m_queue_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });
			if (!m_running) return;
			req = std::move(m_queue.front());
			m_queue.pop_front();
		}

		ai_client::HttpResponse resp;
		if (req.http_method == "POST")
			resp = m_rest.post(req.path, req.body);
		else if (req.http_method == "UPLOAD")
			resp = m_rest.post_form(req.path, req.fields, "file", req.file_path);
		else if (req.http_method == "DELETE")
			resp = m_rest.del(req.path);
		else
			resp = m_rest.get(req.path);

		if (m_running) handle_rest_result(req, resp);
	}
}

void AiBridge::handle_rest_result(const PendingRequest& req, const ai_client::HttpResponse& resp)
{
	using namespace ai_client;
	flatbuffers::FlatBufferBuilder fbb(4096);

	bool ok = resp.status == 200 || resp.status == 201;
	auto err_code = error_code_from_http(resp.status);

	if (req.method == "ai.get_capabilities")
	{
		PipelineTranslator::capabilities_response_to_fb(resp.body, fbb, ok, err_code, resp.body.substr(0, 256));
	}
	else if (req.method == "ai.submit_pipeline")
	{
		if (ok)
		{
			auto task_id = PipelineTranslator::get_json_string(resp.body, "task_id");
			auto cached = PipelineTranslator::get_json_string(resp.body, "cached") == "true";
			auto status_str = PipelineTranslator::get_json_string(resp.body, "status");
			rmms::TaskStatus task_status = rmms::TaskStatus_QUEUED;
			if (status_str == "processing") task_status = rmms::TaskStatus_PROCESSING;
			else if (status_str == "done")
				task_status = rmms::TaskStatus_DONE;
			else if (status_str == "error")
				task_status = rmms::TaskStatus_ERROR;
			else if (status_str == "cancelled")
				task_status = rmms::TaskStatus_CANCELLED;

			auto tid = fbb.CreateString(task_id);
			auto status = ok_status(fbb);
			fbb.Finish(rmms::CreateAISubmitPipelineResponse(fbb, status, tid, cached, task_status));

			// Open the SSE event stream for this task (unless already done).
			if (!task_id.empty() && task_status != rmms::TaskStatus_DONE && task_status != rmms::TaskStatus_ERROR
				&& task_status != rmms::TaskStatus_CANCELLED)
			{
				start_sse(req.client_id, task_id);
			}
		}
		else
		{
			auto status = err_status(fbb, err_code, resp.body.substr(0, 256));
			fbb.Finish(rmms::CreateAISubmitPipelineResponse(fbb, status, 0, false, rmms::TaskStatus_ERROR));
		}
	}
	else if (req.method == "ai.get_task_status")
	{
		if (ok && PipelineTranslator::task_status_to_fb(resp.body, fbb))
		{
			// finished AIGetTaskStatusResponse
		}
		else
		{
			auto status = err_status(fbb, err_code, resp.body.substr(0, 256));
			fbb.Finish(
				rmms::CreateAIGetTaskStatusResponse(fbb, status, 0, rmms::TaskStatus_ERROR, 0, 0, 0, 0, 0, 0, 0));
		}
	}
	else if (req.method == "ai.cancel_task")
	{
		auto status = ok ? ok_status(fbb) : err_status(fbb, err_code, resp.body.substr(0, 256));
		fbb.Finish(rmms::CreateAICancelTaskResponse(fbb, status));
	}
	else if (req.method == "ai.list_tasks")
	{
		if (ok && PipelineTranslator::list_tasks_to_fb(resp.body, fbb))
		{
			// finished AIListTasksResponse
		}
		else
		{
			auto status = err_status(fbb, err_code, resp.body.substr(0, 256));
			fbb.Finish(rmms::CreateAIListTasksResponse(fbb, status, 0, 0));
		}
	}
	else if (req.method == "ai.import_results")
	{
		if (ok && m_state)
		{
			std::vector<std::string> tracks;
			std::vector<std::string> files;
			if (import_results(resp.body, req.import_dir, tracks, files))
			{
				std::vector<flatbuffers::Offset<flatbuffers::String>> track_offs;
				std::vector<flatbuffers::Offset<flatbuffers::String>> file_offs;
				for (const auto& t : tracks) track_offs.push_back(fbb.CreateString(t));
				for (const auto& f : files) file_offs.push_back(fbb.CreateString(f));
				fbb.Finish(rmms::CreateAIImportResultsResponse(
					fbb, ok_status(fbb), fbb.CreateVector(track_offs),
					fbb.CreateVector(file_offs)));
			}
			else
			{
				auto status = err_status(fbb, "IMPORT_FAILED", "no downloadable results");
				fbb.Finish(rmms::CreateAIImportResultsResponse(fbb, status, 0, 0));
			}
		}
		else
		{
			auto status = err_status(fbb, err_code, resp.body.substr(0, 256));
			fbb.Finish(rmms::CreateAIImportResultsResponse(fbb, status, 0, 0));
		}
	}
	else
	{
		return;
	}

	auto event_name = completion_event(req.method);
	m_server->push_event_to(req.client_id, event_name, fbb.GetBufferPointer(), fbb.GetSize(), req.seq_id);
}

void AiBridge::start_sse(uint32_t client_id, std::string task_id)
{
	drain_retired();

	auto session = std::make_shared<SseSession>();
	session->client = std::make_shared<ai_client::SseClient>();
	auto client = session->client;
	SseSession* raw = session.get();

	std::lock_guard<std::mutex> lock(m_sse_mutex);
	if (m_sse.count(task_id)) return; // already streaming
	session->thread
		= std::jthread([this, client_id, task_id, client, raw] { sse_loop(client_id, task_id, client, raw); });
	m_sse[task_id] = std::move(session);
}

void AiBridge::sse_loop(
	uint32_t client_id, std::string task_id, std::shared_ptr<ai_client::SseClient> client, SseSession* session)
{
	using namespace ai_client;
	std::atomic<bool> finished{false};

	auto on_event = [this, client_id, &finished](std::string_view event_type, std::string_view data) {
		flatbuffers::FlatBufferBuilder fbb(2048);
		std::string_view event_name;
		if (event_type == "partial_result" && PipelineTranslator::partial_result_to_fb(data, fbb))
		{
			event_name = "ai.partial_result";
		}
		else if (event_type == "progress" && PipelineTranslator::progress_to_fb(data, fbb))
		{
			event_name = "ai.progress";
		}
		else if (event_type == "final_result" && PipelineTranslator::final_result_to_fb(data, fbb))
		{
			event_name = "ai.final_result";
			finished = true;
		}
		else
		{
			return;
		}
		m_server->push_event_to(client_id, event_name, fbb.GetBufferPointer(), fbb.GetSize(), 0);
	};

	int retries = 0;
	while (m_running)
	{
		// If the session was cancelled or already finished, stop reconnecting.
		{
			std::lock_guard<std::mutex> lock(m_sse_mutex);
			auto it = m_sse.find(task_id);
			if (it == m_sse.end() || it->second.get() != session)
			{
				// Not tracked anymore (cancelled/stopped): leave.
				return;
			}
		}
		if (finished) break;

		client->connect(task_id, on_event); // blocks until disconnect
		if (finished || !m_running || client->stop_requested()) break;

		// Stream dropped: reconnect with backoff (1s, 2s, 4s, 8s, 16s).
		if (retries >= 5) break;
		const int total_ms = (1 << retries) * 1000;
		for (int slept = 0; slept < total_ms; slept += 50)
		{
			if (!m_running || client->stop_requested() || finished) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		retries++;
	}

	// Retire ourselves: only the last one to leave m_sse may push the session,
	// so cancel_sse/stop never see it twice.
	{
		std::lock_guard<std::mutex> lock(m_sse_mutex);
		auto it = m_sse.find(task_id);
		if (it != m_sse.end() && it->second.get() == session)
		{
			m_sse_retired.push_back(it->second);
			m_sse.erase(it);
		}
	}
	// No strong reference to the session is held on this thread; the retired
	// list owns it and drain_retired() joins this thread from outside.
}

void AiBridge::cancel_sse(const std::string& task_id)
{
	if (task_id.empty()) return;
	std::shared_ptr<ai_client::SseClient> client;
	{
		std::lock_guard<std::mutex> lock(m_sse_mutex);
		auto it = m_sse.find(task_id);
		if (it == m_sse.end()) return;
		client = it->second->client;
		m_sse_retired.push_back(it->second);
		m_sse.erase(it);
	}
	client->disconnect();
	// We are on a protocol handler thread, so it is safe to join the retired
	// stream thread.
	drain_retired();
}

void AiBridge::drain_retired()
{
	std::vector<std::shared_ptr<SseSession>> retired;
	{
		std::lock_guard<std::mutex> lock(m_sse_mutex);
		retired.swap(m_sse_retired);
	}
	// Join outside the lock: every retired session's thread has already
	// returned or is about to return without touching the session again.
	retired.clear();
}

bool AiBridge::import_results(const std::string& task_json, const std::string& import_dir,
							  std::vector<std::string>& created_tracks,
							  std::vector<std::string>& imported_files)
{
	using namespace ai_client;

	// GET /tasks/{id} -> completed_urls: [ { step_type, urls: [url, ...] } ]
	std::vector<std::string> urls;
	const auto step_objs = PipelineTranslator::json_array_objects(
		PipelineTranslator::json_get_array(task_json, "completed_urls"));
	for (const auto& step : step_objs)
	{
		for (const auto& u : PipelineTranslator::json_array_strings(
				 PipelineTranslator::json_get_array(step, "urls")))
		{
			if (!u.empty()) urls.push_back(u);
		}
	}
	if (urls.empty()) return false;

	std::string dir = import_dir;
	if (dir.empty())
	{
		const std::string task_id = PipelineTranslator::get_json_string(task_json, "task_id");
		dir = "/tmp/rmms-import/" + (task_id.empty() ? std::string("task") : task_id);
	}
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec) return false;

	for (const auto& url : urls)
	{
		// Strip scheme://host so RestClient (which owns host/port) can fetch it.
		std::string path = url;
		if (const auto scheme = path.find("://"); scheme != std::string::npos)
		{
			const auto slash = path.find('/', scheme + 3);
			path = (slash == std::string::npos) ? "/" : path.substr(slash);
		}
		std::string name = path;
		if (const auto slash = name.find_last_of('/'); slash != std::string::npos)
			name = name.substr(slash + 1);
		if (name.empty()) continue;

		const std::string dest = dir + "/" + name;
		if (!m_rest.download(path, dest)) continue;

		std::string stem = name;
		if (const auto dot = stem.find_last_of('.'); dot != std::string::npos)
			stem = stem.substr(0, dot);

		const std::string track_id = m_state->track_add(rmms::TrackType_AUDIO, stem);
		if (track_id.empty()) continue;
		const std::string clip_id = m_state->clip_add(track_id, rmms::ClipType_AUDIO, 0, 0);
		if (clip_id.empty()) continue;
		m_state->clip_set_audio_url(clip_id, dest);

		created_tracks.push_back(track_id);
		imported_files.push_back(dest);
	}
	return !created_tracks.empty();
}

} // namespace rmms::backend::handlers

void rmms::backend::handlers::register_ai_bridge_handlers(protocol::HandlerRegistry& registry, AiBridge& bridge)
{ bridge.register_handlers(registry); }
