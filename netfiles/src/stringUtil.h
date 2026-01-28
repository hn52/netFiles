#ifndef MODERN_STRING_H
#define MODERN_STRING_H

#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <charconv>
#include <format>
#include <cwchar>
#include <codecvt>
#include <algorithm>
#include <ranges>
#include <concepts>
#include <memory>
#include <Windows.h>
namespace StringUtil {
// ============================================
// 基础 traits 类
// ============================================

template <typename CharT>
struct CharTraits {
    static constexpr bool is_char        = std::same_as<CharT, char>;
    static constexpr bool is_wchar       = std::same_as<CharT, wchar_t>;
    static constexpr bool is_char8       = std::same_as<CharT, char8_t>;
    static constexpr bool is_char16      = std::same_as<CharT, char16_t>;
    static constexpr bool is_char32      = std::same_as<CharT, char32_t>;
    static constexpr bool is_narrow      = is_char || is_char8;
    static constexpr bool is_wide        = is_wchar || is_char16 || is_char32;
    static constexpr size_t min_size     = is_char ? 1 : (is_wchar ? sizeof(wchar_t) : sizeof(CharT));
};

// ============================================
// 编码转换工具
// ============================================

namespace detail {

// UTF-8 到 UTF-32 转换
inline std::u32string_view utf8_to_utf32_view(std::string_view input) {
    static thread_local std::u32string buffer;
    buffer.clear();
    
    for (size_t i = 0; i < input.size(); ) {
        char32_t codepoint = 0;
        unsigned char byte = static_cast<unsigned char>(input[i]);
        
        if (byte < 0x80) {
            codepoint = byte;
            i += 1;
        } else if ((byte >> 5) == 0x06) {
            codepoint = (byte & 0x1F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F);
            i += 2;
        } else if ((byte >> 4) == 0x0E) {
            codepoint = (byte & 0x0F) << 12;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 2]) & 0x3F);
            i += 3;
        } else if ((byte >> 3) == 0x1E) {
            codepoint = (byte & 0x07) << 18;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 12;
            codepoint |= (static_cast<unsigned char>(input[i + 2]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 3]) & 0x3F);
            i += 4;
        } else {
            throw std::runtime_error("Invalid UTF-8 sequence");
        }
        buffer += codepoint;
    }
    
    return std::u32string_view(buffer);
}

// UTF-32 到 UTF-8 转换
inline std::string utf32_to_utf8(std::u32string_view input) {
    std::string result;
    result.reserve(input.size() * 4);
    
    for (char32_t codepoint : input) {
        if (codepoint < 0x80) {
            result += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            result += static_cast<char>(0xC0 | (codepoint >> 6));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }
    
    return result;
}

// UTF-16 编码/解码
inline char32_t decode_utf16(const char16_t*& ptr, const char16_t* end) {
    char32_t codepoint = *ptr++;
    
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && ptr < end) {
        char32_t low = *ptr;
        if (low >= 0xDC00 && low <= 0xDFFF) {
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            ++ptr;
        }
    }
    
    return codepoint;
}

inline void encode_utf16(char32_t codepoint, std::u16string& result) {
    if (codepoint < 0x10000) {
        result += static_cast<char16_t>(codepoint);
    } else {
        codepoint -= 0x10000;
        result += static_cast<char16_t>(0xD800 | (codepoint >> 10));
        result += static_cast<char16_t>(0xDC00 | (codepoint & 0x3FF));
    }
}

// UTF-32 到 UTF-16 转换
inline std::u16string utf32_to_utf16(std::u32string_view input) {
    std::u16string result;
    result.reserve(input.size() * 2);
    
    for (char32_t codepoint : input) {
        encode_utf16(codepoint, result);
    }
    
    return result;
}

// UTF-16 到 UTF-32 转换
inline std::u32string utf16_to_utf32(std::u16string_view input) {
    std::u32string result;
    result.reserve(input.size());
    
    const char16_t* ptr = input.data();
    const char16_t* end = input.data() + input.size();
    
    while (ptr < end) {
        result += decode_utf16(ptr, end);
    }
    
    return result;
}

template <typename T>
[[nodiscard]] std::string to_string_safe(T value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec == std::errc()) {
        return std::string(buffer, result.ptr - buffer);
    }
    return "0";
}
template <typename T>
[[nodiscard]] std::string to_string_safe_float(T value, int precision = 6) {
    char buffer[128];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::fixed, precision);
    if (result.ec == std::errc()) {
        return std::string(buffer, result.ptr - buffer);
    }
    return "0.0";
}

} // namespace detail

// ============================================
// 通用编码转换函数
// ============================================

class EncodingConverter {
public:
    // UTF-8 <-> UTF-16
    static std::u16string to_utf16(std::string_view input) {
        auto utf32 = detail::utf8_to_utf32_view(input);
        return detail::utf32_to_utf16(utf32);
    }
    
    static std::string from_utf16(std::u16string_view input) {
        auto utf32 = detail::utf16_to_utf32(input);
        return detail::utf32_to_utf8(utf32);
    }
    
    // UTF-8 <-> UTF-32
    static std::u32string to_utf32(std::string_view input) {
        return std::u32string(detail::utf8_to_utf32_view(input));
    }
    
    static std::string from_utf32(std::u32string_view input) {
        return detail::utf32_to_utf8(input);
    }
    
    // UTF-16 <-> UTF-32
    static std::u32string utf16_to_utf32(std::u16string_view input) {
        return detail::utf16_to_utf32(input);
    }
    
    static std::u16string utf32_to_utf16(std::u32string_view input) {
        return detail::utf32_to_utf16(input);
    }

};

template <typename TargetString>
int GetWin32CodePage() {
    if (std::is_same<TargetString, char>::value) {
        return CP_ACP;
    }
    else if (std::is_same<TargetString, char8_t>::value) {
        return CP_UTF8;
    }
    else if (std::is_same<TargetString, wchar_t>::value) {
        return 777;
    }
    return -1;
}

