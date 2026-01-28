#include "dav_resource.hpp"
#include "core/utils.hpp"
#include <fstream>
#include <sstream>
#include "http_parser.hpp"
namespace webdav {

// ============ DavFile 实现 ============

DavFile::DavFile(const boost::asio::any_io_executor& io_context,
                 core::IAsyncFileSystem& fs,
                 const std::filesystem::path& path)
    : io_context_(io_context)
    , file_system_(fs)
    , path_(path)
{
}

boost::asio::awaitable<void> DavFile::get(std::ostream& output) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path_, ec);
    
    if (ec) {
        throw std::runtime_error("Cannot get file size");
    }
    
    // 使用流式读取
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file");
    }
    
    constexpr std::size_t BUFFER_SIZE = 64 * 1024;
    char buffer[BUFFER_SIZE];
    
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        output.write(buffer, file.gcount());
    }
    
    co_return;
}

boost::asio::awaitable<void> DavFile::put(std::istream& input, std::size_t size) {
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create file");
    }
    
    constexpr std::size_t BUFFER_SIZE = 64 * 1024;
    char buffer[BUFFER_SIZE];
    std::size_t remaining = size;
    
    while (remaining > 0) {
        std::size_t to_read = std::min(BUFFER_SIZE, remaining);
        input.read(buffer, to_read);
        std::streamsize actual = input.gcount();
        
        if (actual > 0) {
            file.write(buffer, actual);
        }
        
        remaining -= actual;
    }
    
    co_return;
}

boost::asio::awaitable<bool> DavFile::remove() {
    co_await file_system_.remove_file(path_.string());
    co_return true;
}

