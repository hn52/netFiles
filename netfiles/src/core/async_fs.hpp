#ifndef ASYNC_FS_HPP
#define ASYNC_FS_HPP

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>
#include <expected>
#include <boost/asio/io_context.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/awaitable.hpp>
#include<cstdio>
namespace core {

// 文件系统条目
struct FileEntry {
    std::filesystem::path path;
    std::uint64_t size;
    bool is_directory;
    std::filesystem::file_time_type last_modified;
    std::filesystem::perms permissions;
};

// 文件打开标志
enum class FileFlags {
    read = std::ios::in,
    write = std::ios::out,
    append = std::ios::app,
    binary = std::ios::binary
};

/**
 * 异步文件系统接口
 * 提供跨平台的异步文件操作
 */
class IAsyncFileSystem {
public:
    virtual ~IAsyncFileSystem() = default;

    // 目录操作
    virtual boost::asio::awaitable<std::vector<FileEntry>> read_directory(
        std::string_view path) = 0;
    
    virtual boost::asio::awaitable<void> create_directory(
        std::string_view path) = 0;
    
    virtual boost::asio::awaitable<void> remove_directory(
        std::string_view path) = 0;

    // 文件操作
    virtual boost::asio::awaitable<std::uint64_t> file_size(
        std::string_view path) = 0;
    
    virtual boost::asio::awaitable<bool> exists(
        std::string_view path) = 0;
    
    virtual boost::asio::awaitable<void> remove_file(
        std::string_view path) = 0;
    
    virtual boost::asio::awaitable<void> rename(
        std::string_view from, std::string_view to) = 0;

    // 异步读取（流式）
    virtual boost::asio::awaitable<std::size_t> read_file(
        std::string_view path,
        boost::asio::mutable_buffer buffer,
        std::uint64_t offset = 0) = 0;
    
    // 异步写入
    virtual boost::asio::awaitable<std::size_t> write_file(
        std::string_view path,
        boost::asio::const_buffer buffer,
        std::uint64_t offset = 0) = 0;
};



class CNtFile {
private:
    //std::fstream file;
    FILE* fp = 0;
    size_t fileSize;
public:
    // 构造函数
    CNtFile(const std::string& path, bool readOnly = true)
         {
        fileSize = std::filesystem::file_size(path);
#ifdef _WIN32
        // Windows: 使用二进制模式
        /*file.open(path,
            std::ios::binary |
            (readOnly ? std::ios::in : std::ios::out | std::ios::trunc));*/
        fp=std::fopen(path.c_str(), (readOnly ? "rb" : "r+"));
#else
        // Unix/Linux/macOS: 使用O_LARGEFILE标志（自动启用）
        file.open(path,
            std::ios::binary |
            (readOnly ? std::ios::in : std::ios::out | std::ios::trunc));
#endif

        if (!fp) {
            throw std::runtime_error("无法打开文件: " + path);
        }
    }
    bool IsOpen() {
        return fp;
    }
    // 获取文件大小
    size_t GetFileSize() {
        /*file.seekg(0, std::ios::end);
        fileSize = file.tellg();
        file.seekg(0, std::ios::beg);*/
       
        return fileSize;
    }
    // 读取数据到缓冲区
    size_t Read(size_t offset, void* buffer, size_t size) {
       // file.seekg(offset);
        /*if (!file.good()) return false;

        file.read(static_cast<char*>(buffer), size);
        return file.gcount();*/
        //return !file.bad();
        return fread(buffer, 1, size, fp);

    }
    // 从缓冲区写入数据
    size_t Write(size_t offset, const void* buffer, size_t size) {
       // file.seekp(offset);
        //if (!file.good()) return false;

        //file.write(static_cast<const char*>(buffer), size);
        ////return !file.bad();
        //return file.gcount();
        return fwrite(buffer, 1, size, fp);
    }
    // 追加写入
    size_t Append(const void* buffer, size_t size) {
        //file.seekp(0, std::ios::end);
        //file.write(static_cast<const char*>(buffer), size);
        ////return !file.bad();
        //return file.gcount();
        fseek(fp, 0, SEEK_END);
        return fwrite(buffer, 1, size, fp);
    }
    // 刷新缓冲区
    void Flush() {
        //file.flush();
        fflush(fp);
    }
    // 关闭文件
    void Close() {
        if (fp) {
            fclose(fp);
            fp = 0;
        }
    }
    ~CNtFile() {
        Close();
    }
};

/**
 * POSIX异步文件系统实现
 * 使用posix::stream_descriptor进行异步IO
 */
class AsyncFileSystemPOSIX : public IAsyncFileSystem {
public:
    explicit AsyncFileSystemPOSIX(boost::asio::io_context& io_context)
        : io_context_(io_context) {}

    boost::asio::awaitable<std::vector<FileEntry>> read_directory(
        std::string_view path) override;
    
    boost::asio::awaitable<void> create_directory(
        std::string_view path) override;
    
    boost::asio::awaitable<void> remove_directory(
        std::string_view path) override;

    boost::asio::awaitable<std::uint64_t> file_size(
        std::string_view path) override;
    
    boost::asio::awaitable<bool> exists(
        std::string_view path) override;
    
    boost::asio::awaitable<void> remove_file(
        std::string_view path) override;
    
    boost::asio::awaitable<void> rename(
        std::string_view from, std::string_view to) override;

    boost::asio::awaitable<std::size_t> read_file(
        std::string_view path,
        boost::asio::mutable_buffer buffer,
        std::uint64_t offset = 0) override;
    
    boost::asio::awaitable<std::size_t> write_file(
        std::string_view path,
        boost::asio::const_buffer buffer,
        std::uint64_t offset = 0) override;

private:
    boost::asio::io_context& io_context_;
};

/**
 * 带缓冲区的异步文件包装器
 * 用于大文件的分块传输
 */
class AsyncFileBuffered {
public:
    AsyncFileBuffered(const boost::asio::any_io_executor& io_context,
                      std::string_view path,
                      std::size_t buffer_size = 64 * 1024);
    
    ~AsyncFileBuffered();

    // 禁用拷贝
    AsyncFileBuffered(const AsyncFileBuffered&) = delete;
    AsyncFileBuffered& operator=(const AsyncFileBuffered&) = delete;

    // 允许移动
    AsyncFileBuffered(AsyncFileBuffered&&) noexcept;
    AsyncFileBuffered& operator=(AsyncFileBuffered&&) noexcept;

    boost::asio::awaitable<std::uint64_t> size() const { co_return  file_size_; }
    boost::asio::awaitable<std::size_t> read(boost::asio::mutable_buffer buffer);
    boost::asio::awaitable<std::size_t> write(boost::asio::const_buffer buffer);
    boost::asio::awaitable<void> seek(std::uint64_t offset);
    boost::asio::awaitable<void> close();

private:
    const boost::asio::any_io_executor& io_context_;
    std::string_view file_path_;
    std::size_t buffer_size_;
    
    //int fd_ = -1;
    std::uint64_t file_size_ = 0;
    std::uint64_t current_offset_ = 0;
    
    std::vector<char> read_buffer_;
    std::vector<char> write_buffer_;

    CNtFile  rawfile;
};

} // namespace core

#endif // ASYNC_FS_HPP
