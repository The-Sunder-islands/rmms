#include "ai_client/pipeline_translator.h"
#include "rmms_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rmms::backend::ai_client {

// ── JSON utility helpers ────────────────────────────────────────────────────

std::string PipelineTranslator::get_json_string(std::string_view json,
                                                std::string_view key)
{
    return json_get_string(json, std::string(key).c_str());
}

std::string PipelineTranslator::json_escape(std::string_view s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:   oss << c; break;
        }
    }
    return oss.str();
}

static std::string_view skip_whitespace(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix(1);
    return s;
}

std::string PipelineTranslator::json_get_string(std::string_view json, const char* key) {
    std::string search = "\"" + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";

    auto after = skip_whitespace(json.substr(pos + search.size()));
    if (after.empty() || after.front() != ':') return "";
    after.remove_prefix(1);
    after = skip_whitespace(after);

    if (after.empty()) return "";

    if (after.front() == '"') {
        after.remove_prefix(1);
        auto end = after.find('"');
        if (end == std::string_view::npos) return "";
        return std::string(after.substr(0, end));
    }

    if (after.front() == 'n' && after.size() >= 4 && after.substr(0, 4) == "null")
        return "";

    return "";
}

int PipelineTranslator::json_get_int(std::string_view json, const char* key, int default_val) {
    auto s = json_get_string(json, key);
    if (s.empty()) {
        std::string search = "\"" + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return default_val;
        auto after = skip_whitespace(json.substr(pos + search.size()));
        if (after.empty() || after.front() != ':') return default_val;
        after.remove_prefix(1);
        after = skip_whitespace(after);
        if (after.empty()) return default_val;
        auto end = after.find_first_of(",}");
        if (end == std::string_view::npos) return default_val;
        return std::stoi(std::string(after.substr(0, end)));
    }
    return std::stoi(s);
}

double PipelineTranslator::json_get_double(std::string_view json, const char* key, double default_val) {
    auto s = json_get_string(json, key);
    if (s.empty()) {
        std::string search = "\"" + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return default_val;
        auto after = skip_whitespace(json.substr(pos + search.size()));
        if (after.empty() || after.front() != ':') return default_val;
        after.remove_prefix(1);
        after = skip_whitespace(after);
        if (after.empty()) return default_val;
        auto end = after.find_first_of(",}");
        if (end == std::string_view::npos) return default_val;
        return std::stod(std::string(after.substr(0, end)));
    }
    return std::stod(s);
}

int64_t PipelineTranslator::json_get_int64(std::string_view json, const char* key, int64_t default_val) {
    auto s = json_get_string(json, key);
    if (s.empty()) {
        std::string search = "\"" + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return default_val;
        auto after = skip_whitespace(json.substr(pos + search.size()));
        if (after.empty() || after.front() != ':') return default_val;
        after.remove_prefix(1);
        after = skip_whitespace(after);
        if (after.empty()) return default_val;
        auto end = after.find_first_of(",}");
        if (end == std::string_view::npos) return default_val;
        return std::stoll(std::string(after.substr(0, end)));
    }
    return std::stoll(s);
}

bool PipelineTranslator::json_get_bool(std::string_view json, const char* key, bool default_val) {
    std::string search = "\"" + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return default_val;

    auto after = skip_whitespace(json.substr(pos + search.size()));
    if (after.empty() || after.front() != ':') return default_val;
    after.remove_prefix(1);
    after = skip_whitespace(after);
    if (after.empty()) return default_val;

    if (after.substr(0, 4) == "true") return true;
    if (after.substr(0, 5) == "false") return false;
    return default_val;
}

std::string PipelineTranslator::json_get_object(std::string_view json, const char* key) {
    std::string search = "\"" + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";

    auto after = skip_whitespace(json.substr(pos + search.size()));
    if (after.empty() || after.front() != ':') return "";
    after.remove_prefix(1);
    after = skip_whitespace(after);

    if (after.empty() || after.front() != '{') return "";

    int depth = 0;
    size_t i = 0;
    for (; i < after.size(); ++i) {
        if (after[i] == '{') depth++;
        else if (after[i] == '}') {
            depth--;
            if (depth == 0) break;
        } else if (after[i] == '"') {
            i++;
            while (i < after.size() && after[i] != '"') {
                if (after[i] == '\\') i++;
                i++;
            }
        }
    }
    if (depth != 0) return "";
    return std::string(after.substr(0, i + 1));
}

