#include "ftp_session.hpp"
#include "ftp_command_processor.hpp"
#include "core/utils.hpp"
#include <iostream>
#include <boost/asio.hpp>
#include "stringUtil.h"

namespace ftp {

FTPSession::FTPSession(boost::asio::ip::tcp::socket socket,
    const  boost::asio::any_io_executor& io_context,
                       std::shared_ptr<core::IAsyncFileSystem> fs,
                       const std::filesystem::path& root_dir)
    : control_socket_(std::move(socket))
    , io_context_(io_context)
    , file_system_(std::move(fs))
    , command_processor_(std::make_unique<FTPCommandProcessor>(io_context_, *file_system_))
    , data_channel_manager_(std::make_unique<DataChannelManager>(io_context_))
    , data_transfer_(std::make_unique<ftp::DataTransfer>(io_context_, *file_system_))
{
    state_.root_directory = std::filesystem::canonical(root_dir);
    state_.current_directory = state_.root_directory;
    state_.login_time = std::chrono::system_clock::now();
}

FTPSession::~FTPSession() {
    close();
}

boost::asio::awaitable<void> FTPSession::start() {
    auto self = shared_from_this();
    
    // 发送欢迎消息
    co_await send_response(ResponseCode::ServiceReady, 
        "FTP Server ready");
    
    utils::log_info("Client connected from: " + 
        control_socket_.remote_endpoint().address().to_string());
    
    // 主命令循环
    while (active_) {
        try {
            co_await process_one_command();
        } catch (const boost::system::system_error& e) {
            utils::log_error("Client error: " + std::string(e.what()));
            break;
        } catch (const std::exception& e) {
            utils::log_error("Session error: " + std::string(e.what()));
            break;
        }
    }
    
    utils::log_info("Client disconnected");
    co_return;
}

boost::asio::awaitable<void> FTPSession::send_response(ResponseCode code,
                                                        std::string_view message) {
    std::string response = std::format("{} {}\r\n", 
        static_cast<int>(code), message);
    std::cout<<"回复: "<<response<<std::endl;

    std::u8string utf8_message = StringUtil::encode<char8_t, char>(response);
    std::string sv= (char*)utf8_message.c_str();
    
    co_await boost::asio::async_write(control_socket_,
        boost::asio::buffer(sv),
        boost::asio::use_awaitable);
}

boost::asio::awaitable<std::string> FTPSession::read_command() {
    // 从流中读取一行
    boost::asio::streambuf::mutable_buffers_type buf = 
        command_buffer_.prepare(1024);
    
    std::size_t bytes = co_await control_socket_.async_read_some(buf,
        boost::asio::use_awaitable);
    
    command_buffer_.commit(bytes);
    
    // 查找换行符
    std::string_view sv((char*)command_buffer_.data().data(), (char*)command_buffer_.data().data() + command_buffer_.data().size());
    auto pos = sv.find('\n');
    
    if (pos == std::string_view::npos) {
        // 没有完整的行，继续读取
        co_return co_await read_command();
    }
    
    // 提取行
    std::string line(
        boost::asio::buffers_begin(command_buffer_.data()),
        boost::asio::buffers_begin(command_buffer_.data()) + pos + 1
    );
    
    command_buffer_.consume(pos + 1);
    
    // 去除\r\n
    if (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    
    co_return line;
}

boost::asio::awaitable<void> FTPSession::process_one_command() {
    auto line = co_await read_command();
    
    if (line.empty()) {
        co_return;
    }
    
    utils::log_info("Command: " + line);
    
    auto [code, message] = co_await command_processor_->process_command(*this, line);
    
    co_await send_response(code, message);
    
    // 如果是QUIT命令，关闭会话
    if (code == ResponseCode::ServiceClosingControl) {
        active_ = false;
    }
}

boost::asio::awaitable<boost::asio::ip::tcp::socket> 
FTPSession::establish_data_connection() {
    boost::asio::ip::tcp::socket data_socket(io_context_);
    
    if (state_.data_conn_type == DataConnectionType::PASSIVE) {
        // 被动模式：接受连接
        data_socket = co_await data_channel_manager_->accept_passive_connection(
            data_channel_manager_->get_current_passive_port());
    } else {
        // 主动模式：连接到客户端
        boost::asio::ip::tcp::endpoint target(
            boost::asio::ip::make_address(state_.data_conn_host),
            state_.data_conn_port
        );
        data_socket = co_await data_channel_manager_->create_active_channel(target);
    }
    
    co_return data_socket;
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPSession::execute_list(std::string_view path, bool detailed) {
    // 150 Data connection open
    co_await send_response(ResponseCode::DataConnectionOpen, 
        "Opening ASCII mode data connection for file list");
    
    auto data_socket = co_await establish_data_connection();
    
    // 解析路径
    auto full_path = PathResolver::resolve(
        state_.current_directory,
        state_.root_directory,
        path);
    
    if (full_path.empty() || !std::filesystem::exists(full_path)) {
        data_socket.close();
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Directory not found");
    }
    
    if (std::filesystem::is_directory(full_path)) {
        co_await data_transfer_->send_directory_listing(full_path, data_socket, detailed);
    } else {
        // 是文件，发送单个文件信息
        std::vector<FTPFileInfo> entries;
        FTPFileInfo info;
        info.name = (char*)full_path.filename().u8string().c_str();
        info.size = co_await file_system_->file_size(full_path.string());
        info.is_directory = false;
        co_await data_transfer_->send_listing(entries, data_socket);
    }
    
    data_socket.close();
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "Transfer complete");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPSession::execute_retr(std::string_view filename) {
    // 150 Data connection open
    co_await send_response(ResponseCode::DataConnectionOpen, 
        "Opening BINARY mode data connection for file transfer");
    
    auto data_socket = co_await establish_data_connection();
    
    // 解析路径
    auto full_path = PathResolver::resolve(
        state_.current_directory,
        state_.root_directory,
        filename);
    
    if (full_path.empty() || !std::filesystem::exists(full_path)) {
        data_socket.close();
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "File not found");
    }
    
    auto bytes = co_await data_transfer_->send_file(full_path, data_socket);
    
    data_socket.close();
    
    state_.bytes_transferred += bytes;
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "Transfer complete (" + std::to_string(bytes) + " bytes)");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPSession::execute_stor(std::string_view filename) {
    // 150 Data connection open
    co_await send_response(ResponseCode::DataConnectionOpen, 
        "Opening BINARY mode data connection for file upload");
    
    auto data_socket = co_await establish_data_connection();
    
    // 解析目标路径
    auto full_path = PathResolver::resolve(
        state_.current_directory,
        state_.root_directory,
        filename);
    
    if (full_path.empty()) {
        data_socket.close();
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid path");
    }
    
    auto bytes = co_await data_transfer_->receive_file(full_path, data_socket);
    
    data_socket.close();
    
    state_.bytes_transferred += bytes;
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "Transfer complete (" + std::to_string(bytes) + " bytes)");
}

} // namespace ftp
