#ifndef FTP_SESSION_HPP
#define FTP_SESSION_HPP

#include <memory>
#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include "ftp_types.hpp"
#include "data_channel.hpp"
#include "core/async_fs.hpp"

namespace ftp {

class FTPCommandProcessor;

/**
 * FTPSession - FTP客户端会话
 * 处理单个客户端连接的控制通道通信
 */
class FTPSession : public std::enable_shared_from_this<FTPSession> {
public:
    FTPSession(boost::asio::ip::tcp::socket socket,
        const boost::asio::any_io_executor& io_context,
               std::shared_ptr<core::IAsyncFileSystem> fs,
               const std::filesystem::path& root_dir);
    
    ~FTPSession();
    
    // 禁用拷贝
    FTPSession(const FTPSession&) = delete;
    FTPSession& operator=(const FTPSession&) = delete;
    
    // 开始处理会话
    boost::asio::awaitable<void> start();
    
    // 检查会话是否应该继续
    bool is_active() const { return active_; }
    
    // 关闭会话
    void close() { active_ = false; }
    
    // 获取当前目录
    const std::filesystem::path& get_current_directory() const { 
        return state_.current_directory; 
    }
    
    // 获取根目录
    const std::filesystem::path& get_root_directory() const { 
        return state_.root_directory; 
    }
    
    // 检查是否已认证
    bool is_authenticated() const { return state_.authenticated; }
    
    // 获取用户名
    const std::string& get_username() const { return state_.username; }
    
    // 设置用户名
    void set_username(std::string_view username) { 
        state_.username = std::string(username); 
    }
    
    // 设置认证状态
    void set_authenticated(bool auth) { 
        state_.authenticated = auth; 
    }
    
    // 设置权限
    void set_permission(UserPermission perm) { 
        state_.permission = perm; 
    }
    
    // 设置重命名源路径
    void set_rename_from(const std::filesystem::path& path) { 
        state_.rename_from_path = path.string(); 
    }
    
    const std::filesystem::path& get_rename_from() const { 
        return state_.rename_from_path; 
    }
    
    // 设置当前目录
    void set_current_directory(const std::filesystem::path& path) { 
        state_.current_directory = path; 
    }
    
    // 设置数据类型
    void set_representation_type(std::string_view type) { 
        state_.representation_type = std::string(type); 
    }
    
    // 主动数据连接设置
    void set_active_data_connection(const std::string& host, uint16_t port) {
        state_.data_conn_host = host;
        state_.data_conn_port = port;
    }
    
    void set_data_connection_type(DataConnectionType type) {
        state_.data_conn_type = type;
    }
    
    // 创建被动模式数据通道
    boost::asio::awaitable<std::pair<boost::asio::ip::address, uint16_t>> 
    create_passive_channel() {
        co_return co_await data_channel_manager_->create_passive_channel();
    }
    
    // 执行LIST/NLST
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    execute_list(std::string_view path, bool detailed);
    
    // 执行RETR
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    execute_retr(std::string_view filename);
    
    // 执行STOR
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    execute_stor(std::string_view filename);

private:
    boost::asio::ip::tcp::socket control_socket_;
    const boost::asio::any_io_executor& io_context_;
    std::shared_ptr<core::IAsyncFileSystem> file_system_;
    
    std::unique_ptr<FTPCommandProcessor> command_processor_;
    std::unique_ptr<DataChannelManager> data_channel_manager_;
    std::unique_ptr<ftp::DataTransfer> data_transfer_;
    
    FTPSessionState state_;
    std::atomic<bool> active_{true};
    
    boost::asio::streambuf command_buffer_;
    
    // 发送响应
    boost::asio::awaitable<void> send_response(ResponseCode code, 
                                               std::string_view message);
    
    // 读取命令
    boost::asio::awaitable<std::string> read_command();
    
    // 处理命令
    boost::asio::awaitable<void> process_one_command();
    
    // 建立数据连接
    boost::asio::awaitable<boost::asio::ip::tcp::socket> establish_data_connection();
};

} // namespace ftp

#endif // FTP_SESSION_HPP