std::string PipelineTranslator::json_get_array(std::string_view json, const char* key) {
    std::string search = "\"" + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";

    auto after = skip_whitespace(json.substr(pos + search.size()));
    if (after.empty() || after.front() != ':') return "";
    after.remove_prefix(1);
    after = skip_whitespace(after);

    if (after.empty() || after.front() != '[') return "";

    int depth = 0;
    size_t i = 0;
    bool in_string = false;
    for (; i < after.size(); ++i) {
        if (in_string) {
            if (after[i] == '"' && after[i-1] != '\\') in_string = false;
        } else {
            if (after[i] == '"') in_string = true;
            else if (after[i] == '[') depth++;
            else if (after[i] == ']') {
                depth--;
                if (depth == 0) break;
            }
        }
    }
    if (depth != 0) return "";
    return std::string(after.substr(0, i + 1));
}

// ── JSON array helpers ───────────────────────────────────────────────────────

static std::string_view skip_json_whitespace(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix(1);
    return s;
}

std::vector<std::string> PipelineTranslator::json_array_strings(std::string_view arr) {
    std::vector<std::string> out;
    auto s = skip_json_whitespace(arr);
    if (s.empty() || s.front() != '[') return out;
    s.remove_prefix(1);
    while (!s.empty()) {
        s = skip_json_whitespace(s);
        if (s.empty() || s.front() == ']') break;
        if (s.front() == ',') { s.remove_prefix(1); continue; }
        if (s.front() == '"') {
            s.remove_prefix(1);
            std::string item;
            while (!s.empty() && s.front() != '"') {
                if (s.front() == '\\' && s.size() > 1) {
                    item += s.front();
                    s.remove_prefix(1);
                }
                item += s.front();
                s.remove_prefix(1);
            }
            if (!s.empty()) s.remove_prefix(1);  // closing quote
            out.push_back(std::move(item));
        } else {
            // skip a non-string token (object/number/bool) up to a comma or ]
            while (!s.empty() && s.front() != ',' && s.front() != ']')
                s.remove_prefix(1);
        }
    }
    return out;
}

std::vector<std::string> PipelineTranslator::json_array_objects(std::string_view arr) {
    std::vector<std::string> out;
    auto s = skip_json_whitespace(arr);
    if (s.empty() || s.front() != '[') return out;
    s.remove_prefix(1);
    while (!s.empty()) {
        s = skip_json_whitespace(s);
        if (s.empty() || s.front() == ']') break;
        if (s.front() == ',') { s.remove_prefix(1); continue; }
        if (s.front() == '{') {
            int depth = 0;
            size_t i = 0;
            for (; i < s.size(); ++i) {
                if (s[i] == '{') depth++;
                else if (s[i] == '}') {
                    depth--;
                    if (depth == 0) break;
                } else if (s[i] == '"') {
                    i++;
                    while (i < s.size() && s[i] != '"') {
                        if (s[i] == '\\') i++;
                        i++;
                    }
                }
            }
            if (depth != 0) break;
            out.push_back(std::string(s.substr(0, i + 1)));
            s.remove_prefix(i + 1);
        } else {
            while (!s.empty() && s.front() != ',' && s.front() != ']')
                s.remove_prefix(1);
        }
    }
    return out;
}

// ── Enum string → FlatBuffers enum mappings ─────────────────────────────────

int PipelineTranslator::param_type_from_string(std::string_view s) {
    if (s == "int") return rmms::ParamType_INT;
    if (s == "float") return rmms::ParamType_FLOAT;
    if (s == "string") return rmms::ParamType_STRING;
    if (s == "enum") return rmms::ParamType_ENUM;
    if (s == "bool") return rmms::ParamType_BOOL;
    if (s == "multi_enum") return rmms::ParamType_MULTI_ENUM;
    return rmms::ParamType_STRING;
}

int PipelineTranslator::device_type_from_string(std::string_view s) {
    if (s == "npu") return rmms::DeviceType_NPU;
    if (s == "cuda") return rmms::DeviceType_CUDA;
    if (s == "mps") return rmms::DeviceType_MPS;
    return rmms::DeviceType_CPU;
}

int PipelineTranslator::task_status_from_string(std::string_view s) {
    if (s == "queued") return rmms::TaskStatus_QUEUED;
    if (s == "processing") return rmms::TaskStatus_PROCESSING;
    if (s == "partial_error") return rmms::TaskStatus_PARTIAL_ERROR;
    if (s == "error") return rmms::TaskStatus_ERROR;
    if (s == "cancelled") return rmms::TaskStatus_CANCELLED;
    return rmms::TaskStatus_DONE;
}

