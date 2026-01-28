#ifndef DAV_RESOURCE_HPP
#define DAV_RESOURCE_HPP

#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <boost/asio/io_context.hpp>
#include "core/async_fs.hpp"
#include "dav_namespaces.hpp"

namespace webdav {

// 锁信息
struct DavLock {
    std::string token;
    std::string owner;
    std::chrono::system_clock::time_point expires;
    bool exclusive; // true = exclusive, false = shared
    std::string path;
};

// 资源类型
enum class ResourceType {
    FILE,
    COLLECTION
};

// DAV资源接口
class IDavResource {
public:
    virtual ~IDavResource() = default;
    
    virtual boost::asio::awaitable<void> get(std::ostream& output) = 0;
    virtual boost::asio::awaitable<void> put(std::istream& input, std::size_t size) = 0;
    virtual boost::asio::awaitable<bool> remove() = 0;
    virtual boost::asio::awaitable<void> copy(const std::filesystem::path& dest) = 0;
    virtual boost::asio::awaitable<void> move(const std::filesystem::path& dest) = 0;
    
    virtual ResourceType get_type() const = 0;
    virtual std::uint64_t get_size() const = 0;
    virtual std::filesystem::path get_path() const = 0;
    
    virtual boost::asio::awaitable<void> get_properties(DavXmlBuilder& builder) = 0;
    
    // 锁相关
    virtual boost::asio::awaitable<DavLock> lock(std::string_view owner, 
                                                  bool exclusive) = 0;
    virtual boost::asio::awaitable<bool> unlock(std::string_view token) = 0;
    virtual std::vector<DavLock> get_locks() const = 0;
};

// 文件资源实现
class DavFile : public IDavResource {
public:
    DavFile(const boost::asio::any_io_executor& io_context,
            core::IAsyncFileSystem& fs,
            const std::filesystem::path& path);
    
    boost::asio::awaitable<void> get(std::ostream& output) override;
    boost::asio::awaitable<void> put(std::istream& input, std::size_t size) override;
    boost::asio::awaitable<bool> remove() override;
    boost::asio::awaitable<void> copy(const std::filesystem::path& dest) override;
    boost::asio::awaitable<void> move(const std::filesystem::path& dest) override;
    
    ResourceType get_type() const override { return ResourceType::FILE; }
    std::uint64_t get_size() const override { 
        std::error_code ec;
        return std::filesystem::file_size(path_, ec);
    }
    std::filesystem::path get_path() const override { return path_; }
    
    boost::asio::awaitable<void> get_properties(DavXmlBuilder& builder) override;
    
    boost::asio::awaitable<DavLock> lock(std::string_view owner, 
                                          bool exclusive) override;
    boost::asio::awaitable<bool> unlock(std::string_view token) override;
    std::vector<DavLock> get_locks() const override {
        std::lock_guard<std::mutex> lock(lock_mutex_);
        return active_locks_;
    }

private:
    const boost::asio::any_io_executor& io_context_;
    core::IAsyncFileSystem& file_system_;
    std::filesystem::path path_;
    
    mutable std::mutex lock_mutex_;
    std::vector<DavLock> active_locks_;
};

// 集合（目录）资源实现
class DavCollection : public IDavResource {
public:
    DavCollection(const boost::asio::any_io_executor& io_context,
                  core::IAsyncFileSystem& fs,
                  const std::filesystem::path& path);
    
    boost::asio::awaitable<void> get(std::ostream& output) override;
    boost::asio::awaitable<void> put(std::istream& input, std::size_t size) override;
    boost::asio::awaitable<bool> remove() override;
    boost::asio::awaitable<void> copy(const std::filesystem::path& dest) override;
    boost::asio::awaitable<void> move(const std::filesystem::path& dest) override;
    
    ResourceType get_type() const override { return ResourceType::COLLECTION; }
    std::uint64_t get_size() const override { return 0; }
    std::filesystem::path get_path() const override { return path_; }
    
    boost::asio::awaitable<void> get_properties(DavXmlBuilder& builder) override;
    
    boost::asio::awaitable<DavLock> lock(std::string_view owner, 
                                          bool exclusive) override;
    boost::asio::awaitable<bool> unlock(std::string_view token) override;
    std::vector<DavLock> get_locks() const override {
        std::lock_guard<std::mutex> lock(lock_mutex_);
        return active_locks_;
    }
    
    // 添加子资源到集合
    boost::asio::awaitable<void> create_child(std::string_view name, bool collection);

private:
    const boost::asio::any_io_executor& io_context_;
    core::IAsyncFileSystem& file_system_;
    std::filesystem::path path_;
    
    mutable std::mutex lock_mutex_;
    std::vector<DavLock> active_locks_;
};

// 资源工厂
class DavResourceFactory {
public:
    DavResourceFactory(const boost::asio::any_io_executor& io_context,
                       std::shared_ptr<core::IAsyncFileSystem> fs);
    
    std::unique_ptr<IDavResource> create(const std::filesystem::path& path);
    
    void set_root(const std::filesystem::path& root) { root_ = root; }
    const std::filesystem::path& get_root() const { return root_; }
    
    // 验证路径是否在root下
    bool validate_path(const std::filesystem::path& path);

private:
    const boost::asio::any_io_executor& io_context_;
    std::shared_ptr<core::IAsyncFileSystem> file_system_;
    std::filesystem::path root_;
};

} // namespace webdav

#endif // DAV_RESOURCE_HPP
