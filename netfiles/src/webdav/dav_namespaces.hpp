#ifndef DAV_NAMESPACES_HPP
#define DAV_NAMESPACES_HPP

#include <array>
#include <string_view>

namespace webdav {

// WebDAV命名空间定义
struct DavNamespace {
    std::string_view uri;
    std::string_view prefix;
};

constexpr std::array dav_namespaces = {
    DavNamespace{ "DAV:", "d" },
    DavNamespace{ "http://apache.org/dav/props", "win32" }
};

// 属性名称
struct DavProperty {
    std::string_view namespace_uri;
    std::string_view name;
    std::string value;
};

// 常用DAV属性
namespace dav_props {
    constexpr std::string_view getcontentlength = "getcontentlength";
    constexpr std::string_view getlastmodified = "getlastmodified";
    constexpr std::string_view creationdate = "creationdate";
    constexpr std::string_view displayname = "displayname";
    constexpr std::string_view resourcetype = "resourcetype";
    constexpr std::string_view iscollection = "iscollection";
    constexpr std::string_view supportedlock = "supportedlock";
    constexpr std::string_view lockdiscovery = "lockdiscovery";
}

// 构建XML元素
class DavXmlBuilder {
public:
    // 添加声明
    DavXmlBuilder& add_declaration() {
        xml_ += R"(<?xml version="1.0" encoding="UTF-8"?>)";
        return *this;
    }
    
    // 添加DAV命名空间
    DavXmlBuilder& add_dav_ns() {
        xml_ += R"(<D:multistatus xmlns:D="DAV:">)";
        return *this;
    }
    
    // 开始响应元素
    DavXmlBuilder& start_response(std::string_view href) {
        xml_ += std::format(R"(<D:response xmlns:D="DAV:"><D:href>{}</D:href>)", 
            escape_xml(href));
        return *this;
    }
    
    // 结束响应元素
    DavXmlBuilder& end_response() {
        xml_ += "</D:response>";
        return *this;
    }
    
    // 添加propstat元素
    DavXmlBuilder& start_propstat() {
        xml_ += "<D:propstat>";
        return *this;
    }
    
    // 结束propstat元素
    DavXmlBuilder& end_propstat() {
        xml_ += "</D:propstat>";
        return *this;
    }
    
    // 开始prop元素
    DavXmlBuilder& start_prop() {
        xml_ += "<D:prop>";
        return *this;
    }
    
    // 结束prop元素
    DavXmlBuilder& end_prop() {
        xml_ += "</D:prop>";
        return *this;
    }
    
    // 添加状态元素
    DavXmlBuilder& add_status(int code, std::string_view msg) {
        xml_ += std::format("<D:status>HTTP/1.1 {} {}</D:status>", code, msg);
        return *this;
    }
    
    // 添加内容长度属性
    DavXmlBuilder& add_contentlength(std::uint64_t size) {
        xml_ += std::format("<D:getcontentlength>{}</D:getcontentlength>", size);
        return *this;
    }
    
    // 添加最后修改时间属性
    DavXmlBuilder& add_lastmodified(std::string_view date) {
        xml_ += std::format("<D:getlastmodified>{}</D:getlastmodified>", date);
        return *this;
    }
    
    // 添加类型属性（文件或集合）
    DavXmlBuilder& add_resourcetype(bool is_collection) {
        if (is_collection) {
            xml_ += R"(<D:resourcetype><D:collection/></D:resourcetype>)";
        } else {
            xml_ += "<D:resourcetype/>";
        }
        return *this;
    }
    
    // 添加创建时间
    DavXmlBuilder& add_creationdate(std::string_view date) {
        xml_ += std::format("<D:creationdate>{}</D:creationdate>", date);
        return *this;
    }
    
    // 添加显示名称
    DavXmlBuilder& add_displayname(std::string_view name) {
        xml_ += std::format("<D:displayname>{}</D:displayname>", 
            escape_xml(name));
        return *this;
    }
    
    // 添加受支持锁属性
    DavXmlBuilder& add_supportedlock() {
        xml_ += R"(<D:supportedlock>
            <D:lockentry>
                <D:lockscope><D:exclusive/></D:lockscope>
                <D:locktype><D:write/></D:locktype>
            </D:lockentry>
            <D:lockentry>
                <D:lockscope><D:shared/></D:lockscope>
                <D:locktype><D:write/></D:locktype>
            </D:lockentry>
        </D:supportedlock>)";
        return *this;
    }
    
    // 开始multistatus
    DavXmlBuilder& start_multistatus() {
        xml_ += R"(<D:multistatus xmlns:D="DAV:">)";
        return *this;
    }
    
    // 结束multistatus
    DavXmlBuilder& end_multistatus() {
        xml_ += "</D:multistatus>";
        return *this;
    }
    
    std::string build() {
        return std::move(xml_);
    }
    
    void reset() {
        xml_.clear();
    }
    
private:
    std::string xml_;
    
    static std::string escape_xml(std::string_view str) {
        std::string result;
        result.reserve(str.size());
        
        for (char c : str) {
            switch (c) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&apos;"; break;
 default: result += c; break;
            }
        }
        
        return result;
    }
};

} // namespace webdav

#endif // DAV_NAMESPACES_HPP
