#include "ftp_command_processor.hpp"
#include "ftp_session.hpp"
#include "core/utils.hpp"
#include <regex>
#include "stringUtil.h"
using namespace StringUtil;

namespace ftp {

FTPCommandProcessor::FTPCommandProcessor(const boost::asio::any_io_executor& io_context,
                                         core::IAsyncFileSystem& fs)
    : io_context_(io_context)
    , file_system_(fs)
{
    register_all_commands();
}

void FTPCommandProcessor::register_all_commands() {
    handlers_["USER"] = [this](FTPSession& s, std::string_view a) { 
        return handle_user(s, a); };
    handlers_["PASS"] = [this](FTPSession& s, std::string_view a) { 
        return handle_pass(s, a); };
    handlers_["QUIT"] = [this](FTPSession& s, std::string_view a) { 
        return handle_quit(s, a); };
    handlers_["PORT"] = [this](FTPSession& s, std::string_view a) { 
        return handle_port(s, a); };
    handlers_["PASV"] = [this](FTPSession& s, std::string_view a) { 
        return handle_pasv(s, a); };
    handlers_["LIST"] = [this](FTPSession& s, std::string_view a) { 
        return handle_list(s, a); };
    handlers_["NLST"] = [this](FTPSession& s, std::string_view a) { 
        return handle_nlst(s, a); };
    handlers_["RETR"] = [this](FTPSession& s, std::string_view a) { 
        return handle_retr(s, a); };
    handlers_["STOR"] = [this](FTPSession& s, std::string_view a) { 
        return handle_stor(s, a); };
    handlers_["DELE"] = [this](FTPSession& s, std::string_view a) { 
        return handle_dele(s, a); };
    handlers_["RNFR"] = [this](FTPSession& s, std::string_view a) { 
        return handle_rnfr(s, a); };
    handlers_["RNTO"] = [this](FTPSession& s, std::string_view a) { 
        return handle_rnto(s, a); };
    handlers_["CWD"]  = [this](FTPSession& s, std::string_view a) { 
        return handle_cwd(s, a); };
    handlers_["PWD"]  = [this](FTPSession& s, std::string_view a) { 
        return handle_pwd(s, a); };
    handlers_["MKD"]  = [this](FTPSession& s, std::string_view a) { 
        return handle_mkd(s, a); };
    handlers_["RMD"]  = [this](FTPSession& s, std::string_view a) { 
        return handle_rmd(s, a); };
    handlers_["TYPE"] = [this](FTPSession& s, std::string_view a) { 
        return handle_type(s, a); };
    handlers_["SIZE"] = [this](FTPSession& s, std::string_view a) { 
        return handle_size(s, a); };
    handlers_["MDTM"] = [this](FTPSession& s, std::string_view a) { 
        return handle_mdtm(s, a); };
    handlers_["NOOP"] = [this](FTPSession& s, std::string_view a) { 
        return handle_noop(s, a); };
    handlers_["FEAT"] = [this](FTPSession& s, std::string_view a) { 
        return handle_feat(s, a); };
    handlers_["OPTS"] = [this](FTPSession& s, std::string_view a) { 
        return handle_opts(s, a); };
    handlers_["CDUP"] = [this](FTPSession& s, std::string_view a) { 
        return handle_cwd(s, ".."); };
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::process_command(FTPSession& session,
                                     std::string_view raw_command) 
{
    std::string localstr = StringUtil::encode<char, char8_t>(std::u8string_view((const char8_t*)raw_command.data(), raw_command.size()));
    std::cout<<"process_command: " << localstr << std::endl;
    // 去除命令中的多余空白
    std::string_view trimmed = localstr;
    while (!trimmed.empty() && 
           (trimmed.back() == '\r' || trimmed.back() == '\n' ||
            trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed = trimmed.substr(0, trimmed.size() - 1);
    }
    
    if (trimmed.empty()) {
        co_return std::make_pair(ResponseCode::SyntaxError, 
            "Empty command");
    }

    
    // 解析命令和参数
    std::vector<std::string> parts = utils::parseCmd(trimmed, ' ');
    std::string command = parts.empty() ? "" : utils::to_Upper(parts[0]);
    std::string argument = parts.size() > 1 ? parts[1] : "";
    
    auto it = handlers_.find(command);
    if (it == handlers_.end()) {
        co_return std::make_pair(ResponseCode::SyntaxError, 
            "Unknown command: " + std::string(command));
    }
    
    ResponseCode code;
    std::string message;
    
    try {
        auto result = co_await it->second(session, argument);
        code = result.first;
        message = result.second;
    } catch (const std::exception& e) {
        code = ResponseCode::FileUnavailable;
        message = std::string("Error: ") + e.what();
    }
    
    co_return std::make_pair(code, message);
}

std::vector<std::string> 
FTPCommandProcessor::get_supported_commands() const {
    std::vector<std::string> commands;
    commands.reserve(handlers_.size());
    for (const auto& [cmd, _] : handlers_) {
        commands.push_back(cmd);
    }
    return commands;
}

// ============ 各个命令处理函数 ============

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_user(FTPSession& session, std::string_view arg) {
    session.set_username(std::string(arg));
    co_return std::make_pair(ResponseCode::NeedPassword, 
        "User name OK, more password required");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_pass(FTPSession& session, std::string_view arg) {
    if (session.get_username().empty()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Login with USER first");
    }
    
    // 简化处理：任何密码都接受
    // 实际应用中应该在数据库中验证密码
    session.set_authenticated(true);
    session.set_permission(UserPermission::WRITE);
    
    co_return std::make_pair(ResponseCode::LoggedIn, 
        "Login successful");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_quit(FTPSession& session, std::string_view) {
    session.close();
    co_return std::make_pair(ResponseCode::ServiceClosingControl, 
        "Goodbye");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_port(FTPSession& session, std::string_view arg) {
    // PORT h1,h2,h3,h4,p1,p2
    std::regex pattern(R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))");
    std::smatch match;
    
    std::string arg_str(arg);
    if (!std::regex_match(arg_str, match, pattern)) {
        co_return std::make_pair(ResponseCode::ParameterSyntaxError, 
            "Invalid PORT command");
    }
    
    std::string host = match[1].str() + "." + match[2].str() + "." + 
                       match[3].str() + "." + match[4].str();
    uint16_t port = (static_cast<uint16_t>(std::stoi(match[5].str())) << 8) | 
                    static_cast<uint16_t>(std::stoi(match[6].str()));
    
    session.set_active_data_connection(host, port);
    session.set_data_connection_type(DataConnectionType::ACTIVE);
    
    co_return std::make_pair(ResponseCode::CommandOkay, "PORT command successful");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_pasv(FTPSession& session, std::string_view) {
    auto [addr, port] = co_await session.create_passive_channel();
    
    // 构造227响应: Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    std::string host = addr.to_string();
    std::vector<std::string> parts = utils::split(host, '.');
    
    std::string response = std::format(
        "Entering Passive Mode ({},{},{},{},{},{})",
        parts[0], parts[1], parts[2], parts[3],
        (port >> 8) & 0xFF, port & 0xFF);
    
    co_return std::make_pair(ResponseCode::EnterPasviveMode, response);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_list(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    co_return co_await session.execute_list(arg, true);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_nlst(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    co_return co_await session.execute_list(arg, false);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_retr(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    if (arg.empty()) {
        co_return std::make_pair(ResponseCode::ParameterSyntaxError, 
            "Missing filename");
    }
    
    co_return co_await session.execute_retr(arg);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_stor(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    if (arg.empty()) {
        co_return std::make_pair(ResponseCode::ParameterSyntaxError, 
            "Missing filename");
    }
    
    co_return co_await session.execute_stor(arg);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_dele(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }

    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid path");
    }
    
    bool is_dir = std::filesystem::is_directory(path);
    
    if (is_dir) {
        co_await file_system_.remove_directory(path.string());
    } else {
        co_await file_system_.remove_file(path.string());
    }
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "File deleted");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_rnfr(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid path");
    }
    
    session.set_rename_from(path);
    
    co_return std::make_pair(ResponseCode::FileActionPending, 
        "Ready for destination name");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_rnto(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    auto from = session.get_rename_from();
    if (from.empty()) {
        co_return std::make_pair(ResponseCode::BadSequence, 
            "RNFR required first");
    }
    
    if (arg.empty()) {
        co_return std::make_pair(ResponseCode::ParameterSyntaxError, 
            "Missing destination name");
    }
   
    auto to = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (to.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid destination path");
    }
    
    co_await file_system_.rename(from.string(), to.string());
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "Rename successful");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_cwd(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }

 
    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty() || !std::filesystem::exists(path)) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Directory not found");
    }
    
    if (!std::filesystem::is_directory(path)) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Not a directory");
    }
    
    //auto rel_path = PathResolver::get_relative_path(path, session.get_root_directory());
    session.set_current_directory(path);
    std::cout << "Current directory: " << session.get_current_directory() << std::endl;
    
    co_return std::make_pair(ResponseCode::CWD_pub, "Directory changed");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_pwd(FTPSession& session, std::string_view) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    auto path = PathResolver::get_relative_path(session.get_current_directory(), session.get_root_directory()).string();
   // auto path =   session.get_current_directory().string();
    if (path.empty()) {
        path = "/";
    }
    
    co_return std::make_pair(ResponseCode::DirectoryCreated,
        "\"" + path + "\"");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_mkd(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
   
    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid path");
    }
    
    co_await file_system_.create_directory(path.string());
    
    co_return std::make_pair(ResponseCode::DirectoryCreated, 
        "\"" + path.string() + "\" created");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_rmd(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }

    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Invalid path");
    }
    
    co_await file_system_.remove_directory(path.string());
    
    co_return std::make_pair(ResponseCode::FileActionCompleted, 
        "Directory removed");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_type(FTPSession& session, std::string_view arg) {
    std::string type_str(arg);
    if (type_str == "I" || type_str == "Binary") {
        session.set_representation_type("I");
        co_return std::make_pair(ResponseCode::CommandOkay, 
            "Type set to I");
    } else if (type_str == "A" || type_str == "ASCII") {
        session.set_representation_type("A");
        co_return std::make_pair(ResponseCode::CommandOkay, 
            "Type set to A");
    }
    
    co_return std::make_pair(ResponseCode::ParameterSyntaxError, 
        "Invalid type");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_size(FTPSession& session, std::string_view arg) {
    if (!session.is_authenticated()) {
        co_return std::make_pair(ResponseCode::NotLoggedIn, 
            "Not logged in");
    }
    
    auto path = PathResolver::resolve(
        session.get_current_directory(),
        session.get_root_directory(),
        arg);
    
    if (path.empty()) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "File not found");
    }
    
    if (std::filesystem::is_directory(path)) {
        co_return std::make_pair(ResponseCode::FileUnavailable, 
            "Is a directory");
    }
    
    auto size = co_await file_system_.file_size(path.string());
    
    co_return std::make_pair(ResponseCode::FileStatusOkay, 
        std::to_string(size));
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_mdtm(FTPSession& session, std::string_view arg) {
    // 简化实现
    co_return std::make_pair(ResponseCode::FileStatusOkay, 
        "20230401120000");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_noop(FTPSession& session, std::string_view) {
    co_return std::make_pair(ResponseCode::CommandOkay, "OK");
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_feat(FTPSession& session, std::string_view) {
    std::string features = 
        "Extensions supported:\r\n"
        " SIZE\r\n"
        " MDTM\r\n"
        " MLST modify*;size;type;\r\n"
        " MLSD\r\n"
        " TVFS\r\n"
        " UTF8\r\n"
        " EPSV\r\n"
        " EPRT\r\n"
        " 211 End";
    
    co_return std::make_pair(ResponseCode::HelpMessage, features);
}

boost::asio::awaitable<std::pair<ResponseCode, std::string>> 
FTPCommandProcessor::handle_opts(FTPSession& session, std::string_view arg) {
    std::string opts(arg);
    if (opts == "UTF8 ON") {
        co_return std::make_pair(ResponseCode::CommandOkay, 
            "OK, UTF8 enabled");
    }
    
    co_return std::make_pair(ResponseCode::CommandOkay, 
        "Options set");
}

// ============ PathResolver 实现 ============

std::filesystem::path 
PathResolver::resolve(const std::filesystem::path& current,
                      const std::filesystem::path& root,
                      std::string_view path) {
    std::filesystem::path target;
    
    /*if (path.empty() || path == ".") {
        target = current;
    } else if (path.starts_with("/")) {
        target = std::filesystem::path(path);
    } 
    else {
        target = std::filesystem::path(path);*/
   // }
        target = current / std::filesystem::path(path);
    
    
    
    // 规范化路径
    std::error_code ec;
    auto canonical = std::filesystem::canonical(target, ec);
    if (ec) {
        return "";
    }
    
    // 确保路径在root目录下
    auto root_canonical = std::filesystem::canonical(root, ec);
    if (ec) {
        return "";
    }
    
    if (canonical.string().find(root_canonical.string()) != 0) {
        return ""; // 越权访问
    }
    
    return canonical;
}

std::filesystem::path 
PathResolver::get_relative_path(const std::filesystem::path& absolute,
                                const std::filesystem::path& base) {
    auto root_canonical = std::filesystem::canonical(base);
    auto abs_canonical = std::filesystem::canonical(absolute);
    
    auto it_abs = abs_canonical.begin();
    auto it_root = root_canonical.begin();
    
    while (it_abs != abs_canonical.end() && 
           it_root != root_canonical.end() && 
           *it_abs == *it_root) {
        ++it_abs;
        ++it_root;
    }
    
    std::filesystem::path result;
    for (; it_root != root_canonical.end(); ++it_root) {
        result /= "..";
    }
    for (; it_abs != abs_canonical.end(); ++it_abs) {
        result /= *it_abs;
    }
    
    if (result.empty()) {
        result = "/";
    }
    
    return result;
}

} // namespace ftp
