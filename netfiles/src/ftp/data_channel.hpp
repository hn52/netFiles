#ifndef DATA_CHANNEL_HPP
#define DATA_CHANNEL_HPP

#include <memory>
#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/streambuf.hpp>
#include "core/async_fs.hpp"
#include "ftp/ftp_types.hpp"

namespace ftp {

/**
 * 数据通道管理器
 * 管理FTP数据连接（主动和被动模式）
 */
class DataChannelManager {
public:
    explicit DataChannelManager(const boost::asio::any_io_executor& io_context);
    ~DataChannelManager();

    // 创建被动模式数据通道
    // 返回监听地址和端口
    boost::asio::awaitable<std::pair<boost::asio::ip::address, uint16_t>> 
    create_passive_channel();

    // 创建主动模式数据通道
    // 连接到指定的目标地址
    boost::asio::awaitable<boost::asio::ip::tcp::socket> 
    create_active_channel(const boost::asio::ip::tcp::endpoint& target);

    // 接受被动模式连接
    boost::asio::awaitable<boost::asio::ip::tcp::socket> 
    accept_passive_connection(uint16_t port);

    // 关闭所有数据通道
    void close_all();

    // 获取当前被动监听端口
    uint16_t get_current_passive_port() const { return current_passive_port_; }

private:
    const boost::asio::any_io_executor& io_context_;
    
    // 被动模式 acceptor
    std::unique_ptr<boost::asio::ip::tcp::acceptor> pasv_acceptor_;
    
    // 当前被动端口
    std::atomic<uint16_t> current_passive_port_{20000};
    
    // 端口范围
    static constexpr uint16_t PASV_PORT_MIN = 20000;
    static constexpr uint16_t PASV_PORT_MAX = 30000;
    
    // 找到一个可用的端口并创建acceptor
    boost::asio::awaitable<uint16_t> find_available_port();
};

/**
 * 数据传输器
 * 负责在控制通道和数据通道之间传输数据
 */
class DataTransfer {
public:
    DataTransfer(const boost::asio::any_io_executor& io_context,
                 core::IAsyncFileSystem& fs);
    
    // 从文件发送到数据通道
    boost::asio::awaitable<std::size_t> send_file(
        const std::filesystem::path& filepath,
        boost::asio::ip::tcp::socket& data_socket,
        std::uint64_t offset = 0,
        std::size_t max_bytes = std::numeric_limits<std::size_t>::max());
    
    // 从数据通道接收文件
    boost::asio::awaitable<std::size_t> receive_file(
        const std::filesystem::path& filepath,
        boost::asio::ip::tcp::socket& data_socket,
        bool append = false,
        std::size_t max_bytes = std::numeric_limits<std::size_t>::max());

    // 在数据通道上发送字符串列表
    boost::asio::awaitable<void> send_listing(
        const std::vector<FTPFileInfo>& entries,
        boost::asio::ip::tcp::socket& data_socket);
    
    // 发送目录列表（从文件系统读取）
    boost::asio::awaitable<void> send_directory_listing(
        const std::filesystem::path& dir_path,
        boost::asio::ip::tcp::socket& data_socket,
        bool detailed = true);

private:
    const boost::asio::any_io_executor& io_context_;
    core::IAsyncFileSystem& file_system_;
    
    // 传输缓冲区大小
    static constexpr std::size_t BUFFER_SIZE = 64 * 1024; // 64KB
    
    // 生成LIST格式的条目
    std::string format_list_entry(const FTPFileInfo& info);
};

} // namespace ftp

#endif // DATA_CHANNEL_HPP
