#ifndef ACCESS_CONTROL_HPP
#define ACCESS_CONTROL_HPP

#include <string>
#include <string_view>
#include <filesystem>
#include <unordered_set>
#include <memory>
#include <functional>
#include <variant>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>

namespace security {

// 认证结果
enum class AuthResult {
    SUCCESS,
    FAILURE,
    NO_SUCH_USER,
    WRONG_PASSWORD,
    ACCOUNT_DISABLED,
    NEED_PASSWORD
};

// 访问权限
enum class AccessPermission {
    NONE,
    READ,
    WRITE,
    EXECUTE,
    ADMIN,
    FULL
};

// 用户信息
struct UserInfo {
    std::string username;
    std::string password_hash;
    std::string home_directory;
    AccessPermission permission;
    bool enabled;
};

// 访问控制条目
struct ACE {
    std::string principal; // 用户名或组名
    bool apply_to_owner;
    bool apply_to_group;
    bool apply_to_others;
    AccessPermission permissions;
};

/**
 * 用户存储接口
 */
class IUserStore {
public:
    virtual ~IUserStore() = default;
    virtual std::optional<UserInfo> get_user(std::string_view username) = 0;
    virtual bool validate_password(const UserInfo& user, 
                                   std::string_view password) = 0;
    virtual bool user_exists(std::string_view username) = 0;
    virtual void add_user(const UserInfo& user) = 0;
};

/**
 * 文件用户存储实现
 * 从/etc/ftpd/users.conf格式文件中读取用户
 */
class FileUserStore : public IUserStore {
public:
    explicit FileUserStore(const std::filesystem::path& config_path);
    
    std::optional<UserInfo> get_user(std::string_view username) override;
    bool validate_password(const UserInfo& user, 
                           std::string_view password) override;
    bool user_exists(std::string_view username) override;
    void add_user(const UserInfo& user) override;

private:
    std::filesystem::path config_path_;
    std::unordered_map<std::string, UserInfo> users_;
    std::mutex mutex_;
    
    void load_users();
    void save_users();
};

/**
 * 访问控制器
 */
class AccessController {
public:
    AccessController(std::shared_ptr<IUserStore> user_store,
                     const std::filesystem::path& root_dir);
    
    // 认证用户
    AuthResult authenticate(std::string_view username, 
                            std::string_view password);
    
    // 检查权限
    bool check_permission(std::string_view username,
                          const std::filesystem::path& path,
                          AccessPermission required);
    
    // 获取用户信息
    std::optional<UserInfo> get_user(std::string_view username);
    
    // 获取用户主目录
    std::filesystem::path get_home_directory(std::string_view username);

private:
    std::shared_ptr<IUserStore> user_store_;
    std::filesystem::path root_directory_;
    std::mutex mutex_;
};

/**
 * 简单认证处理器
 */
class AuthHandler {
public:
    using AuthCallback = std::function<
        boost::asio::awaitable<std::pair<bool, std::string>>(
            std::string_view username,
            std::string_view password)
    >;
    
    void set_auth_callback(AuthCallback callback) {
        callback_ = std::move(callback);
    }
    
    boost::asio::awaitable<std::pair<bool, std::string>> 
    authenticate(std::string_view username, std::string_view password) {
        if (callback_) {
            co_return co_await callback_(username, password);
        }
        co_return std::make_pair(true, "OK"); // 默认允许
    }

private:
    AuthCallback callback_;
};

} // namespace security

#endif // ACCESS_CONTROL_HPP
