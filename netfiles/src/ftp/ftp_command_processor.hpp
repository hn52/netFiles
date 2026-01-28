#ifndef FTP_COMMAND_PROCESSOR_HPP
#define FTP_COMMAND_PROCESSOR_HPP

#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <boost/asio/io_context.hpp>
#include "ftp_types.hpp"
#include "core/async_fs.hpp"

namespace ftp {

class FTPSession;

/**
 * FTP命令处理器类型
 * 返回ResponseCode和响应消息
 */
using FTPCommandHandler = std::function<
    boost::asio::awaitable<std::pair<ResponseCode, std::string>>(
        FTPSession&, std::string_view)
>;

/**
 * FTP命令处理器注册表
 * 管理所有FTP命令及其处理函数
 */
class FTPCommandProcessor {
public:
    FTPCommandProcessor(const boost::asio::any_io_executor& io_context,
                        core::IAsyncFileSystem& fs);
    
    // 处理命令
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    process_command(FTPSession& session, std::string_view raw_command);
    
    // 获取支持的命令列表
    std::vector<std::string> get_supported_commands() const;

private:
    const boost::asio::any_io_executor& io_context_;
    core::IAsyncFileSystem& file_system_;
    
    // 命令注册表
    std::unordered_map<std::string, FTPCommandHandler> handlers_;
    
    // 注册所有命令
    void register_all_commands();
    
    // 各个命令的处理函数
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_user(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_pass(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_quit(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_port(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_pasv(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_list(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_nlst(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_retr(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_stor(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_dele(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_rnfr(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_rnto(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_cwd(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_pwd(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_mkd(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_rmd(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_type(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_size(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_mdtm(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_noop(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_feat(FTPSession& session, std::string_view arg);
    
    boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
    handle_opts(FTPSession& session, std::string_view arg);
};

/**
 * 路径解析工具
 */
class PathResolver {
public:
    static std::filesystem::path resolve(
        const std::filesystem::path& current,
        const std::filesystem::path& root,
        std::string_view path);
    
    static std::filesystem::path get_relative_path(
        const std::filesystem::path& absolute,
        const std::filesystem::path& base);
};

} // namespace ftp

#endif // FTP_COMMAND_PROCESSOR_HPP