// ── Translation: REST responses → FlatBuffers tables ────────────────────────

bool PipelineTranslator::capabilities_response_to_fb(std::string_view json,
                                                      flatbuffers::FlatBufferBuilder& fbb,
                                                      bool ok,
                                                      std::string_view error_code,
                                                      std::string_view error_msg)
{
    flatbuffers::Offset<rmms::StatusResponse> status;
    if (ok) {
        status = rmms::CreateStatusResponse(fbb, true);
    } else {
        auto ec = fbb.CreateString(error_code);
        auto em = fbb.CreateString(error_msg);
        status = rmms::CreateStatusResponse(fbb, false, ec, em);
    }

    if (!ok) {
        auto resp = rmms::CreateAIGetCapabilitiesResponse(fbb, status, 0);
        fbb.Finish(resp);
        return true;
    }

    auto proto_ver = fbb.CreateString(json_get_string(json, "protocol_version"));
    auto server_ver = fbb.CreateString(json_get_string(json, "server_version"));
    int64_t max_upload = json_get_int64(json, "max_upload_bytes", 0);

    // capabilities[]
    std::vector<flatbuffers::Offset<rmms::AICapability>> caps;
    for (const auto& cap_obj : json_array_objects(json_get_array(json, "capabilities"))) {
        auto id = fbb.CreateString(json_get_string(cap_obj, "id"));
        auto label = fbb.CreateString(json_get_string(cap_obj, "label"));
        auto desc = fbb.CreateString(json_get_string(cap_obj, "description"));
        auto default_model = fbb.CreateString(json_get_string(cap_obj, "default_model"));

        rmms::CapabilityStatus status =
            json_get_string(cap_obj, "status") == "implemented"
                ? rmms::CapabilityStatus_IMPLEMENTED
                : rmms::CapabilityStatus_NOT_IMPLEMENTED;

        std::vector<flatbuffers::Offset<flatbuffers::String>> models;
        for (auto& m : json_array_strings(json_get_array(cap_obj, "models")))
            models.push_back(fbb.CreateString(m));
        auto models_vec = fbb.CreateVector(models);

        std::vector<flatbuffers::Offset<rmms::ParamDef>> param_defs;
        for (const auto& pd_obj : json_array_objects(json_get_array(cap_obj, "param_defs"))) {
            rmms::ParamDefBuilder pdb(fbb);
            auto key = fbb.CreateString(json_get_string(pd_obj, "key"));
            auto pd_label = fbb.CreateString(json_get_string(pd_obj, "label"));
            auto pd_desc = fbb.CreateString(json_get_string(pd_obj, "description"));
            auto default_val = fbb.CreateString(json_get_string(pd_obj, "default"));
            auto placeholder = fbb.CreateString(json_get_string(pd_obj, "placeholder"));
            auto pattern = fbb.CreateString(json_get_string(pd_obj, "pattern"));
            auto group = fbb.CreateString(json_get_string(pd_obj, "group"));
            auto type = param_type_from_string(json_get_string(pd_obj, "type"));

            std::vector<flatbuffers::Offset<rmms::EnumOption>> options;
            for (const auto& opt_obj : json_array_objects(json_get_array(pd_obj, "options"))) {
                auto value = fbb.CreateString(json_get_string(opt_obj, "value"));
                auto opt_label = fbb.CreateString(json_get_string(opt_obj, "label"));
                options.push_back(rmms::CreateEnumOption(fbb, value, opt_label));
            }
            auto options_vec = fbb.CreateVector(options);

            pdb.add_key(key);
            pdb.add_type(static_cast<rmms::ParamType>(type));
            pdb.add_label(pd_label);
            pdb.add_description(pd_desc);
            pdb.add_default_val(default_val);
            pdb.add_min(json_get_double(pd_obj, "min"));
            pdb.add_max(json_get_double(pd_obj, "max"));
            pdb.add_step(json_get_double(pd_obj, "step"));
            pdb.add_decimals(static_cast<uint8_t>(json_get_int(pd_obj, "decimals")));
            pdb.add_placeholder(placeholder);
            pdb.add_pattern(pattern);
            pdb.add_options(options_vec);
            pdb.add_group(group);
            param_defs.push_back(pdb.Finish());
        }
        auto param_defs_vec = fbb.CreateVector(param_defs);

        caps.push_back(rmms::CreateAICapability(fbb, id, label, desc, status,
                                                models_vec, default_model, param_defs_vec));
    }
    auto caps_vec = fbb.CreateVector(caps);

    // devices[]
    std::vector<flatbuffers::Offset<rmms::AIDevice>> devices;
    for (const auto& dev_obj : json_array_objects(json_get_array(json, "devices"))) {
        auto install_hint = fbb.CreateString(json_get_string(dev_obj, "install_hint"));
        auto dev_type = device_type_from_string(json_get_string(dev_obj, "device_type"));

        std::vector<flatbuffers::Offset<rmms::AIDeviceUnit>> units;
        for (const auto& unit_obj : json_array_objects(json_get_array(dev_obj, "units"))) {
            auto name = fbb.CreateString(json_get_string(unit_obj, "name"));
            units.push_back(rmms::CreateAIDeviceUnit(
                fbb, json_get_int(unit_obj, "device_index"),
                name, json_get_int64(unit_obj, "memory_total_mb")));
        }
        auto units_vec = fbb.CreateVector(units);

        devices.push_back(rmms::CreateAIDevice(
            fbb, static_cast<rmms::DeviceType>(dev_type),
            json_get_bool(dev_obj, "available"),
            json_get_int(dev_obj, "count"),
            install_hint, units_vec));
    }
    auto devices_vec = fbb.CreateVector(devices);

    // scheduler
    auto sched_obj = json_get_object(json, "scheduler");
    rmms::AISchedulerBuilder sb(fbb);
    sb.add_max_concurrent_tasks(json_get_int(sched_obj, "max_concurrent_tasks"));
    sb.add_max_queue_size(json_get_int(sched_obj, "max_queue_size"));
    auto sched = sb.Finish();

    std::vector<flatbuffers::Offset<flatbuffers::String>> formats;
    for (auto& f : json_array_strings(json_get_array(json, "output_formats")))
        formats.push_back(fbb.CreateString(f));
    auto formats_vec = fbb.CreateVector(formats);

    std::vector<flatbuffers::Offset<flatbuffers::String>> pkgs;
    for (auto& p : json_array_strings(json_get_array(json, "output_packages")))
        pkgs.push_back(fbb.CreateString(p));
    auto pkgs_vec = fbb.CreateVector(pkgs);

    auto caps_table = rmms::CreateAICapabilities(
        fbb, proto_ver, server_ver, caps_vec, devices_vec, sched,
        formats_vec, pkgs_vec, max_upload);
    auto resp = rmms::CreateAIGetCapabilitiesResponse(fbb, status, caps_table);
    fbb.Finish(resp);
    return true;
}

