#ifndef FTP_TYPES_HPP
#define FTP_TYPES_HPP

#include <string>
#include <string_view>
#include <filesystem>
#include <variant>
#include <optional>
#include <chrono>
#include <cstdint>
#include <boost/asio/ip/address.hpp>

namespace ftp {

// FTP响应码分类
enum class ResponseCode {
    // 1xx: 肯定初步成功
    DataConnectionOpen=150,

    ServiceReady = 220,
    EnterPasviveMode = 227,
    FileStatusOkay = 213,
    DirectoryStatusOkay = 212,
    HelpMessage = 211,
    NameSystemType = 215,
    
    // 2xx: 肯定完成成功
    CommandOkay = 200,
    FileActionCompleted = 226,
    LoggedIn = 230,
    CWD_pub=250,
    DirectoryCreated = 257,
    FileActionPending = 350,
    
    // 3xx: 肯定中间成功
    NeedPassword = 331,
    NeedAccount = 332,
    NeedMoreInfo = 350,
    
    // 4xx: 暂时否定完成失败
    ServiceNotAvailable = 421,
    ServiceClosingControl = 421,
    DataConnectionClosed = 426,
    FileActionNotTaken = 450,
    ActionAborted = 451,
    
    // 5xx: 永久否定完成失败
    SyntaxError = 500,
    ParameterSyntaxError = 501,
    CommandNotImplemented = 502,
    BadSequence = 503,
    ParameterNotImplemented = 504,
    NotLoggedIn = 530,
    NeedAccountForStoring = 532,
    FileUnavailable = 550,
    PageTypeUnknown = 551,
    ExceededStorageAllocation = 552,
    FileNameNotAllowed = 553
};

// FTP传输模式
enum class TransferMode {
    ASCII,      // ASCII模式
    IMAGE,      // 二进制模式
    LOCAL       // 本地模式
};

// 数据连接类型
enum class DataConnectionType {
    ACTIVE,     // 主动模式 (PORT)
    PASSIVE     // 被动模式 (PASV)
};

// 用户权限
enum class UserPermission {
    NONE,
    READ,
    WRITE,
    ADMIN
};

// FTP命令类型
enum class CommandType {
    USER, PASS, QUIT, 
    PORT, PASV, 
    LIST, NLST, 
    RETR, STOR, STOU, APPE,
    DELE, RMD, MKD, PWD, CWD, CDUP,
    TYPE, MODE, STRU,
    RNFR, RNTO,
    SITE, HELP,
    NOOP, OPTS,
    // 扩展命令
    MDTM, SIZE, MLST, MLSD,
    FEAT, REST, AUTH, PBSZ, PROT
};

// FTP会话状态
struct FTPSessionState {
    std::string username;
    bool authenticated = false;
    UserPermission permission = UserPermission::NONE;
    std::filesystem::path current_directory = "/";
    std::filesystem::path root_directory;
    
    TransferMode transfer_mode = TransferMode::IMAGE;
    std::string representation_type = "I"; // 二进制
    
    DataConnectionType data_conn_type = DataConnectionType::PASSIVE;
    std::uint32_t data_conn_port = 0;
    std::string data_conn_host;
    
    std::string rename_from_path;
    
    std::chrono::system_clock::time_point login_time;
    std::size_t bytes_transferred = 0;
};

// FTP命令解析结果
struct FTPCommand {
    CommandType type;
    std::string argument;
    std::string raw;
};

// FTP文件信息（用于LIST/NLST）
struct FTPFileInfo {
    std::string name;
    std::uint64_t size;
    bool is_directory;
    std::filesystem::perms permissions;
    std::filesystem::file_time_type modified_time;
};

// 数据通道配置
struct DataChannelConfig {
    DataConnectionType type;
    boost::asio::ip::address address; // 对于PASV是监听地址
    uint16_t port;                     // 对于PASV是监听端口
    std::string host;                  // 对于PORT是目标主机
    uint16_t port_num;                 // 对于PORT是目标端口（高8位+低8位）
};

} // namespace ftp

#endif // FTP_TYPES_HPP
