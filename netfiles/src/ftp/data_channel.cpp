#include "data_channel.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include "ftp_types.hpp"
//#include <pwd.h>
//#include <grp.h>

namespace ftp {

DataChannelManager::DataChannelManager(const boost::asio::any_io_executor& io_context)
    : io_context_(io_context) 
{
}

DataChannelManager::~DataChannelManager() {
    close_all();
}

boost::asio::awaitable<std::pair<boost::asio::ip::address, uint16_t>> 
DataChannelManager::create_passive_channel() {
    uint16_t port = co_await find_available_port();
    
    boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::make_address("127.0.0.1"), 
        port
    );
    
    auto acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
        io_context_, endpoint
    );
    boost::system::error_code ec;
    // 设置端口复用选项
    boost::asio::socket_base::reuse_address option(true);
    acceptor->set_option(option);

    // 开始监听
    acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        OutputDebugStringA(ec.message().c_str());
        /*std::cerr << "监听失败: " << ec.message() << std::endl;
        return 1;*/
    }

    // 获取本地端点信息
   
    boost::asio::ip::tcp::endpoint local_ep = acceptor->local_endpoint(ec);
    if (ec) {
        /*std::cerr << "获取端点失败: " << ec.message() << std::endl;
        co_return;*/
    }

    
    pasv_acceptor_ = std::move(acceptor);
    current_passive_port_ = local_ep.port();
    
    
    co_return std::make_pair(
        local_ep.address(),
        local_ep.port()
    );
}

boost::asio::awaitable<uint16_t> 
DataChannelManager::find_available_port() {
    for (uint16_t port = PASV_PORT_MIN; port <= PASV_PORT_MAX; ++port) {
        try {
            boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::make_address("0.0.0.0"), 
                port
            );
            auto acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
                io_context_, endpoint
            );
            co_return port; // 找到可用端口
        } catch (const boost::system::system_error&) {
            continue; // 端口被占用，继续尝试
        }
    }
    co_return 0; // 没有可用端口
}

boost::asio::awaitable<boost::asio::ip::tcp::socket> 
DataChannelManager::create_active_channel(
    const boost::asio::ip::tcp::endpoint& target) 
{
    boost::asio::ip::tcp::socket socket(io_context_);
    co_await socket.async_connect(target, boost::asio::use_awaitable);
    co_return socket;
}

boost::asio::awaitable<boost::asio::ip::tcp::socket> 
DataChannelManager::accept_passive_connection(uint16_t port) {
    if (!pasv_acceptor_ || pasv_acceptor_->local_endpoint().port() != port) {
        throw std::runtime_error("No passive connection pending on port " + 
            std::to_string(port));
    }
    
    boost::asio::ip::tcp::socket socket(co_await pasv_acceptor_->async_accept(
        boost::asio::use_awaitable));
    
    // 关闭acceptor（只接受一个连接）
    pasv_acceptor_->close();
    pasv_acceptor_.reset();
    
    co_return socket;
}

void DataChannelManager::close_all() {
    if (pasv_acceptor_) {
        boost::system::error_code ec;
        pasv_acceptor_->close(ec);
        pasv_acceptor_.reset();
    }
}

// ============ DataTransfer 实现 ============

DataTransfer::DataTransfer(const boost::asio::any_io_executor& io_context,
                           core::IAsyncFileSystem& fs)
    : io_context_(io_context)
    , file_system_(fs)
{
}

boost::asio::awaitable<std::size_t> 
DataTransfer::send_file(const std::filesystem::path& filepath,
                         boost::asio::ip::tcp::socket& data_socket,
                         std::uint64_t offset,
                         std::size_t max_bytes) 
{
    std::size_t total_sent = 0;
    std::vector<char> buffer(BUFFER_SIZE);
    
    // 使用AsyncFileBuffered进行高效读写
    core::AsyncFileBuffered file(io_context_, filepath.string(), BUFFER_SIZE);
    
    if (offset > 0) {
        co_await file.seek(offset);
    }
    
    while (total_sent < max_bytes) {
        auto chunk_size = std::min(BUFFER_SIZE, max_bytes - total_sent);
        auto bytes_read = co_await file.read(
            boost::asio::buffer(buffer.data(), chunk_size));
        
        if (bytes_read == 0) {
            break; // 文件结束
        }
        
        boost::asio::const_buffer send_buffer(buffer.data(), bytes_read);
        std::size_t bytes_sent = 0;
        
        while (bytes_sent < bytes_read) {
            bytes_sent += co_await data_socket.async_send(
                boost::asio::buffer((char*)send_buffer.data() + bytes_sent, 
                                    bytes_read - bytes_sent),
                boost::asio::use_awaitable);
        }
        
        total_sent += bytes_sent;
    }
    
    co_await file.close();
    co_return total_sent;
}