bool PipelineTranslator::task_status_to_fb(std::string_view json,
                                           flatbuffers::FlatBufferBuilder& fbb)
{
    auto task_id = fbb.CreateString(json_get_string(json, "task_id"));
    auto step_type = fbb.CreateString(json_get_string(json, "step_type"));
    auto task_status = task_status_from_string(json_get_string(json, "status"));

    std::vector<flatbuffers::Offset<flatbuffers::String>> url_strings;
    for (const auto& u : json_array_objects(json_get_array(json, "completed_urls")))
        url_strings.push_back(fbb.CreateString(u));

    std::vector<flatbuffers::Offset<flatbuffers::String>> error_strings;
    for (const auto& e : json_array_objects(json_get_array(json, "errors")))
        error_strings.push_back(fbb.CreateString(e));

    auto urls_vec = fbb.CreateVector(url_strings);
    auto errors_vec = fbb.CreateVector(error_strings);

    auto status = rmms::CreateStatusResponse(fbb, true);
    auto resp = rmms::CreateAIGetTaskStatusResponse(
        fbb, status, task_id, static_cast<rmms::TaskStatus>(task_status),
        json_get_int(json, "current_step"), step_type,
        json_get_int(json, "percent"), 0, 0, urls_vec, errors_vec);
    fbb.Finish(resp);
    return true;
}