template <typename TargetString, typename SourceString>
std::basic_string<TargetString>  encode(std::basic_string_view<SourceString> input) 
{
    int dstCodePage = GetWin32CodePage<TargetString>();
    int srcCodePage = GetWin32CodePage<SourceString>();
    if(srcCodePage == -1 || dstCodePage == -1) return {};
    if (std::is_same<SourceString, wchar_t>::value)
    {
        if (input.empty()) return {};
        int size = WideCharToMultiByte(dstCodePage, 0, (LPCWCH)input.data(), static_cast<int>(input.size()),
            nullptr, 0, nullptr, nullptr);
        if (size <= 0) throw std::runtime_error("WideCharToMultiByte failed");
        std::vector<TargetString> result(size+1, '\0');
        WideCharToMultiByte(dstCodePage, 0, (LPCWCH)input.data(), static_cast<int>(input.size()),
            (LPSTR)result.data(), size, nullptr, nullptr);
        return result.data();
    }
    else if (std::is_same<TargetString, wchar_t>::value)
    {
        if (input.empty()) return {};
        int size = MultiByteToWideChar(srcCodePage, 0, (LPCCH)input.data(), static_cast<int>(input.size()),
            nullptr, 0);
        if (size <= 0) throw std::runtime_error("MultiByteToWideChar failed");
        std::vector<TargetString> result(size+1, L'\0');
        MultiByteToWideChar(srcCodePage, 0, (LPCCH)input.data(), static_cast<int>(input.size()),
            (wchar_t*)result.data(), size);
        return result.data();
    }
    else if (std::is_same<TargetString, SourceString>::value)
    {
        return {};
    }
    else
    {
        int size = MultiByteToWideChar(srcCodePage, 0, (LPCCH)input.data(), static_cast<int>(input.size()),
            nullptr, 0);
        if (size <= 0) throw std::runtime_error("MultiByteToWideChar failed");
        std::wstring result(size, L'\0');
        MultiByteToWideChar(srcCodePage, 0, (LPCCH)input.data(), static_cast<int>(input.size()),
            result.data(), size);
        size = WideCharToMultiByte(dstCodePage, 0, result.data(), static_cast<int>(result.size()),
            nullptr, 0, nullptr, nullptr);
        if (size <= 0) throw std::runtime_error("WideCharToMultiByte failed");
        std::vector<TargetString> result1(size+1, '\0');
        WideCharToMultiByte(dstCodePage, 0, result.data(), static_cast<int>(result.size()),
            (LPSTR)result1.data(), size, nullptr, nullptr);
        return result1.data();
    }

}
// 宽字符转换 (基于系统locale)
template 
std::string encode<char, wchar_t>(std::wstring_view input);
template 
std::u8string encode<char8_t, wchar_t>(std::wstring_view input);
template 
std::wstring encode<wchar_t, char>(std::string_view input);
template 
std::wstring encode<wchar_t, char8_t>(std::u8string_view input);
template 
std::string encode<char, char8_t>(std::u8string_view input);
template 
std::u8string encode<char8_t, char>(std::string_view input);



// ============================================
// ModernString 模板类定义
// ============================================

template <typename CharT>
    requires (std::same_as<CharT, char> || 
              std::same_as<CharT, char8_t> || 
              std::same_as<CharT, wchar_t> ||
              std::same_as<CharT, char16_t> || 
              std::same_as<CharT, char32_t>)
class ModernString {
public:
    // 类型别名
    using char_type   = CharT;
    using string_type = std::basic_string<CharT>;
    using view_type   = std::basic_string_view<CharT>;
    using size_type   = typename string_type::size_type;
    using iterator    = typename string_type::iterator;
    using const_iterator = typename string_type::const_iterator;
    
    // 静态常量
    static constexpr size_type npos = static_cast<size_type>(-1);
    
private:
    string_type data_;
    
