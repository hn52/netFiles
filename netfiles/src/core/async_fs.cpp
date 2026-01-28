#include "async_fs.hpp"
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define access _access
// ... other Windows-specific code ...
#else
#include <unistd.h>
// ... POSIX-specific code ...
#endif
#include <sys/stat.h>
//#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <boost/asio/buffer.hpp>
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#ifdef _WIN32
#define pread(fd, buf, count, offset) \
    _lseeki64(fd, offset, SEEK_SET) == -1 ? -1 : _read(fd, buf, count)

#define pwrite(fd, buf, count, offset) \
    _lseeki64(fd, offset, SEEK_SET) == -1 ? -1 : _write(fd, buf, count)
#endif

namespace core {



// ============ AsyncFileSystemPOSIX 实现 ============

boost::asio::awaitable<std::vector<FileEntry>> 
AsyncFileSystemPOSIX::read_directory(std::string_view path) {
    std::vector<FileEntry> entries;

    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        std::error_code ec;
        auto status = std::filesystem::status(entry.path(), ec);
        if (!ec) {
            FileEntry fe;
            fe.path = entry.path().string();
            fe.is_directory = std::filesystem::is_directory(status);
            fe.permissions = status.permissions();

            if (!fe.is_directory) {
                fe.size = std::filesystem::file_size(entry.path(), ec);
            }
            else {
                fe.size = 0;
            }

            if (!ec) {
                fe.last_modified = std::filesystem::last_write_time(entry.path(), ec);
                entries.push_back(fe);
            }
        }
        
    }
    co_return entries;
}

boost::asio::awaitable<void> 
AsyncFileSystemPOSIX::create_directory(std::string_view path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to create directory: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> 
AsyncFileSystemPOSIX::remove_directory(std::string_view path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to remove directory: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<std::uint64_t> 
AsyncFileSystemPOSIX::file_size(std::string_view path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        co_return 0;
    }
    co_return size;
}

boost::asio::awaitable<bool> 
AsyncFileSystemPOSIX::exists(std::string_view path) {
    std::error_code ec;
    bool exist = std::filesystem::exists(path, ec);
    co_return exist && !ec;
}

boost::asio::awaitable<void> 
AsyncFileSystemPOSIX::remove_file(std::string_view path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to remove file: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<void> 
AsyncFileSystemPOSIX::rename(std::string_view from, std::string_view to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        throw std::runtime_error("Failed to rename: " + ec.message());
    }
    co_return;
}

boost::asio::awaitable<std::size_t> 
AsyncFileSystemPOSIX::read_file(std::string_view path,
                                 boost::asio::mutable_buffer buffer,
                                 std::uint64_t offset) {
    int fd = open(path.data(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Failed to open file: " + 
            std::string(std::strerror(errno)));
    }
    
    if (offset > 0) {
        off_t result = lseek(fd, static_cast<off_t>(offset), SEEK_SET);
        if (result < 0) {
            close(fd);
            throw std::runtime_error("Failed to seek: " + 
                std::string(std::strerror(errno)));
        }
    }
    
    ssize_t bytes = read(fd, buffer.data(),
                         buffer.size());
    close(fd);
    
    if (bytes < 0) {
        throw std::runtime_error("Failed to read: " + 
            std::string(std::strerror(errno)));
    }
    
    co_return static_cast<std::size_t>(bytes);
}

boost::asio::awaitable<std::size_t> 
AsyncFileSystemPOSIX::write_file(std::string_view path,
                                  boost::asio::const_buffer buffer,
                                  std::uint64_t offset) {
    int flags = O_WRONLY | O_CREAT;
    if (offset == 0) {
        flags |= O_TRUNC;
    }
    
    int fd = open(path.data(), flags, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open file for writing: " + 
            std::string(std::strerror(errno)));
    }
    
    if (offset > 0) {
        off_t result = lseek(fd, static_cast<off_t>(offset), SEEK_SET);
        if (result < 0) {
            close(fd);
            throw std::runtime_error("Failed to seek: " + 
                std::string(std::strerror(errno)));
        }
    }
    
    ssize_t bytes = write(fd, buffer.data(),
                          buffer.size());
    close(fd);
    
    if (bytes < 0) {
        throw std::runtime_error("Failed to write: " + 
            std::string(std::strerror(errno)));
    }
    
    co_return static_cast<std::size_t>(bytes);
}