bool PipelineTranslator::list_tasks_to_fb(std::string_view json,
                                          flatbuffers::FlatBufferBuilder& fbb)
{
    std::vector<flatbuffers::Offset<flatbuffers::String>> tasks;
    for (const auto& t : json_array_objects(json_get_array(json, "tasks")))
        tasks.push_back(fbb.CreateString(t));
    auto tasks_vec = fbb.CreateVector(tasks);

    auto status = rmms::CreateStatusResponse(fbb, true);
    auto resp = rmms::CreateAIListTasksResponse(
        fbb, status, tasks_vec, json_get_int(json, "total"));
    fbb.Finish(resp);
    return true;
}

// ── Translation: AISubmitPipelineRequest (FlatBuffers) → JSON ──────────────

// JSON array of pipeline steps. Shared by the JSON body and the multipart form
// (the AI server expects the bare steps array in the form field "pipeline").
static std::string steps_array_json(const rmms::AISubmitPipelineRequest& req)
{
    std::ostringstream json;
    json << "[";
    if (req.pipeline()) {
        auto* steps = req.pipeline()->steps();
        if (steps) {
            for (size_t i = 0; i < steps->size(); ++i) {
                if (i > 0) json << ",";
                auto* step = steps->Get(i);
                json << "{";
                if (step->type())
                    json << "\"capability\":\"" << PipelineTranslator::json_escape(step->type()->string_view()) << "\"";
                if (step->model())
                    json << ",\"model\":\"" << PipelineTranslator::json_escape(step->model()->string_view()) << "\"";
                if (step->params())
                    json << ",\"params\":" << step->params()->string_view();
                if (step->model_params())
                    json << ",\"model_params\":" << step->model_params()->string_view();
                if (step->input()) {
                    json << ",\"input\":{";
                    json << "\"from_step\":" << step->input()->from_step();
                    if (step->input()->stem())
                        json << ",\"stem\":\"" << PipelineTranslator::json_escape(step->input()->stem()->string_view()) << "\"";
                    json << "}";
                }
                json << "}";
            }
        }
    }
    json << "]";
    return json.str();
}

std::string PipelineTranslator::submit_pipeline_request_to_json(
    const rmms::AISubmitPipelineRequest& req)
{
    std::ostringstream json;
    json << "{";

    if (req.input_url())
        json << "\"input_url\":\"" << json_escape(req.input_url()->string_view()) << "\",";

    // pipeline
    json << "\"pipeline\":{\"steps\":" << steps_array_json(req) << "},";

    if (req.device_pref())
        json << "\"device_preference\":\"" << json_escape(req.device_pref()->string_view()) << "\",";
    json << "\"device_index\":" << req.device_index() << ",";
    json << "\"priority\":" << req.priority() << ",";
    if (req.output_format())
        json << "\"output_format\":\"" << json_escape(req.output_format()->string_view()) << "\",";
    if (req.output_package())
        json << "\"output_package\":\"" << json_escape(req.output_package()->string_view()) << "\",";
    json << "\"force_refresh\":" << (req.force_refresh() ? "true" : "false");

    json << "}";
    return json.str();
}

PipelineTranslator::SubmitForm PipelineTranslator::submit_pipeline_form(
    const rmms::AISubmitPipelineRequest& req)
{
    SubmitForm form;
    form.steps_json = steps_array_json(req);
    if (req.device_pref())
        form.device_preference = std::string(req.device_pref()->string_view());
    form.priority = std::to_string(req.priority());
    if (req.output_format())
        form.output_format = std::string(req.output_format()->string_view());
    if (req.output_package())
        form.output_package = std::string(req.output_package()->string_view());
    form.force_refresh = req.force_refresh() ? "true" : "false";
    return form;
}

// ── Translation: SSE JSON events → FlatBuffers ─────────────────────────────

bool PipelineTranslator::partial_result_to_fb(std::string_view json,
                                              flatbuffers::FlatBufferBuilder& fbb)
{
    auto task_id = json_get_string(json, "task_id");
    auto step_type = json_get_string(json, "step_type");
    int step_index = json_get_int(json, "step_index");
    auto track_obj = json_get_object(json, "track");

    auto stem = json_get_string(track_obj, "stem");
    auto track_type = json_get_string(track_obj, "track_type");
    auto label = json_get_string(track_obj, "label");
    auto url = json_get_string(track_obj, "url");
    auto format = json_get_string(track_obj, "format");
    int sample_rate = json_get_int(track_obj, "sample_rate");
    double duration = json_get_double(track_obj, "duration");
    int64_t size_bytes = json_get_int64(track_obj, "size_bytes");

    auto tid = fbb.CreateString(task_id);
    auto stype = fbb.CreateString(step_type);
    auto sstem = fbb.CreateString(stem);
    auto ttype = fbb.CreateString(track_type);
    auto slabel = fbb.CreateString(label);
    auto surl = fbb.CreateString(url);
    auto sformat = fbb.CreateString(format);

    auto event = rmms::CreateAIEventPartialResult(fbb,
        tid, step_index, stype, sstem, ttype, slabel, surl, sformat,
        sample_rate, duration, size_bytes);
    fbb.Finish(event);
    return true;
}

