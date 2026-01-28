#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <regex>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>
#include "core/utils.hpp"

namespace webdav {

// HTTP方法
enum class HttpMethod {
    GET,
    HEAD,
    PUT,
    DELETE_,
    MKCOL,
    PROPFIND,
    PROPPATCH,
    COPY,
    MOVE,
    LOCK,
    UNLOCK,
    OPTIONS
};

// HTTP版本
enum class HttpVersion {
    HTTP10,
    HTTP11,
    UNKNOWN
};

// HTTP请求
struct HttpRequest {
    HttpMethod method;
    std::string path;
    HttpVersion version;
    
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    // 解析相关
    std::string host;
    std::string if_match;
    std::string if_none_match;
    std::string if_modified_since;
    std::string if_unmodified_since;
    std::string authorization;
    
    // WebDAV特有
    std::string depth;          // for PROPFIND, LOCK
    std::string destination;    // for COPY, MOVE
    std::string overwrite;      // T or F
    std::string lock_token;
    std::string content_type;
    std::size_t content_length = 0;
};

// HTTP响应状态
struct HttpResponse {
    int status_code = 200;
    std::string status_message;
    HttpVersion version = HttpVersion::HTTP11;
    
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    void set_header(std::string_view key, std::string_view value) {
        headers[std::string(key)] = std::string(value);
    }
    
    std::string get_header(std::string_view key) const {
        auto it = headers.find(std::string(key));
        if (it != headers.end()) {
            return it->second;
        }
        return "";
    }
};

// HTTP解析器
class HttpParser {
public:
    // 解析请求
    static boost::asio::awaitable<Result<HttpRequest>> 
    parse_request(boost::asio::ip::tcp::socket& socket,
                  boost::asio::streambuf& buffer);
    
    // 解析请求行
    static bool parse_request_line(std::string_view line,
                                   HttpMethod& method,
                                   std::string& path,
                                   HttpVersion& version);
    
    // 解析请求头
    static bool parse_headers(std::string_view headers_str,
                              HttpRequest& request);
    
    // 方法转字符串
    static std::string method_to_string(HttpMethod method);
    
    // 字符串转方法
    static std::optional<HttpMethod> string_to_method(std::string_view str);
    
    // 生成响应
    static std::string generate_response(const HttpResponse& response);
    
    // 生成错误响应
    static HttpResponse error_response(int code, std::string_view message);
    
    // 解析URL编码
    static std::string decode_url(std::string_view encoded);
    
    // 编码URL
    static std::string  encode_url(std::string_view str);
    static std::string encode_url(std::u8string_view str);
    static std::string encodeFilename(const std::string& filename);
};

std::string getHttpDate();
size_t encodeUtf8(unsigned int codepoint, std::uint8_t* output);
} // namespace webdav

#endif // HTTP_PARSER_HPP