    // 辅助函数：字符分类
    [[nodiscard]] static constexpr bool is_space(CharT c) noexcept {
        if constexpr (std::same_as<CharT, char> || std::same_as<CharT, char8_t>) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'|| c == '\0';
        } else if constexpr (std::same_as<CharT, wchar_t>) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == '\0';
        } else {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == '\0';
        }
    }
    
    // 辅助函数：数字字符判断
    [[nodiscard]] static constexpr bool is_digit(CharT c) noexcept {
        if constexpr (std::same_as<CharT, char> || std::same_as<CharT, char8_t>) {
            return c >= '0' && c <= '9';
        } else if constexpr (std::same_as<CharT, wchar_t>) {
            return c >= '0' && c <= '9';
        } else {
            return c >= u8'0' && c <= u8'9';
        }
    }
    
    // 辅助函数：十六进制字符判断
    [[nodiscard]] static constexpr bool is_hex_digit(CharT c) noexcept {
        return is_digit(c) || 
               (c >= 'a' && c <= 'f') || 
               (c >= 'A' && c <= 'F');
    }
    
    // 辅助函数：十六进制字符转数值
    [[nodiscard]] static constexpr int hex_value(CharT c) noexcept {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    }
    
    // 辅助函数：安全解析整数
    template <typename NumericType>
    [[nodiscard]] std::optional<NumericType> parse_number() const noexcept {
        if (data_.empty()) return std::nullopt;
        
        size_t start = 0;
        bool negative = false;
        
        // 处理符号
        if constexpr (std::signed_integral<NumericType>) {
            if (data_[0] == '-') {
                negative = true;
                start = 1;
            } else if (data_[0] == '+') {
                start = 1;
            }
        }
        
        if (start >= data_.size()) return std::nullopt;
        
        NumericType result = 0;
        bool has_digit = false;
        
        for (size_t i = start; i < data_.size(); ++i) {
            if (!is_digit(data_[i])) {
                // 十六进制前缀
                if ((data_[i] == 'x' || data_[i] == 'X') && i == start + 1 && start == 1) {
                    // 0x 或 0X 开头
                    result = 0;
                    for (size_t j = i + 1; j < data_.size(); ++j) {
                        if (!is_hex_digit(data_[j])) break;
                        has_digit = true;
                        result = result * 16 + hex_value(data_[j]);
                    }
                    return negative ? -result : result;
                }
                break;
            }
            has_digit = true;
            result = result * 10 + (data_[i] - '0');
        }
        
        if (!has_digit) return std::nullopt;
        return negative ? -result : result;
    }
    
    // 辅助函数：安全解析浮点数
    template <typename FloatType>
    [[nodiscard]] std::optional<FloatType> parse_float() const noexcept {
        if (data_.empty()) return std::nullopt;
        
        size_t start = 0;
        bool negative = false;
        
        if (data_[0] == '-') {
            negative = true;
            start = 1;
        } else if (data_[0] == '+') {
            start = 1;
        }
        
        if (start >= data_.size()) return std::nullopt;
        
        FloatType result = 0;
        FloatType divisor = 1;
        bool in_fraction = false;
        bool has_digit = false;
        
        for (size_t i = start; i < data_.size(); ++i) {
            if (data_[i] == '.') {
                if (in_fraction) break;
                in_fraction = true;
                continue;
            }
            
            if (!is_digit(data_[i])) {
                // 检查科学计数法
                if ((data_[i] == 'e' || data_[i] == 'E') && i < data_.size() - 1) {
                    int exp_sign = 1;
                    size_t exp_start = i + 1;
                    if (data_[i + 1] == '-') {
                        exp_sign = -1;
                        exp_start = i + 2;
                    } else if (data_[i + 1] == '+') {
                        exp_start = i + 2;
                    }
                    
                    if (exp_start >= data_.size()) break;
                    
                    int exponent = 0;
                    for (size_t j = exp_start; j < data_.size(); ++j) {
                        if (!is_digit(data_[j])) break;
                        exponent = exponent * 10 + (data_[j] - '0');
                    }
                    
                    // 应用指数
                    while (exponent > 0) {
                        result *= 10;
                        divisor *= 10;
                        --exponent;
                    }
                    while (exponent < 0) {
                        result /= 10;
                        divisor /= 10;
                        --exponent;
                    }
                    break;
                }
                break;
            }
            
            has_digit = true;
            result = result * 10 + (data_[i] - '0');
            if (in_fraction) {
                divisor *= 10;
            }
        }
        
        if (!has_digit) return std::nullopt;
        result /= divisor;
        return negative ? -result : result;
    }
    
