#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flatbuffers {
template <bool B> class FlatBufferBuilderImpl;
using FlatBufferBuilder = FlatBufferBuilderImpl<false>;
}

namespace rmms { struct AISubmitPipelineRequest; }

namespace rmms::backend::ai_client {

class PipelineTranslator {
public:
    // Public JSON accessor for bridge-layer use (e.g. extracting task_id).
    static std::string get_json_string(std::string_view json, std::string_view key);

    // AISubmitPipelineRequest (FlatBuffers) → JSON for POST /api/v1/tasks
    static std::string submit_pipeline_request_to_json(
        const rmms::AISubmitPipelineRequest& req);

    // JSON from GET /api/v1/capabilities → full AIGetCapabilitiesResponse
    // table (fbb finished). On failure (or ok=false) builds an error-status
    // response with no capabilities.
    static bool capabilities_response_to_fb(std::string_view json,
                                            flatbuffers::FlatBufferBuilder& fbb,
                                            bool ok,
                                            std::string_view error_code,
                                            std::string_view error_msg);

    // JSON from GET /api/v1/tasks/{id} → AIGetTaskStatusResponse table
    static bool task_status_to_fb(std::string_view json,
                                  flatbuffers::FlatBufferBuilder& fbb);

    // JSON from GET /api/v1/tasks → AIListTasksResponse table
    static bool list_tasks_to_fb(std::string_view json,
                                 flatbuffers::FlatBufferBuilder& fbb);

    // SSE JSON events → FlatBuffers event tables
    static bool partial_result_to_fb(std::string_view json,
                                     flatbuffers::FlatBufferBuilder& fbb);

    static bool progress_to_fb(std::string_view json,
                               flatbuffers::FlatBufferBuilder& fbb);

    static bool final_result_to_fb(std::string_view json,
                                   flatbuffers::FlatBufferBuilder& fbb);

    // Raw JSON accessors used by the bridge (e.g. AI result import).
    static std::string json_get_array(std::string_view json, const char* key);
    static std::vector<std::string> json_array_strings(std::string_view arr);
    static std::vector<std::string> json_array_objects(std::string_view arr);

private:
    static std::string json_escape(std::string_view s);
    static std::string json_get_string(std::string_view json, const char* key);
    static int json_get_int(std::string_view json, const char* key, int default_val = 0);
    static double json_get_double(std::string_view json, const char* key, double default_val = 0.0);
    static int64_t json_get_int64(std::string_view json, const char* key, int64_t default_val = 0);
    static bool json_get_bool(std::string_view json, const char* key, bool default_val = false);
    static std::string json_get_object(std::string_view json, const char* key);
    // All string elements of a JSON array (returns empty list on parse error).
    // All object elements of a JSON array, kept as raw JSON strings.

    static int param_type_from_string(std::string_view s);
    static int device_type_from_string(std::string_view s);
    static int task_status_from_string(std::string_view s);
};

}  // namespace rmms::backend::ai_client
