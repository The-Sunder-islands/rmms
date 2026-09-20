#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rmms::backend::ai_client {

struct ParsedUrl {
    std::string host;
    int         port;
    std::string path_prefix;
};

struct HttpResponse {
    int         status;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

class RestClient {
public:
    explicit RestClient(std::string_view base_url = "http://127.0.0.1:8420");
    ~RestClient();

    RestClient(const RestClient&) = delete;
    RestClient& operator=(const RestClient&) = delete;

    HttpResponse get(std::string_view path,
                     const std::unordered_map<std::string, std::string>& headers = {});

    HttpResponse post(std::string_view path,
                      std::string_view body,
                      std::string_view content_type = "application/json",
                      const std::unordered_map<std::string, std::string>& headers = {});

    HttpResponse del(std::string_view path,
                     const std::unordered_map<std::string, std::string>& headers = {});

    // multipart/form-data upload of a single file (AI input upload).
    HttpResponse post_file(std::string_view path, std::string_view field,
                           std::string_view file_path,
                           std::string_view content_type = "application/octet-stream");

    // multipart/form-data with text fields plus one file part (AI server's
    // upload endpoint expects e.g. "pipeline" + "file").
    HttpResponse post_form(std::string_view path,
                           const std::vector<std::pair<std::string, std::string>>& fields,
                           std::string_view file_field, std::string_view file_path,
                           std::string_view content_type = "application/octet-stream");

    // GETs path and streams the body into dest_path (AI result download).
    // Returns true on HTTP 200. Chunked responses are not supported.
    bool download(std::string_view path, std::string_view dest_path);

    void set_timeout(int seconds);

private:
    int connect_to_server();
    HttpResponse request(std::string_view method, std::string_view path,
                         std::string_view body,
                         std::string_view content_type,
                         const std::unordered_map<std::string, std::string>& extra_headers);
    static HttpResponse parse_response(int fd);

    ParsedUrl   m_url;
    int         m_timeout_sec;
};

}  // namespace rmms::backend::ai_client
