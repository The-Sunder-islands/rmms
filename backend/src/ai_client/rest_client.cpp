#include "ai_client/rest_client.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace rmms::backend::ai_client {

static ParsedUrl parse_url(std::string_view url)
{
	ParsedUrl parsed{};
	parsed.port = 80;

	std::string_view scheme;
	std::string_view remainder = url;

	auto proto_pos = url.find("://");
	if (proto_pos != std::string_view::npos)
	{
		scheme = url.substr(0, proto_pos);
		remainder = url.substr(proto_pos + 3);
		if (scheme == "https") parsed.port = 443;
	}

	auto path_pos = remainder.find('/');
	std::string_view host_port;
	if (path_pos != std::string_view::npos)
	{
		host_port = remainder.substr(0, path_pos);
		parsed.path_prefix = std::string(remainder.substr(path_pos));
	}
	else
	{
		host_port = remainder;
		parsed.path_prefix = "";
	}

	auto colon_pos = host_port.find(':');
	if (colon_pos != std::string_view::npos)
	{
		parsed.host = std::string(host_port.substr(0, colon_pos));
		parsed.port = std::stoi(std::string(host_port.substr(colon_pos + 1)));
	}
	else
	{
		parsed.host = std::string(host_port);
	}

	return parsed;
}

RestClient::RestClient(std::string_view base_url)
	: m_url(parse_url(base_url))
	, m_timeout_sec(30)
{
}

RestClient::~RestClient() = default;

void RestClient::set_timeout(int seconds)
{ m_timeout_sec = seconds; }

int RestClient::connect_to_server()
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
	std::string port_str = std::to_string(m_url.port);
	int ret = getaddrinfo(m_url.host.c_str(), port_str.c_str(), &hints, &result);
	if (ret != 0 || !result)
	{
		if (result) freeaddrinfo(result);
		close(fd);
		return -1;
	}

	ret = connect(fd, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);
	if (ret < 0)
	{
		close(fd);
		return -1;
	}

	return fd;
}

HttpResponse RestClient::request(std::string_view method, std::string_view path, std::string_view body,
	std::string_view content_type, const std::unordered_map<std::string, std::string>& extra_headers)
{
	int fd = connect_to_server();
	if (fd < 0) return HttpResponse{-1, "connection failed", {}};

	std::string full_path = m_url.path_prefix.empty() ? std::string(path) : (m_url.path_prefix + std::string(path));

	std::ostringstream req;
	req << method << " " << full_path << " HTTP/1.1\r\n";
	req << "Host: " << m_url.host;
	if (m_url.port != 80 && m_url.port != 443) req << ":" << m_url.port;
	req << "\r\n";
	req << "Connection: close\r\n";

	if (!body.empty())
	{
		req << "Content-Type: " << content_type << "\r\n";
		req << "Content-Length: " << body.size() << "\r\n";
	}

	for (const auto& [k, v] : extra_headers)
		req << k << ": " << v << "\r\n";

	req << "\r\n";
	req << body;

	std::string req_str = req.str();
	ssize_t sent = send(fd, req_str.data(), req_str.size(), MSG_NOSIGNAL);
	if (sent < 0)
	{
		close(fd);
		return HttpResponse{-1, "send failed", {}};
	}

	HttpResponse resp = parse_response(fd);
	close(fd);
	return resp;
}

HttpResponse RestClient::parse_response(int fd)
{
	HttpResponse resp{};
	std::string header_buf;
	header_buf.reserve(8192);
	char buf[4096];

	while (header_buf.find("\r\n\r\n") == std::string::npos)
	{
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
		{
			if (n == 0 && !header_buf.empty()) break;
			resp.status = -1;
			resp.body = "recv failed";
			return resp;
		}
		header_buf.append(buf, static_cast<size_t>(n));
		if (header_buf.size() > 65536)
		{
			resp.status = -1;
			resp.body = "headers too large";
			return resp;
		}
	}

	auto header_end = header_buf.find("\r\n\r\n");
	std::string_view headers_sv(header_buf.data(), header_end);

	auto status_line_end = headers_sv.find("\r\n");
	std::string_view status_line = headers_sv.substr(0, status_line_end);

	auto sp1 = status_line.find(' ');
	auto sp2 = status_line.find(' ', sp1 + 1);
	if (sp1 != std::string_view::npos && sp2 != std::string_view::npos)
	{
		auto code_str = status_line.substr(sp1 + 1, sp2 - sp1 - 1);
		resp.status = std::stoi(std::string(code_str));
	}

	size_t content_length = 0;
	bool chunked = false;
	std::string_view headers_body = headers_sv.substr(status_line_end + 2);

	size_t pos = 0;
	while (pos < headers_body.size())
	{
		auto line_end = headers_body.find("\r\n", pos);
		std::string_view line;
		if (line_end == std::string_view::npos)
		{
			// Last header line (the terminating CRLFCRLF was already removed).
			line = headers_body.substr(pos);
			pos = headers_body.size();
		}
		else
		{
			line = headers_body.substr(pos, line_end - pos);
			pos = line_end + 2;
		}

		auto colon = line.find(':');
		if (colon == std::string_view::npos) continue;

		std::string key(line.substr(0, colon));
		std::string val(line.substr(colon + 1));
		while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
			val.erase(0, 1);

		for (auto& c : key)
			c = static_cast<char>(std::tolower(c));
		resp.headers[key] = val;

		if (key == "content-length") content_length = std::stoul(val);
		else if (key == "transfer-encoding" && val.find("chunked") != std::string::npos)
			chunked = true;
	}

	// The header_buf might contain body data after \r\n\r\n
	std::string body_prefix(header_buf.data() + header_end + 4, header_buf.size() - header_end - 4);

	if (chunked)
	{
		std::string all_body = body_prefix;
		while (true)
		{
			ssize_t n = recv(fd, buf, sizeof(buf), 0);
			if (n <= 0) break;
			all_body.append(buf, static_cast<size_t>(n));
		}

		std::string decoded;
		size_t chunk_pos = 0;
		while (chunk_pos < all_body.size())
		{
			auto crlf = all_body.find("\r\n", chunk_pos);
			if (crlf == std::string::npos) break;
			std::string size_hex(all_body, chunk_pos, crlf - chunk_pos);
			size_t chunk_size = std::stoul(size_hex, nullptr, 16);
			if (chunk_size == 0) break;
			if (crlf + 2 + chunk_size > all_body.size()) break;
			decoded.append(all_body, crlf + 2, chunk_size);
			chunk_pos = crlf + 2 + chunk_size + 2; // +2 for trailing \r\n
		}
		resp.body = std::move(decoded);
	}
	else
	{
		resp.body = body_prefix;
		if (content_length > resp.body.size())
		{
			size_t remaining = content_length - resp.body.size();
			std::string more_body(remaining, '\0');
			size_t offset = 0;
			while (offset < remaining)
			{
				ssize_t n = recv(fd, more_body.data() + offset, remaining - offset, 0);
				if (n <= 0) break;
				offset += static_cast<size_t>(n);
			}
			resp.body += more_body;
		}
	}

	return resp;
}

