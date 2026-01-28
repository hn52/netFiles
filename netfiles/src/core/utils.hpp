#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <expected>
#include <source_location>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace utils {

// 字符串分割器
inline std::vector<std::string> split(std::string_view sv, char delim) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) 
    {
        auto pos = sv.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(sv.substr(start));
            break;
        }
        parts.emplace_back(sv.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// 字符串分割器
inline std::vector<std::string> parseCmd(std::string_view sv, char delim) {
	std::vector<std::string> parts;
	std::size_t start = 0;

	auto pos = sv.find(delim, start);
	if (pos == std::string_view::npos) {
		parts.emplace_back(sv.substr(start));
        return parts;
	}
	parts.emplace_back(sv.substr(start, pos - start));
	start = pos + 1;
	parts.emplace_back(sv.substr(start));

	return parts;
}

// 字符串转小写
inline std::string to_lower(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (unsigned char c : sv) {
        result.push_back(std::tolower(c));
    }
    return result;
}
inline std::string to_Upper(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (unsigned char c : sv) {
        result.push_back(std::toupper(c));
    }
    return result;
}

// 路径清理（防止../攻击）
inline std::string sanitize_path(std::string_view root, std::string_view path) {
    std::filesystem::path full_path = std::filesystem::path(root) / path;
    std::string canonical = std::filesystem::canonical(full_path).string();
    
    // 确保路径在root目录下
    if (canonical.find(std::filesystem::canonical(root).string()) != 0) {
        return ""; // 越权访问
    }
    return canonical;
}

// 日志宏
inline void log_info(std::string_view msg, 
                     std::source_location loc = std::source_location::current()) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    std::cout << std::format("[INFO] {} [{}:{}] {}\n", 
        oss.str(), loc.file_name(), loc.line(), msg);
}

inline void log_error(std::string_view msg,
                      std::source_location loc = std::source_location::current()) {
    std::cerr << std::format("[ERROR] [{}:{}] {}\n", 
        loc.file_name(), loc.line(), msg);
}

} // namespace utils

// 结果类型
template<typename T>
using Result = std::expected<T, std::string>;

#endif // UTILS_HPP