public:
    // ========================================
    // 构造函数和析构函数
    // ========================================
    
    ModernString() noexcept = default;
    
    explicit ModernString(view_type view) : data_(view) {}
    
    ModernString(const ModernString& other) : data_(other.data_) {}
    ModernString(ModernString&& other) noexcept : data_(std::move(other.data_)) {}
    
    template <typename OtherChar>
    requires (!std::same_as<OtherChar, CharT>)
    explicit ModernString(const ModernString<OtherChar>& other) {
        if constexpr (std::same_as<CharT, char>) {
            data_ = other.to_string();
        } else if constexpr (std::same_as<CharT, wchar_t>) {
            data_ = encode<wchar_t, OtherChar>(other.view());//EncodingConverter::to_wide<std::wstring>(other.to_string_view());
        } else if constexpr (std::same_as<CharT, char8_t>) {
            data_ = reinterpret_cast<const char8_t*>(other.to_utf8().c_str());
        } else if constexpr (std::same_as<CharT, char16_t>) {
            data_ = EncodingConverter::to_utf16(other.to_string());
        } else if constexpr (std::same_as<CharT, char32_t>) {
            data_ = EncodingConverter::to_utf32(other.to_string());
        }
    }
    
    // 从 C-string 构造（使用模板以支持不同字符类型）
    template <typename OtherChar>
    requires std::same_as<OtherChar, CharT>
    ModernString(const OtherChar* str) : data_(str) {}
    
    template <typename OtherChar>
    requires std::same_as<OtherChar, CharT>
    ModernString(const OtherChar* str, size_type count) : data_(str, count) {}
    
    // 初始化列表构造（仅对 char 类型可用）
    ModernString(std::initializer_list<CharT> init) : data_(init) {}
    
    // ========================================
    // 赋值运算符
    // ========================================
    
    ModernString& operator=(const ModernString& other) {
        if (this != &other) {
            data_ = other.data_;
        }
        return *this;
    }
    
    ModernString& operator=(ModernString&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
        }
        return *this;
    }
    
    ModernString& operator=(view_type view) {
        data_ = view;
        return *this;
    }
    
    ModernString& operator=(CharT ch) {
        data_.assign(1, ch);
        return *this;
    }
    
    // += 运算符
    ModernString& operator+=(const ModernString& other) {
        data_ += other.data_;
        return *this;
    }
    
    ModernString& operator+=(view_type view) {
        data_ += view;
        return *this;
    }
    
    ModernString& operator+=(CharT ch) {
        data_ += ch;
        return *this;
    }
    ModernString& operator+=(CharT* ch) {
        data_ += ch;
        return *this;
    }
    
    // ========================================
    // 元素访问
    // ========================================
    
    [[nodiscard]] CharT& at(size_type pos) {
        return data_.at(pos);
    }
    
    [[nodiscard]] const CharT& at(size_type pos) const {
        return data_.at(pos);
    }
    
    [[nodiscard]] CharT& operator[](size_type pos) {
        return data_[pos];
    }
    
    [[nodiscard]] const CharT& operator[](size_type pos) const {
        return data_[pos];
    }
    
    [[nodiscard]] CharT& front() {
        return data_.front();
    }
    
    [[nodiscard]] const CharT& front() const {
        return data_.front();
    }
    
    [[nodiscard]] CharT& back() {
        return data_.back();
    }
    
    [[nodiscard]] const CharT& back() const {
        return data_.back();
    }
    
    // ========================================
    // 迭代器
    // ========================================
    
    iterator begin() noexcept { return data_.begin(); }
    iterator end() noexcept { return data_.end(); }
    const_iterator begin() const noexcept { return data_.begin(); }
    const_iterator end() const noexcept { return data_.end(); }
    const_iterator cbegin() const noexcept { return data_.cbegin(); }
    const_iterator cend() const noexcept { return data_.cend(); }
    
    // ========================================
    // 容量相关
    // ========================================
    
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return data_.size(); }
    [[nodiscard]] size_type length() const noexcept { return data_.length(); }
    [[nodiscard]] size_type max_size() const noexcept { return data_.max_size(); }
    [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
    
    void reserve(size_type new_cap) { data_.reserve(new_cap); }
    void shrink_to_fit() { data_.shrink_to_fit(); }
    
    void clear() noexcept { data_.clear(); }
    
    // ========================================
    // 转换函数
    // ========================================
    
    [[nodiscard]] const CharT* c_str() const noexcept { return data_.c_str(); }
    [[nodiscard]] const CharT* data() const noexcept { return data_.data(); }
    [[nodiscard]] CharT* data() noexcept { return data_.data(); }
    [[nodiscard]] view_type view() const noexcept { return view_type(data_); }
    [[nodiscard]] view_type to_string_view() const noexcept { return view_type(data_); }
    [[nodiscard]] std::basic_string<CharT> raw() const noexcept { return data_; }
    
    [[nodiscard]] std::string to_string() const {
        if constexpr (std::same_as<CharT, char>) {
            return data_;
        } else if constexpr (std::same_as<CharT, wchar_t>) {
            return encode<char,wchar_t>(data_);
        } else if constexpr (std::same_as<CharT, char8_t> || std::same_as<CharT, char16_t>) {
            return EncodingConverter::from_utf16(std::u16string_view(
                reinterpret_cast<const char16_t*>(data_.data()), data_.size()));
        } else {
            return EncodingConverter::from_utf32(view_type(data_));
        }
    }
    
    [[nodiscard]] std::wstring to_wstring() const {
        if constexpr (std::same_as<CharT, wchar_t>) {
            return data_;
        } else {
            return encode<wchar_t, CharT>(view());// (//EncodingConverter::to_wide<std::wstring>(to_string());
        }
    }
    
    template <typename OtherString>
    [[nodiscard]] OtherString to() const {
        if constexpr (std::same_as<OtherString, std::string>) {
            return to_string();
        } else if constexpr (std::same_as<OtherString, std::wstring>) {
            return to_wstring();
        } else if constexpr (std::same_as<OtherString, std::u16string>) {
            if constexpr (std::same_as<CharT, char16_t>) {
                return data_;
            } else {
                return EncodingConverter::to_utf16(to_string());
            }
        } else if constexpr (std::same_as<OtherString, std::u32string>) {
            if constexpr (std::same_as<CharT, char32_t>) {
                return data_;
            } else {
                return EncodingConverter::to_utf32(to_string());
            }
        } else {
            static_assert(always_false<OtherString>, "Unsupported target string type");
        }
    }
    
    // 获取 UTF-8 字符串（所有类型通用）
    [[nodiscard]] std::string to_utf8() const {
        return to_string();
    }
    
    // ========================================
    // 数值转换 - 模板成员方法
    // ========================================
    
    // 字符串 -> 整数（有符号）
    template <typename IntType>
    requires std::signed_integral<IntType>
    [[nodiscard]] std::optional<IntType> to_int() const noexcept {
        if (auto result = parse_number<IntType>(); result.has_value()) {
            if (result.value() >= (std::numeric_limits<IntType>::min)() &&
                result.value() <= (std::numeric_limits<IntType>::max)()) {
                return static_cast<IntType>(result.value());
            }
        }
        return std::nullopt;
    }
    
    template <typename IntType>
    requires std::signed_integral<IntType>
    [[nodiscard]] IntType to_int_or(IntType default_value) const noexcept {
        return to_int<IntType>().value_or(default_value);
    }
    
    // 字符串 -> 整数（无符号）
    template <typename UIntType>
    requires std::unsigned_integral<UIntType>
    [[nodiscard]] std::optional<UIntType> to_uint() const noexcept {
        if (auto result = parse_number<UIntType>(); result.has_value()) {
            if (result.value() >= 0 &&
                static_cast<uint64_t>(result.value()) <= (std::numeric_limits<UIntType>::max)()) {
                return static_cast<UIntType>(result.value());
            }
        }
        return std::nullopt;
    }
    
    template <typename UIntType>
    requires std::unsigned_integral<UIntType>
    [[nodiscard]] UIntType to_uint_or(UIntType default_value) const noexcept {
        return to_uint<UIntType>().value_or(default_value);
    }
    
    // 字符串 -> 浮点数
    template <typename FloatType>
    requires std::floating_point<FloatType>
    [[nodiscard]] std::optional<FloatType> to_float() const noexcept {
        if (auto result = parse_float<FloatType>(); result.has_value()) {
            if (std::isfinite(result.value())) {
                return result.value();
            }
        }
        return std::nullopt;
    }
    
    template <typename FloatType>
    requires std::floating_point<FloatType>
    [[nodiscard]] FloatType to_float_or(FloatType default_value) const noexcept {
        return to_float<FloatType>().value_or(default_value);
    }
    
    // 便捷方法
    [[nodiscard]] std::optional<int>      to_int()      const { return to_int<int>(); }
    [[nodiscard]] int                     to_int_or(int default_value) const { return to_int<int>().value_or(default_value); }
    [[nodiscard]] std::optional<long>     to_long()     const { return to_int<long>(); }
    [[nodiscard]] long                    to_long_or(long default_value) const { return to_long().value_or(default_value); }
    [[nodiscard]] std::optional<long long> to_llong()   const { return to_int<long long>(); }
    [[nodiscard]] long long               to_llong_or(long long default_value) const { return to_llong().value_or(default_value); }
    
    [[nodiscard]] std::optional<unsigned>    to_uint()      const { return to_uint<unsigned>(); }
    [[nodiscard]] unsigned                   to_uint_or(unsigned default_value) const { return to_uint<unsigned>().value_or(default_value); }
    [[nodiscard]] std::optional<unsigned long> to_ulong()   const { return to_uint<unsigned long>(); }
    [[nodiscard]] unsigned long              to_ulong_or(unsigned long default_value) const { return to_ulong().value_or(default_value); }
    [[nodiscard]] std::optional<unsigned long long> to_ullong() const { return to_uint<unsigned long long>(); }
    [[nodiscard]] unsigned long long         to_ullong_or(unsigned long long default_value) const { return to_ullong().value_or(default_value); }
    
    [[nodiscard]] std::optional<float>    to_float()    const { return to_float<float>(); }
    [[nodiscard]] float                   to_float_or(float default_value) const { return to_float<float>().value_or(default_value); }
    [[nodiscard]] std::optional<double>   to_double()   const { return to_float<double>(); }
    [[nodiscard]] double                  to_double_or(double default_value) const { return to_double().value_or(default_value); }
    
    // ========================================
    // 静态工厂方法 - 数字 -> 字符串
    // ========================================
    
    template <typename IntType>
        requires std::integral<IntType>
    static ModernString from(IntType value) {
        ModernString result;
        result.data_ = detail::to_string_safe(value);  // ← 使用 to_chars
        return result;
    }
    template <typename FloatType>
        requires std::floating_point<FloatType>
    static ModernString from(FloatType value) {
        ModernString result;
        result.data_ = detail::to_string_safe_float(value, 6);
        return result;
    }
    
    // 十六进制格式化
    template <typename UIntType>
		requires std::unsigned_integral<UIntType>
	static ModernString from_hex(UIntType value, bool uppercase = false) {
		ModernString result;
		if (uppercase) {
			result.data_ = std::format("{:X}", value);
		}
		else {
			result.data_ = std::format("{:x}", value);
		}
		return result;
	}
    
    // 带前导零的格式化
    template <typename NumericType>
    requires std::integral<NumericType>
    static ModernString from_number(NumericType value, size_type width, CharT fill = '0') {
        std::string num_str = detail::to_string_safe(value);
        ModernString result;
        if (num_str.size() < static_cast<size_t>(width)) {
            result.data_.append(static_cast<size_t>(width) - num_str.size(),
                static_cast<CharT>(fill));
            result.data_ += num_str;
        }
        else {
            result.data_ = num_str;
        }
        return result;
    }
    
    static ModernString from_float(float value, int precision = 6) {
        ModernString result;
        result.data_ = std::format("{:.{}f}", value, precision);
        return result;
    }
    
    static ModernString from_double(double value, int precision = 6) {
        ModernString result;
        result.data_ = std::format("{:.{}f}", value, precision);
        return result;
    }
    
    // ========================================
    // 比较操作
    // ========================================
    
    [[nodiscard]] int compare(const ModernString& other) const noexcept {
        return data_.compare(other.data_);
    }
    
    [[nodiscard]] int compare(view_type view) const noexcept {
        return data_.compare(view);
    }
    
    [[nodiscard]] int compare(size_type pos, size_type count, view_type view) const {
        return data_.compare(pos, count, view);
    }
    
    // 运算符比较
    [[nodiscard]] friend bool operator==(const ModernString& lhs, const ModernString& rhs) noexcept {
        return lhs.data_ == rhs.data_;
    }
    
    [[nodiscard]] friend bool operator==(const ModernString& lhs, view_type rhs) noexcept {
        return lhs.data_ == rhs;
    }
    
    [[nodiscard]] friend bool operator==(view_type lhs, const ModernString& rhs) noexcept {
        return lhs == rhs.data_;
    }
    
    [[nodiscard]] friend auto operator<=>(const ModernString& lhs, const ModernString& rhs) noexcept {
        return lhs.data_ <=> rhs.data_;
    }
    
    #ifndef _MSC_VER
    [[nodiscard]] friend auto operator<=>(const ModernString& lhs, view_type rhs) noexcept {
        return lhs.data_ <=> rhs;
    }
    #endif
    
    [[nodiscard]] bool operator!=(const ModernString& other) const noexcept {
        return !(*this == other);
    }
    
    [[nodiscard]] bool operator<(const ModernString& other) const noexcept {
        return data_ < other.data_;
    }
    
    [[nodiscard]] bool operator<=(const ModernString& other) const noexcept {
        return data_ <= other.data_;
    }
    
    [[nodiscard]] bool operator>(const ModernString& other) const noexcept {
        return data_ > other.data_;
    }
    
    [[nodiscard]] bool operator>=(const ModernString& other) const noexcept {
        return data_ >= other.data_;
    }
    
    // ========================================
    // 子字符串操作
    // ========================================
    
    [[nodiscard]] ModernString substr(size_type pos = 0, size_type count = npos) const {
        return ModernString(view_type(data_.substr(pos, count)));
    }
    
    [[nodiscard]] ModernString mid(size_type pos, size_type count = npos) const {
        return substr(pos, count);
    }
    
    [[nodiscard]] ModernString left(size_type count) const {
        if (count >= size()) return *this;
        return substr(0, count);
    }
    
    [[nodiscard]] ModernString right(size_type count) const {
        if (count >= size()) return *this;
        return substr(size() - count, count);
    }
    
    // ========================================
    // 查找操作
    // ========================================
    
    [[nodiscard]] size_type find(view_type str, size_type pos = 0) const noexcept {
        return data_.find(str, pos);
    }
    
    [[nodiscard]] size_type find(CharT ch, size_type pos = 0) const noexcept {
        return data_.find(ch, pos);
    }
    
    [[nodiscard]] size_type find_first_of(view_type chars, size_type pos = 0) const noexcept {
        return data_.find_first_of(chars, pos);
    }
    
    [[nodiscard]] size_type find_first_not_of(view_type chars, size_type pos = 0) const noexcept {
        return data_.find_first_not_of(chars, pos);
    }
    
    [[nodiscard]] size_type find_last_of(view_type chars, size_type pos = npos) const noexcept {
        return data_.find_last_of(chars, pos);
    }
    
    [[nodiscard]] size_type find_last_not_of(view_type chars, size_type pos = npos) const noexcept {
        return data_.find_last_not_of(chars, pos);
    }
    
    [[nodiscard]] size_type rfind(view_type str, size_type pos = npos) const noexcept {
        return data_.rfind(str, pos);
    }
    
    [[nodiscard]] size_type rfind(CharT ch, size_type pos = npos) const noexcept {
        return data_.rfind(ch, pos);
    }
    
    // ========================================
    // 字符串分隔（SPLIT）- 核心功能
    // ========================================
    
    // 基本分隔 - 按单个分隔符
    [[nodiscard]] std::vector<ModernString> split(CharT delimiter, bool skip_empty = false) const {
        std::vector<ModernString> result;
        size_type start = 0;
        size_type pos = 0;
        
        while ((pos = find(delimiter, start)) != npos) {
            if (!skip_empty || pos > start) {
                result.emplace_back(substr(start, pos - start));
            }
            start = pos + 1;
        }
        
        // 添加最后一个部分
        if (!skip_empty || start < size()) {
            result.emplace_back(substr(start));
        }
        
        return result;
    }
    
    // 多分隔符分隔（按任意一个字符分隔）
    [[nodiscard]] std::vector<ModernString> split(view_type delimiters, bool skip_empty = false) const {
        std::vector<ModernString> result;
        size_type start = 0;
        size_type pos = 0;
        
        while ((pos = find_first_of(delimiters, start)) != npos) {
            if (!skip_empty || pos > start) {
                result.emplace_back(substr(start, pos - start));
            }
            start = pos + 1;
        }
        
        // 添加最后一个部分
        if (!skip_empty || start < size()) {
            result.emplace_back(substr(start));
        }
        
        return result;
    }
    
    // 连续分隔符作为整体分隔
    [[nodiscard]] std::vector<ModernString> split_with_delim(view_type delimiters) const {
        std::vector<ModernString> parts;
        size_type pos = 0;
        
        while (pos < size()) {
            // 跳过连续的分隔符
            while (pos < size() && find_first_of(delimiters, pos) == pos) {
                ++pos;
            }
            
            if (pos >= size()) break;
            
            size_type end = find_first_of(delimiters, pos);
            if (end == npos) end = size();
            
            parts.emplace_back(substr(pos, end - pos));
            pos = end;
        }
        
        return parts;
    }
    
    // 正则表达式风格分隔（使用分隔符作为边界）
    [[nodiscard]] std::vector<ModernString> split_by(view_type pattern, bool include = false) const {
        std::vector<ModernString> parts;
        size_type pos = 0;
        size_type prev = 0;
        
        while ((pos = find(pattern, prev)) != npos) {
            parts.emplace_back(substr(prev, pos - prev));
            if (include) {
                parts.emplace_back(substr(pos, pattern.size()));
            }
            prev = pos + pattern.size();
        }
        
        parts.emplace_back(substr(prev));
        return parts;
    }
    
    // 使用 split_lines 分行（跨平台换行符处理）
    [[nodiscard]] std::vector<ModernString> split_lines() const {
        std::vector<ModernString> result;
        size_type start = 0;
        size_type pos = 0;
        
        while (start < size()) {
            pos = find('\n', start);
            
            if constexpr (std::same_as<CharT, char> || std::same_as<CharT, char8_t>) {
                // 检查 \r\n
                if (pos != npos && pos > start && data_[pos - 1] == '\r') {
                    result.emplace_back(substr(start, pos - start - 1));
                } else {
                    result.emplace_back(substr(start, pos == npos ? npos : pos - start));
                }
            } else {
                result.emplace_back(substr(start, pos == npos ? npos : pos - start));
            }
            
            if (pos == npos) break;
            start = pos + 1;
        }
        
        return result;
    }
    
    // 使用 splitChunks 按长度分割
    [[nodiscard]] std::vector<ModernString> split_chunks(size_type chunk_size) const {
        if (chunk_size == 0) return {};
        
        std::vector<ModernString> result;
        size_type num_chunks = (size() + chunk_size - 1) / chunk_size;
        
        for (size_type i = 0; i < num_chunks; ++i) {
            size_type start = i * chunk_size;
            size_type len = (std::min)(chunk_size, size() - start);
            result.emplace_back(substr(start, len));
        }
        
        return result;
    }
    
    // ========================================
    // 字符串修整（Trim）
    // ========================================
    
    [[nodiscard]] ModernString trim() const {
        size_type start = find_first_not_of(view_type(
            std::initializer_list<CharT>{' ', '\t', '\n', '\r', '\v', '\f'}));
        if (start == npos) return ModernString();
        
        size_type end = find_last_not_of(view_type(
            std::initializer_list<CharT>{' ', '\t', '\n', '\r', '\v', '\f'}));
        
        return substr(start, end - start + 1);
    }
    
    [[nodiscard]] ModernString trim_left() const {
        size_type pos = find_first_not_of(view_type(
            std::initializer_list<CharT>{' ', '\t', '\n', '\r', '\v', '\f'}));
        if (pos == npos) return ModernString();
        return substr(pos);
    }
    
    [[nodiscard]] ModernString trim_right() const {
        size_type pos = find_last_not_of(view_type(
            std::initializer_list<CharT>{' ', '\t', '\n', '\r', '\v', '\f'}));
        if (pos == npos) return ModernString();
        return substr(0, pos + 1);
    }
    
    [[nodiscard]] ModernString trim(view_type chars) const {
        size_type start = find_first_not_of(chars);
        if (start == npos) return ModernString();
        
        size_type end = find_last_not_of(chars);
        return substr(start, end - start + 1);
    }
    
    [[nodiscard]] ModernString trim_left(view_type chars) const {
        size_type pos = find_first_not_of(chars);
        if (pos == npos) return ModernString();
        return substr(pos);
    }
    
    [[nodiscard]] ModernString trim_right(view_type chars) const {
        size_type pos = find_last_not_of(chars);
        if (pos == npos) return ModernString();
        return substr(0, pos + 1);
    }
    
    // ========================================
    // 大小写转换
    // ========================================
    
    [[nodiscard]] ModernString to_upper() const {
        ModernString result = *this;
        std::ranges::transform(result.data_, result.data_.begin(), 
            [](CharT c) -> CharT {
                if constexpr (std::same_as<CharT, char>) {
                    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                } else if constexpr (std::same_as<CharT, wchar_t>) {
                    return static_cast<wchar_t>(towupper(c));
                } else {
                    // 对于 Unicode 字符，需要更复杂的处理
                    return c; // 简化处理
                }
            });
        return result;
    }
    
    [[nodiscard]] ModernString to_lower() const {
        ModernString result = *this;
        std::ranges::transform(result.data_, result.data_.begin(),
            [](CharT c) -> CharT {
                if constexpr (std::same_as<CharT, char>) {
                    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                } else if constexpr (std::same_as<CharT, wchar_t>) {
                    return static_cast<wchar_t>(std::tolower(c));
                } else {
                    return c; // 简化处理
                }
            });
        return result;
    }
    
    [[nodiscard]] ModernString reversed() const {
        ModernString result = *this;
        std::ranges::reverse(result.data_);
        return result;
    }
    
    // ========================================
    // 替换操作
    // ========================================
    
    [[nodiscard]] ModernString replace(view_type old_str, view_type new_str) const {
        ModernString result;
        size_type pos = 0;
        size_type old_len = old_str.size();
        size_type new_len = new_str.size();
        
        while ((pos = find(old_str, pos)) != npos) {
            result += substr(0, pos);
            result += new_str;
            pos += old_len;
        }
		if (pos != npos)
			result += substr(pos);

        return result;
    }
    
    [[nodiscard]] ModernString replace_all(view_type old_str, view_type new_str) const {
        return replace(old_str, new_str);
    }
    
    [[nodiscard]] ModernString replace_first(view_type old_str, view_type new_str, size_type from = 0) const {
        size_type pos = find(old_str, from);
        if (pos == npos) return *this;
        
        ModernString result = substr(0, pos) + new_str + substr(pos + old_str.size());
        return result;
    }
    
    // 替换单个字符
    [[nodiscard]] ModernString replace(CharT old_ch, CharT new_ch) const {
        ModernString result = *this;
        std::ranges::replace(result.data_, old_ch, new_ch);
        return result;
    }
    
    // ========================================
    // 连接操作
    // ========================================
    
    template <typename Range>
    [[nodiscard]] static ModernString join(const Range& range, view_type separator = view_type()) {
        ModernString result;
        bool first = true;
        
        for (const auto& str : range) {
            if (!first) result += separator;
            result += str;
            first = false;
        }
        
        return result;
    }
    
    template <typename Iter>
    [[nodiscard]] static ModernString join_range(Iter first, Iter last, view_type separator = view_type()) {
        ModernString result;
        bool first_elem = true;
        
        for (auto it = first; it != last; ++it) {
            if (!first_elem) result += separator;
            result += *it;
            first_elem = false;
        }
        
        return result;
    }
    
    // ========================================
    // 包含和前缀/后缀检查
    // ========================================
    
    [[nodiscard]] bool contains(view_type str) const noexcept {
        return find(str) != npos;
    }
    
    [[nodiscard]] bool contains(CharT ch) const noexcept {
        return find(ch) != npos;
    }
    
    [[nodiscard]] bool starts_with(view_type prefix) const noexcept {
        return size() >= prefix.size() && substr(0, prefix.size()) == prefix;
    }
    
    [[nodiscard]] bool starts_with(CharT ch) const noexcept {
        return !empty() && front() == ch;
    }
    
    [[nodiscard]] bool ends_with(view_type suffix) const noexcept {
        return size() >= suffix.size() && substr(size() - suffix.size()) == suffix;
    }
    
    [[nodiscard]] bool ends_with(CharT ch) const noexcept {
        return !empty() && back() == ch;
    }
    
    // ========================================
    // 重复和填充
    // ========================================
    
    [[nodiscard]] ModernString repeated(size_type count) const {
        if (count == 0 || empty()) return ModernString();
        if (count == 1) return *this;
        
        ModernString result;
        result.data_.reserve(size() * count);
        for (size_type i = 0; i < count; ++i) {
            result.data_ += data_;
        }
        return result;
    }
    
    [[nodiscard]] ModernString padded_left(size_type width, CharT fill = ' ') const {
        if (size() >= width) return *this;
        ModernString result;
        result.data_.reserve(width);
        result.data_.append(width - size(), fill);
        result.data_ += data_;
        return result;
    }
    
    [[nodiscard]] ModernString padded_right(size_type width, CharT fill = ' ') const {
        if (size() >= width) return *this;
        ModernString result = *this;
        result.data_.append(width - size(), fill);
        return result;
    }
    
    [[nodiscard]] ModernString centered(size_type width, CharT fill = ' ') const {
        if (size() >= width) return *this;
        size_type pad_total = width - size();
        size_type pad_left = pad_total / 2;
        size_type pad_right = pad_total - pad_left;
        
        ModernString result;
        result.data_.reserve(width);
        result.data_.append(pad_left, fill);
        result.data_ += data_;
        result.data_.append(pad_right, fill);
        return result;
    }
    
    // ========================================
    // 运算符重载（+）
    // ========================================
    
    friend ModernString operator+(const ModernString& lhs, const ModernString& rhs) {
        ModernString result = lhs;
        result += rhs;
        return result;
    }
    
    friend ModernString operator+(const ModernString& lhs, view_type rhs) {
        ModernString result = lhs;
        result += rhs;
        return result;
    }
    
    friend ModernString operator+(view_type lhs, const ModernString& rhs) {
        ModernString result;
        result.data_ = lhs;
        result += rhs;
        return result;
    }
    
    friend ModernString operator+(const ModernString& lhs, CharT rhs) {
        ModernString result = lhs;
        result.data_ += rhs;
        return result;
    }
    friend ModernString operator+(const ModernString& lhs,const CharT* rhs) {
        ModernString result = lhs;
        result.data_ += rhs;
        return result;
    }
    friend ModernString operator+(CharT lhs, const ModernString& rhs) {
        ModernString result;
        result += lhs;
        result += rhs;
        return result;
    }
    
    // ========================================
    // 转换函数 - 转换为其他 ModernString 类型
    // ========================================
    
    template <typename OtherChar>
    [[nodiscard]] ModernString<OtherChar> as() const {
        return ModernString<OtherChar>(*this);
    }
    
    [[nodiscard]] ModernString<char>    as_char()    const { return as<char>(); }
    [[nodiscard]] ModernString<char8_t> as_char8()   const { return as<char8_t>(); }
    [[nodiscard]] ModernString<wchar_t> as_wchar()   const { return as<wchar_t>(); }
    [[nodiscard]] ModernString<char16_t> as_char16()  const { return as<char16_t>(); }
    [[nodiscard]] ModernString<char32_t> as_char32()  const { return as<char32_t>(); }
    
    // ========================================
    // 序列化
    // ========================================
    
    [[nodiscard]] std::string serialize() const {
        // 将字符串序列化为一进制格式
        // 格式：长度（4字节）+ 内容
        auto result = to_string();
        uint32_t len = static_cast<uint32_t>(result.size());
        std::string serialized;
        serialized.reserve(sizeof(len) + len);
        serialized.append(reinterpret_cast<const char*>(&len), sizeof(len));
        serialized += result;
        return serialized;
    }
    
    static ModernString deserialize(std::string_view data) {
        if (data.size() < sizeof(uint32_t)) {
            throw std::runtime_error("Invalid serialized data");
        }
        uint32_t len = *reinterpret_cast<const uint32_t*>(data.data());
        if (data.size() < sizeof(len) + len) {
            throw std::runtime_error("Invalid serialized data size");
        }
        return ModernString(data.substr(sizeof(len), len));
    }
    
    // 辅助类型声明
private:
    template <typename T>
    static constexpr bool always_false = false;
};