HttpResponse RestClient::get(std::string_view path, const std::unordered_map<std::string, std::string>& headers)
{ return request("GET", path, "", "", headers); }

HttpResponse RestClient::post(std::string_view path, std::string_view body, std::string_view content_type,
	const std::unordered_map<std::string, std::string>& headers)
{ return request("POST", path, body, content_type, headers); }
HttpResponse RestClient::del(std::string_view path,
                              const std::unordered_map<std::string, std::string>& headers) {
    return request("DELETE", path, "", "", headers);
}

HttpResponse RestClient::post_file(std::string_view path, std::string_view field,
                                   std::string_view file_path,
                                   std::string_view content_type)
{
    return post_form(path, {}, field, file_path, content_type);
}

HttpResponse RestClient::post_form(
    std::string_view path,
    const std::vector<std::pair<std::string, std::string>>& fields,
    std::string_view file_field, std::string_view file_path,
    std::string_view content_type)
{
    std::FILE* f = std::fopen(std::string(file_path).c_str(), "rb");
    if (f == nullptr) {
        return HttpResponse{-1, "cannot open file", {}};
    }
    std::string data;
    char buf[8192];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    std::fclose(f);

    // Unique enough for a single request; the field is only used for framing.
    static std::atomic<unsigned> counter{0};
    const std::string boundary =
        "----rmmsformboundary" + std::to_string(counter.fetch_add(1));

    std::string basename(file_path);
    if (auto slash = basename.find_last_of('/'); slash != std::string::npos)
        basename = basename.substr(slash + 1);

    std::string body;
    body.reserve(data.size() + 512);
    for (const auto& [key, value] : fields) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n";
        body += value;
        body += "\r\n";
    }
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + std::string(file_field) +
            "\"; filename=\"" + basename + "\"\r\n";
    body += "Content-Type: " + std::string(content_type) + "\r\n\r\n";
    body += data;
    body += "\r\n--" + boundary + "--\r\n";

    return request("POST", path, body,
                   "multipart/form-data; boundary=" + boundary, {});
}

bool RestClient::download(std::string_view path, std::string_view dest_path) {
    int fd = connect_to_server();
    if (fd < 0) return false;

    const std::string full_path = m_url.path_prefix.empty()
        ? std::string(path)
        : (m_url.path_prefix + std::string(path));

    std::string req;
    req += "GET " + full_path + " HTTP/1.1\r\n";
    req += "Host: " + m_url.host;
    if (m_url.port != 80 && m_url.port != 443)
        req += ":" + std::to_string(m_url.port);
    req += "\r\nConnection: close\r\n\r\n";

    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t w = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (w <= 0) {
            close(fd);
            return false;
        }
        sent += static_cast<size_t>(w);
    }

    // Read the response head.
    std::string head;
    char buf[8192];
    while (head.find("\r\n\r\n") == std::string::npos) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) {
            close(fd);
            return false;
        }
        head.append(buf, static_cast<size_t>(r));
        if (head.size() > 65536) {
            close(fd);
            return false;
        }
    }
    const auto head_end = head.find("\r\n\r\n");
    const std::string headers = head.substr(0, head_end);
    const std::string body_prefix = head.substr(head_end + 4);

    const auto status_pos = headers.find(' ');
    if (status_pos == std::string::npos) {
        close(fd);
        return false;
    }
    const int status = std::atoi(headers.c_str() + status_pos + 1);
    if (status != 200) {
        close(fd);
        return false;
    }
    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        close(fd);
        return false;  // not needed for the AI server's FileResponse
    }

    std::FILE* out = std::fopen(std::string(dest_path).c_str(), "wb");
    if (out == nullptr) {
        close(fd);
        return false;
    }

    bool ok = body_prefix.empty() ||
              std::fwrite(body_prefix.data(), 1, body_prefix.size(), out) ==
                  body_prefix.size();
    while (ok) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r == 0) break;
        if (r < 0) {
            ok = false;
            break;
        }
        ok = std::fwrite(buf, 1, static_cast<size_t>(r), out) ==
             static_cast<size_t>(r);
    }
    std::fclose(out);
    close(fd);
    return ok;
}
} // namespace rmms::backend::ai_client