// ============ AsyncFileBuffered 实现 ============

AsyncFileBuffered::AsyncFileBuffered(const boost::asio::any_io_executor& io_context,
                                     std::string_view path,
                                     std::size_t buffer_size)
    : io_context_(io_context)
    , file_path_(path)
    , buffer_size_(buffer_size)
    , read_buffer_(buffer_size)
    , write_buffer_(buffer_size)
    , rawfile(std::string(path),false)
{
    if (rawfile.IsOpen()) {
        file_size_ = rawfile.GetFileSize();
    }
    //fd_ = open(path.data(), O_RDWR);
    //if (fd_ < 0) {
    //    // 文件不存在，创建它
    //    fd_ = open(path.data(), O_RDWR | O_CREAT, 0644);
    //}
    //
    //if (fd_ < 0) {
    //    throw std::runtime_error("Failed to open file: " + 
    //        std::string(std::strerror(errno)));
    //}
    //
    //// 获取文件大小
    //struct stat st;
    //if (fstat(fd_, &st) == 0) {
    //    file_size_ = static_cast<std::uint64_t>(st.st_size);
    //}
}

AsyncFileBuffered::~AsyncFileBuffered() {
    /*if (fd_ >= 0) {
        ::close(fd_);
    }*/
}

//AsyncFileBuffered::AsyncFileBuffered(AsyncFileBuffered&& other) noexcept
//    : io_context_(other.io_context_)
//    , file_path_(other.file_path_)
//    , buffer_size_(other.buffer_size_)
//    //, fd_(other.fd_)
//    , file_size_(other.file_size_)
//    , current_offset_(other.current_offset_)
//    , read_buffer_(std::move(other.read_buffer_))
//    , write_buffer_(std::move(other.write_buffer_))
//   // , rawfile(path, false)
//{
//    other.fd_ = -1;
//}

//AsyncFileBuffered& AsyncFileBuffered::operator=(AsyncFileBuffered&& other) noexcept {
//    if (this != &other) {
//        if (fd_ >= 0) ::close(fd_);
//        
//        fd_ = other.fd_;
//        file_size_ = other.file_size_;
//        current_offset_ = other.current_offset_;
//        read_buffer_ = std::move(other.read_buffer_);
//        write_buffer_ = std::move(other.write_buffer_);
//        other.fd_ = -1;
//    }
//    return *this;
//}

boost::asio::awaitable<std::size_t> AsyncFileBuffered::read(
    boost::asio::mutable_buffer buffer) 
{
    ssize_t bytes = rawfile.Read(static_cast<off_t>(current_offset_), buffer.data(), buffer.size());
   /* pread(fd_, buffer.data(), buffer.size(),
                          static_cast<off_t>(current_offset_));*/
    if (bytes < 0) {
        throw std::runtime_error("Failed to read: " + 
            std::string(std::strerror(errno)));
    }
    
    current_offset_ += static_cast<std::uint64_t>(bytes);
    co_return static_cast<std::size_t>(bytes);
}

boost::asio::awaitable<std::size_t> AsyncFileBuffered::write(
    boost::asio::const_buffer buffer) 
{
    /*ssize_t bytes = pwrite(fd_, buffer.data(), buffer.size(), 
                           static_cast<off_t>(current_offset_));*/
    ssize_t bytes=rawfile.Write(static_cast<off_t>(current_offset_), buffer.data(), buffer.size());
    if (bytes < 0) {
        throw std::runtime_error("Failed to write: " + 
            std::string(std::strerror(errno)));
    }
    
    current_offset_ += static_cast<std::uint64_t>(bytes);
    file_size_ = std::max(file_size_, current_offset_);
    
    co_return static_cast<std::size_t>(bytes);
}

boost::asio::awaitable<void> AsyncFileBuffered::seek(std::uint64_t offset) {
    if (offset > file_size_) {
        throw std::out_of_range("Seek offset exceeds file size");
    }
    current_offset_ = offset;
    co_return;
}

boost::asio::awaitable<void> AsyncFileBuffered::close() {
    /*if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }*/
    rawfile.Close();
    co_return;
}

} // namespace core
