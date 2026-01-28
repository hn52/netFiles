#include "http_parser.hpp"
#include "core/utils.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <iomanip>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include "stringUtil.h"
#include "Encoding.hpp"
namespace webdav {

boost::asio::awaitable<Result<HttpRequest>> 
HttpParser::parse_request(boost::asio::ip::tcp::socket& socket,
                           boost::asio::streambuf& buffer) {
    HttpRequest request;
    
    // 读取请求行
    std::string request_line;
   
    boost::asio::streambuf::mutable_buffers_type header_buf = buffer.prepare(4096);
    std::size_t header_bytes = co_await socket.async_read_some(header_buf,
        boost::asio::use_awaitable);
    buffer.commit(header_bytes);
    
    std::string_view header_sv;
    std::string_view cotent_sv((char*)buffer.data().data(),
        (char*)buffer.data().data() + header_bytes);
    //std::cout << header_bytes << "ok Request line: " << cotent_sv << std::endl;
    // 查找空行
    auto empty_line = cotent_sv.find("\r\n\r\n");
    if (empty_line != std::string_view::npos) {
        
        header_sv = cotent_sv.substr(0, empty_line);
        buffer.consume(empty_line + 4);
    }

	if (!parse_request_line(header_sv, request.method,
		request.path, request.version)) {
        std::cout << "Request line: " << header_sv << std::endl;
		co_return std::unexpected("Invalid request line");
	}
    else
    {
       // std::cout<<header_bytes << "ok Request line: " << header_sv << std::endl;
    }
    
    request.path=StringUtil::encode<char,char8_t>((char8_t*)request.path.c_str());
    
    std::string headers_str(header_sv);
    parse_headers(headers_str, request);
    //std::cout << "Request headers: " << headers_str << std::endl;
    
    // 读取请求体（如果有）
    auto it = request.headers.find("content-length");
    if (it != request.headers.end()) {
        request.content_length = std::stoul(it->second);
        
        if (request.content_length > 0) {
            
            std::string body=std::string(cotent_sv.substr(empty_line+4,header_bytes-empty_line-4));
            body.reserve(request.content_length);
            
            while (body.size() < request.content_length) {
                int chunk= request.content_length-body.size()<4096?(request.content_length - body.size()) : 4096;
                auto buf = buffer.prepare(chunk);
                //std::cout << "chunk: " << chunk << std::endl;
                std::size_t bytes = co_await socket.async_read_some(buf,
                    boost::asio::use_awaitable);
               
                buffer.commit(bytes);
               // std::cout << "body read: " << (char*)buf.data() << std::endl;
                body.append((char*)buf.data(),
                    (char*)buf.data() + bytes);
            }
            
            request.body = std::move(body);
        }
    }
    
    co_return request;
}

bool HttpParser::parse_request_line(std::string_view line,
                                    HttpMethod& method,
                                    std::string& path,
                                    HttpVersion& version) {
    std::vector<std::string> parts = utils::split(line, ' ');
    
    if (parts.size() < 2) {
        return false;
    }
    
    auto method_opt = string_to_method(parts[0]);
    if (!method_opt.has_value()) {
        return false;
    }
    
    method = method_opt.value();
    path = decode_url(parts[1]);
    
    // 解析HTTP版本
    if (parts.size() >= 3) {
        if (parts[2] == "HTTP/1.1") {
            version = HttpVersion::HTTP11;
        } else if (parts[2] == "HTTP/1.0") {
            version = HttpVersion::HTTP10;
        } else {
            version = HttpVersion::UNKNOWN;
        }
    }
    
    return true;
}

bool HttpParser::parse_headers(std::string_view headers_str,
                               HttpRequest& request) {
    std::string str = std::string(headers_str);
    std::istringstream iss(str);
    std::string line;

	while ((std::getline)(iss, line)) {
		if (line.empty() || line == "\r") {
			continue;
		}
        
        // 去除\r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);
        
        // 去除前导空白
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        
        // 转换key为小写（HTTP头不区分大小写）
        std::transform(key.begin(), key.end(), key.begin(), 
            [](unsigned char c) { return std::tolower(c); });
        
        request.headers[key] = value;
        
        // 保存常用头
        if (key == "host") {
            request.host = value;
        } else if (key == "if-match") {
            request.if_match = value;
        } else if (key == "if-none-match") {
            request.if_none_match = value;
        } else if (key == "authorization") {
            request.authorization = value;
        } else if (key == "depth") {
            request.depth = value;
        } else if (key == "destination") {
            request.destination = value;
        } else if (key == "overwrite") {
            request.overwrite = value;
        } else if (key == "lock-token") {
            request.lock_token = value;
        } else if (key == "content-type") {
            request.content_type = value;
        } else if (key == "content-length") {
            request.content_length = std::stoul(value);
        }
    }
    
    return true;
}

std::string HttpParser::method_to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE_:  return "DELETE";
        case HttpMethod::MKCOL:   return "MKCOL";
        case HttpMethod::PROPFIND: return "PROPFIND";
        case HttpMethod::PROPPATCH: return "PROPPATCH";
        case HttpMethod::COPY:    return "COPY";
        case HttpMethod::MOVE:    return "MOVE";
        case HttpMethod::LOCK:    return "LOCK";
        case HttpMethod::UNLOCK:  return "UNLOCK";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default:                  return "UNKNOWN";
    }
}