// 比较运算符（不同 ModernString 类型之间）
template <typename Char1, typename Char2>
[[nodiscard]] bool operator==(const ModernString<Char1>& lhs, const ModernString<Char2>& rhs) noexcept {
    return lhs.to_string() == rhs.to_string();
}

// ============================================
// 全局便利函数
// ============================================

template <typename T>
ModernString(T) -> ModernString<std::remove_cvref_t<std::ranges::range_value_t<T>>>;

// 连接多个字符串
template <typename... Args>
[[nodiscard]] ModernString<char> concat(Args&&... args) {
    return ModernString<char>::from(std::format("{}...{}", std::forward<Args>(args)...));
}

// 构建字符串
template <typename... Args>
[[nodiscard]] ModernString<char> format(Args&&... args) {
    return ModernString<char>::from(std::format(std::forward<Args>(args)...));
}

// ============================================
// 编码转换便捷函数
// ============================================

namespace encoding {

// UTF-8 到其他编码
[[nodiscard]] inline ModernString<char16_t> utf8_to_utf16(const ModernString<char>& str) {
    return ModernString<char16_t>(EncodingConverter::to_utf16(str.to_string_view()));
}

[[nodiscard]] inline ModernString<char32_t> utf8_to_utf32(const ModernString<char>& str) {
    return ModernString<char32_t>(EncodingConverter::to_utf32(str.to_string_view()));
}

//[[nodiscard]] inline ModernString<wchar_t> utf8_to_wide(const ModernString<char>& str) {
//    return ModernString<wchar_t>(EncodingConverter::to_wide<std::wstring>(str.to_string_view()));
//}

// 其他编码到 UTF-8
[[nodiscard]] inline ModernString<char> utf16_to_utf8(const ModernString<char16_t>& str) {
    return ModernString<char>(EncodingConverter::from_utf16(str.to_string_view()));
}

[[nodiscard]] inline ModernString<char> utf32_to_utf8(const ModernString<char32_t>& str) {
    return ModernString<char>(EncodingConverter::from_utf32(str.to_string_view()));
}

//[[nodiscard]] inline ModernString<char> wide_to_utf8(const ModernString<wchar_t>& str) {
//    return ModernString<char>(EncodingConverter::from_wide<std::string>(str.to_string_view()));
//}

// UTF-16 到 UTF-32
[[nodiscard]] inline ModernString<char32_t> utf16_to_utf32(const ModernString<char16_t>& str) {
    return ModernString<char32_t>(EncodingConverter::utf16_to_utf32(str.to_string_view()));
}

// UTF-32 到 UTF-16
[[nodiscard]] inline ModernString<char16_t> utf32_to_utf16(const ModernString<char32_t>& str) {
    return ModernString<char16_t>(EncodingConverter::utf32_to_utf16(str.to_string_view()));
}

} // namespace encoding



// ============================================
// 流输出运算符重载
// ============================================

template <typename CharT>
std::basic_ostream<CharT>& operator<<(std::basic_ostream<CharT>& os,
    const ModernString<CharT>& str) {
    os << str.to_string_view();
    return os;
}

//template <typename CharT>
//ModernString<CharT>& operator+(ModernString<CharT>& os,
//    const ModernString<CharT>& str) {
//    os+= str.to_string_view();
//    return os;
//}
//template <typename CharT>
//ModernString<CharT>& operator+(ModernString<CharT>& os,
//    const CharT* str) {
//    os += str;
//    return os;
//}

//// 宽字符特殊化
//template <>
//inline std::wostream& operator<<(std::wostream& os, const ModernString<wchar_t>& str) {
//    os << str.view();
//    return os;
//}



// 类型别名
using String = ModernString<char>;
using WString = ModernString<wchar_t>;
using U8String = ModernString<char8_t>;
using U16String = ModernString<char16_t>;
using U32String = ModernString<char32_t>;

} // namespace modern_string

#endif // MODERN_STRING_H