bool PipelineTranslator::progress_to_fb(std::string_view json,
                                        flatbuffers::FlatBufferBuilder& fbb)
{
    auto task_id = json_get_string(json, "task_id");
    auto step_type = json_get_string(json, "step_type");
    int step_index = json_get_int(json, "step_index");
    auto status_str = json_get_string(json, "status");
    int percent = json_get_int(json, "percent");
    auto error_obj = json_get_object(json, "error");
    auto error_code = json_get_string(error_obj, "code");
    auto error_msg = json_get_string(error_obj, "message");

    rmms::StepStatus status = rmms::StepStatus_RUNNING;
    if (status_str == "completed") status = rmms::StepStatus_COMPLETED;
    else if (status_str == "failed") status = rmms::StepStatus_FAILED;

    auto urls_str = json_get_array(json, "urls");
    std::vector<std::string> url_list;
    if (!urls_str.empty()) {
        std::string_view arr(urls_str);
        arr.remove_prefix(1); // '['
        if (!arr.empty() && arr.back() == ']') arr.remove_suffix(1);
        size_t p = 0;
        while (p < arr.size()) {
            p = arr.find('"', p);
            if (p == std::string_view::npos) break;
            auto end = arr.find('"', p + 1);
            if (end == std::string_view::npos) break;
            url_list.push_back(std::string(arr.substr(p + 1, end - p - 1)));
            p = end + 1;
        }
    }

    auto tid = fbb.CreateString(task_id);
    auto stype = fbb.CreateString(step_type);
    auto ecode = error_code.empty() ? fbb.CreateString("") : fbb.CreateString(error_code);
    auto emsg = error_msg.empty() ? fbb.CreateString("") : fbb.CreateString(error_msg);

    std::vector<flatbuffers::Offset<flatbuffers::String>> url_offsets;
    for (const auto& u : url_list)
        url_offsets.push_back(fbb.CreateString(u));
    auto urls_vec = fbb.CreateVector(url_offsets);

    auto event = rmms::CreateAIEventProgress(fbb,
        tid, step_index, stype, status, percent, urls_vec, ecode, emsg);
    fbb.Finish(event);
    return true;
}

bool PipelineTranslator::final_result_to_fb(std::string_view json,
                                            flatbuffers::FlatBufferBuilder& fbb)
{
    auto task_id = json_get_string(json, "task_id");
    auto status_str = json_get_string(json, "status");

    rmms::TaskStatus status = rmms::TaskStatus_DONE;
    if (status_str == "partial_error") status = rmms::TaskStatus_PARTIAL_ERROR;
    else if (status_str == "error") status = rmms::TaskStatus_ERROR;
    else if (status_str == "cancelled") status = rmms::TaskStatus_CANCELLED;

    auto urls_str = json_get_array(json, "urls");
    auto errors_str = json_get_array(json, "errors");

    // Convert JSON arrays to string vectors
    std::vector<std::string> url_strings;
    std::vector<std::string> error_strings;

    if (!urls_str.empty()) {
        url_strings.push_back(std::string(urls_str));
    }
    if (!errors_str.empty()) {
        error_strings.push_back(std::string(errors_str));
    }

    auto tid = fbb.CreateString(task_id);

    std::vector<flatbuffers::Offset<flatbuffers::String>> url_offsets;
    for (const auto& u : url_strings)
        url_offsets.push_back(fbb.CreateString(u));
    auto urls_vec = fbb.CreateVector(url_offsets);

    std::vector<flatbuffers::Offset<flatbuffers::String>> err_offsets;
    for (const auto& e : error_strings)
        err_offsets.push_back(fbb.CreateString(e));
    auto errs_vec = fbb.CreateVector(err_offsets);

    auto event = rmms::CreateAIEventFinalResult(fbb,
        tid, status, urls_vec, errs_vec);
    fbb.Finish(event);
    return true;
}

}  // namespace rmms::backend::ai_client