boost::asio::awaitable<std::size_t> 
DataTransfer::receive_file(const std::filesystem::path& filepath,
                            boost::asio::ip::tcp::socket& data_socket,
                            bool append,
                            std::size_t max_bytes) 
{
    std::size_t total_received = 0;
    std::vector<char> buffer(BUFFER_SIZE);
    
    /*int flags = O_WRONLY | O_CREAT;
    if (!append) {
        flags |= O_TRUNC;
    }*/
    
    core::AsyncFileBuffered file(io_context_, filepath.string());
    
    if (append) {
        co_await file.seek(co_await file.size());
    }
    
    while (total_received < max_bytes) {
        auto bytes_received = co_await data_socket.async_read_some(
            boost::asio::buffer(buffer.data(), BUFFER_SIZE),
            boost::asio::use_awaitable);
        
        if (bytes_received == 0) {
            break; // 连接关闭
        }
        
        boost::asio::const_buffer receive_buffer(buffer.data(), bytes_received);
        std::size_t bytes_written = 0;
        
        // 使用write_file进行写入
        bytes_written = co_await file.write(receive_buffer);
        
        total_received += bytes_written;
    }
    
    co_await file.close();
    co_return total_received;
}

boost::asio::awaitable<void> 
DataTransfer::send_listing(const std::vector<FTPFileInfo>& entries,
                           boost::asio::ip::tcp::socket& data_socket) 
{
    std::string listing;
    
    for (const auto& entry : entries) {
        listing += format_list_entry(entry);
        listing += "\r\n";
    }
    
    boost::asio::const_buffer buffer(listing.data(), listing.size());
    co_await data_socket.async_send(buffer, boost::asio::use_awaitable);
}

boost::asio::awaitable<void> 
DataTransfer::send_directory_listing(const std::filesystem::path& dir_path,
                                     boost::asio::ip::tcp::socket& data_socket,
                                     bool detailed) 
{
    auto entries = co_await file_system_.read_directory(dir_path.string());
    
    std::vector<FTPFileInfo> file_infos;
    file_infos.reserve(entries.size());
    
    for (const auto& entry : entries) {
        FTPFileInfo info;
        info.name = (char*)entry.path.filename().u8string().c_str();
        info.size = entry.size;
        info.is_directory = entry.is_directory;
        info.permissions = entry.permissions;
        info.modified_time = entry.last_modified;
        file_infos.push_back(info);
    }
    
    if (detailed) {
        co_await send_listing(file_infos, data_socket);
    } else {
        // NLST - 只发送文件名
        std::string listing;
        for (const auto& info : file_infos) {
            listing += info.name;
            listing += "\r\n";
        }
        boost::asio::const_buffer buffer(listing.data(), listing.size());
        co_await data_socket.async_send(buffer, boost::asio::use_awaitable);
    }
}

std::string 
DataTransfer::format_list_entry(const FTPFileInfo& info) {
    // 格式: -rwxr-xr-x   1 user     group    12345 Jan 01 12:00 filename
    std::ostringstream oss;
    
    // 文件类型
    if (info.is_directory) {
        oss << 'd';
    } else {
        oss << '-';
    }
    
    // 权限
    auto perms = info.permissions;
    auto has_perm = [&](std::filesystem::perms p, std::filesystem::perms flag) {
        return (perms & flag) != std::filesystem::perms::none;
    };
    
    oss << (has_perm(perms, std::filesystem::perms::owner_read) ? 'r' : '-');
    oss << (has_perm(perms, std::filesystem::perms::owner_write) ? 'w' : '-');
    oss << (has_perm(perms, std::filesystem::perms::owner_exec) ? 'x' : '-');
    oss << (has_perm(perms, std::filesystem::perms::group_read) ? 'r' : '-');
    oss << (has_perm(perms, std::filesystem::perms::group_write) ? 'w' : '-');
    oss << (has_perm(perms, std::filesystem::perms::group_exec) ? 'x' : '-');
    oss << (has_perm(perms, std::filesystem::perms::others_read) ? 'r' : '-');
    oss << (has_perm(perms, std::filesystem::perms::others_write) ? 'w' : '-');
    oss << (has_perm(perms, std::filesystem::perms::others_exec) ? 'x' : '-');
    
    // 硬链接数（简化为1）
    oss << "    1 ";
    
    // 所有者（简化为root）
    oss << "root ";
    oss << "root ";
    
    // 文件大小
    oss << std::setw(12) << info.size << ' ';
    
    // 修改时间
    auto time = info.modified_time;
    /*auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(time);
    auto epoch = sctp.time_since_epoch();
    std::time_t last_mod = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() - 
        (std::chrono::system_clock::now() - 
         std::chrono::system_clock::from_time_t(epoch)));*/
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    time_t last_mod = std::chrono::system_clock::to_time_t(sctp);
    
    std::tm* tm_info = std::localtime(&last_mod);
    
    // 如果文件在6个月内，显示月份、日期、小时:分钟
    // 否则显示年份
    std::ostringstream date_oss;
    date_oss << std::put_time(tm_info, "%b %d %H:%M");
    if (std::chrono::system_clock::now() - sctp > std::chrono::hours(180 * 24)) {
        date_oss.str("");
        date_oss << std::put_time(tm_info, "%b %d  %Y");
    }
    
    oss << date_oss.str() << ' ';
    
    // 文件名
    oss << info.name;
    std::string str= oss.str();
    
    return str;
}

} // namespace ftp