boost::asio::awaitable<void> DavFile::copy(const std::filesystem::path& dest) {
    // 简化实现：同步复制
    std::error_code ec;
    std::filesystem::copy_file(path_, dest, 
        std::filesystem::copy_options::overwrite_existing, ec);
    
    if (ec) {
        throw std::runtime_error("Copy failed: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> DavFile::move(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::rename(path_, dest, ec);
    
    if (ec) {
        throw std::runtime_error("Move failed: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> DavFile::get_properties(DavXmlBuilder& builder) {
    std::error_code ec;
    auto last_write = std::filesystem::last_write_time(path_, ec);
    
    // 获取时间字符串
    /*auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(last_write);
    auto epoch = sctp.time_since_epoch();
    std::time_t time = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() - 
        (std::chrono::system_clock::now() - 
         std::chrono::system_clock::from_time_t(epoch)));*/

    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    time_t time = std::chrono::system_clock::to_time_t(sctp);

    std::tm* tm_info = std::localtime(&time);
    char date_buf[64];
    std::strftime(date_buf, sizeof(date_buf), 
        "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    builder.add_contentlength(get_size())
           .add_lastmodified(date_buf)
           .add_creationdate(date_buf)
           .add_displayname(HttpParser::encodeFilename(path_.filename().string()))
           .add_resourcetype(false);
    
    co_return;
}

boost::asio::awaitable<DavLock> DavFile::lock(std::string_view owner, 
                                               bool exclusive) {
    DavLock lock;
    lock.token = std::format("urn:uuid:{:x}", 
        std::chrono::system_clock::now().time_since_epoch().count());
    lock.owner = std::string(owner);
    lock.expires = std::chrono::system_clock::now() + std::chrono::minutes(15);
    lock.exclusive = exclusive;
    lock.path = HttpParser::encodeFilename(path_.string());
    
    std::lock_guard<std::mutex> guard(lock_mutex_);
    active_locks_.push_back(lock);
    
    co_return lock;
}

boost::asio::awaitable<bool> DavFile::unlock(std::string_view token) {
    std::lock_guard<std::mutex> guard(lock_mutex_);
    
    auto it = std::find_if(active_locks_.begin(), active_locks_.end(),
        [token](const DavLock& l) { return l.token == token; });
    
    if (it != active_locks_.end()) {
        active_locks_.erase(it);
        co_return true;
    }
    
    co_return false;
}

// ============ DavCollection 实现 ============

DavCollection::DavCollection(const boost::asio::any_io_executor& io_context,
                             core::IAsyncFileSystem& fs,
                             const std::filesystem::path& path)
    : io_context_(io_context)
    , file_system_(fs)
    , path_(path)
{
}

boost::asio::awaitable<void> DavCollection::get(std::ostream& output) {
    // PROPFIND响应由handler处理
    co_return;
}

boost::asio::awaitable<void> DavCollection::put(std::istream& input, 
                                                  std::size_t size) {
    throw std::runtime_error("Cannot PUT to a collection");
}

boost::asio::awaitable<bool> DavCollection::remove() {
    co_await file_system_.remove_directory(path_.string());
    co_return true;
}

boost::asio::awaitable<void> DavCollection::copy(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::copy(path_, dest, 
        std::filesystem::copy_options::recursive | 
        std::filesystem::copy_options::overwrite_existing, ec);
    
    if (ec) {
        throw std::runtime_error("Copy failed: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> DavCollection::move(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::rename(path_, dest, ec);
    
    if (ec) {
        throw std::runtime_error("Move failed: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> DavCollection::get_properties(DavXmlBuilder& builder) {
    builder.add_creationdate("Thu, 01 Jan 1970 00:00:00 GMT")
           .add_displayname(HttpParser::encodeFilename(path_.filename().string()))
           .add_resourcetype(true);
    
    co_return;
}

boost::asio::awaitable<DavLock> DavCollection::lock(std::string_view owner, 
                                                     bool exclusive) {
    DavLock lock;
    lock.token = std::format("urn:uuid:{:x}", 
        std::chrono::system_clock::now().time_since_epoch().count());
    lock.owner = std::string(owner);
    lock.expires = std::chrono::system_clock::now() + std::chrono::minutes(15);
    lock.exclusive = exclusive;
    lock.path = HttpParser::encodeFilename(path_.string());
    
    std::lock_guard<std::mutex> guard(lock_mutex_);
    active_locks_.push_back(lock);
    
    co_return lock;
}

boost::asio::awaitable<bool> DavCollection::unlock(std::string_view token) {
    std::lock_guard<std::mutex> guard(lock_mutex_);
    
    auto it = std::find_if(active_locks_.begin(), active_locks_.end(),
        [token](const DavLock& l) { return l.token == token; });
    
    if (it != active_locks_.end()) {
        active_locks_.erase(it);
        co_return true;
    }
    
    co_return false;
}

boost::asio::awaitable<void> DavCollection::create_child(std::string_view name, 
                                                          bool collection) {
    std::filesystem::path child = path_ / name;
    
    if (collection) {
        co_await file_system_.create_directory(child.string());
    } else {
        // 创建空文件
        std::ofstream(child.string()).close();
    }
    
    co_return;
}

// ============ DavResourceFactory 实现 ============

DavResourceFactory::DavResourceFactory(const boost::asio::any_io_executor& io_context,
                                       std::shared_ptr<core::IAsyncFileSystem> fs)
    : io_context_(io_context)
    , file_system_(std::move(fs))
{
}

std::unique_ptr<IDavResource> DavResourceFactory::create(
    const std::filesystem::path& path) 
{
    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);
    
    if (ec || !exists) {
        return nullptr;
    }
    
    bool is_dir = std::filesystem::is_directory(path);
    
    if (is_dir) {
        return std::make_unique<DavCollection>(io_context_, *file_system_, path);
    } else {
        return std::make_unique<DavFile>(io_context_, *file_system_, path);
    }
}

bool DavResourceFactory::validate_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    if (ec) return false;
    
    auto root_canonical = std::filesystem::canonical(root_, ec);
    if (ec) return false;
    
    return canonical.string().find(root_canonical.string()) == 0;
}

} // namespace webdav
