#include "ai_client/rest_client.h"

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
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

HttpResponse RestClient::del(std::string_view path, const std::unordered_map<std::string, std::string>& headers)
{ return request("DELETE", path, "", "", headers); }

} // namespace rmms::backend::ai_client