std::optional<HttpMethod> HttpParser::string_to_method(std::string_view str) {
    if (str == "GET") return HttpMethod::GET;
    if (str == "HEAD") return HttpMethod::HEAD;
    if (str == "PUT") return HttpMethod::PUT;
    if (str == "DELETE") return HttpMethod::DELETE_;
    if (str == "MKCOL") return HttpMethod::MKCOL;
    if (str == "PROPFIND") return HttpMethod::PROPFIND;
    if (str == "PROPPATCH") return HttpMethod::PROPPATCH;
    if (str == "COPY") return HttpMethod::COPY;
    if (str == "MOVE") return HttpMethod::MOVE;
    if (str == "LOCK") return HttpMethod::LOCK;
    if (str == "UNLOCK") return HttpMethod::UNLOCK;
    if (str == "OPTIONS") return HttpMethod::OPTIONS;
    return std::nullopt;
}

HttpResponse HttpParser::error_response(int code, std::string_view message) {
    HttpResponse response;
    response.status_code = code;
    response.status_message = std::string(message);
    response.set_header("Content-Type", "text/plain");
    response.set_header("Content-Length", std::to_string(message.size()));
    response.body = std::string(message);
    return response;
}

std::string HttpParser::generate_response(const HttpResponse& response) {
    std::ostringstream oss;
    
    // 状态行
    oss << "HTTP/1.1 " << response.status_code << " " 
        << response.status_message << "\r\n";
    
    // 头
    for (const auto& [key, value] : response.headers) {
        oss << key << ": " << value << "\r\n";
    }
    
    // 空行
    oss << "\r\n";
    
    // 体（如果有）
    if (!response.body.empty()) {
        oss << response.body;
    }
    
    return oss.str();
}

std::string HttpParser::decode_url(std::string_view encoded) {
    std::string result;
    result.reserve(encoded.size());
    
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            if (i + 2 < encoded.size()) {
                int value;
                std::istringstream iss(std::string(encoded.substr(i + 1, 2)));
                if (iss >> std::hex >> value) {
                    result.push_back(static_cast<char>(value));
                    i += 2;
                    continue;
                }
            }
        }
        result.push_back(encoded[i]);
    }
    
    return result;
}

std::string HttpParser::encode_url(std::string_view str) {
    std::ostringstream oss;
    
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::uppercase << std::hex << std::setw(2) 
                << std::setfill('0') << static_cast<int>(c);
        }
    }
    
    return oss.str();
}
// 编码文件名用于URL
std::string HttpParser::encodeFilename(const std::string& filename) {
#if  0
    std::string url_encoded;
    std::wstring str = StringUtil::encode<wchar_t, char>(filename);
    wprintf(L"Encoded filename: %s\n", str.c_str());
    
    //return encode_url(str);
    for (auto c : str) {
        if (c >= 0x80) {
            // 中文字符：UTF-8编码后逐字节编码
            uint8_t utf8_buf[4];
            size_t utf8_len = encodeUtf8(c, utf8_buf);
            for (size_t i = 0; i < utf8_len; ++i) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%.2X", (unsigned char)utf8_buf[i]);
                url_encoded += hex;
            }
        }
        else if (c == ' ') {
            url_encoded += "%20";
        }
        else {
            url_encoded += c;
        }
    }

    return url_encoded;
#else
    std::string str=faw::Encoding::gb18030_to_utf8(filename.c_str());
    std::ostringstream oss;

    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        }
        else if (c == ' ') {
            oss << "%20";
        }
        else if (c <= 0x7f) {
            oss << c;
        }
        else {
            oss << '%' << std::uppercase << std::hex << std::setw(2)
                << std::setfill('0') << static_cast<int>(c);
        }
    }

    return oss.str();
#endif
}
std::string HttpParser::encode_url(std::u8string_view str) {
    std::ostringstream oss;

    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        }
        else {
            oss << '%' << std::uppercase << std::hex << std::setw(2)
                << std::setfill('0') << static_cast<int>(c);
        }
    }

    return oss.str();
}


size_t encodeUtf8(unsigned int codepoint, std::uint8_t* output) {
    // 检查代理对 (U+D800 到 U+DFFF) - 无效 Unicode 范围
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        return 0; // 错误：无效的代理码点
    }
    // 检查最大 Unicode 码点
    // Unicode 范围是 0x000000 - 0x10FFFF
    // 最大有效编码范围是 0x10FFFF。
    if (codepoint > 0x10FFFF) {
        return 0; // 错误：超出 Unicode 范围
    }
    if (codepoint < 0x80) {
        output[0] = static_cast<std::uint8_t>(codepoint);
        return 1;
    }
    else if (codepoint < 0x800) {
        output[0] = static_cast<std::uint8_t>(0xC0 | (codepoint >> 6));
        output[1] = static_cast<std::uint8_t>(0x80 | (codepoint & 0x3F));
        return 2;
    }
    else if (codepoint < 0x10000) {
        output[0] = static_cast<std::uint8_t>(0xE0 | (codepoint >> 12));
        output[1] = static_cast<std::uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        output[2] = static_cast<std::uint8_t>(0x80 | (codepoint & 0x3F));
        return 3;
    }
    else { // codepoint <= 0x10FFFF
        output[0] = static_cast<std::uint8_t>(0xF0 | (codepoint >> 18));
        output[1] = static_cast<std::uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
        output[2] = static_cast<std::uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        output[3] = static_cast<std::uint8_t>(0x80 | (codepoint & 0x3F));
        return 4;
    }
}

std::string getHttpDate() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    // 转换为UTC时间
    std::tm* gmtime_result = std::gmtime(&time);

    const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s, %02d %s %d %02d:%02d:%02d GMT",
        days[gmtime_result->tm_wday],
        gmtime_result->tm_mday,
        months[gmtime_result->tm_mon],
        gmtime_result->tm_year + 1900,
        gmtime_result->tm_hour,
        gmtime_result->tm_min,
        gmtime_result->tm_sec);

    return std::string(buffer);
}

} // namespace webdav
