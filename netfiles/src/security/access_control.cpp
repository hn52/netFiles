#include "access_control.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>

namespace security {

// ============ FileUserStore 实现 ============

FileUserStore::FileUserStore(const std::filesystem::path& config_path)
    : config_path_(config_path)
{
    load_users();
}

void FileUserStore::load_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!std::filesystem::exists(config_path_)) {
        return; // 使用默认配置
    }
    
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 格式: username:password_hash:home_dir:permission:enabled
        std::vector<std::string> parts = [line]() {
            std::vector<std::string> result;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, ':')) {
                result.push_back(part);
            }
            return result;
        }();
        
        if (parts.size() < 5) {
            continue;
        }
        
        UserInfo user;
        user.username = parts[0];
        user.password_hash = parts[1];
        user.home_directory = parts[2];
        
        // 解析权限
        if (parts[3] == "read") {
            user.permission = AccessPermission::READ;
        } else if (parts[3] == "write") {
            user.permission = AccessPermission::WRITE;
        } else if (parts[3] == "admin") {
            user.permission = AccessPermission::ADMIN;
        } else {
            user.permission = AccessPermission::NONE;
        }
        
        user.enabled = (parts[4] == "1" || parts[4] == "true");
        
        users_[user.username] = std::move(user);
    }
}

std::optional<UserInfo> FileUserStore::get_user(std::string_view username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(std::string(username));
    if (it != users_.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

bool FileUserStore::validate_password(const UserInfo& user, 
                                       std::string_view password) {
    // 简化实现：明文比较
    // 实际应用中应该使用bcrypt或argon2
    std::lock_guard<std::mutex> lock(mutex_);
    return user.password_hash == password;
}

bool FileUserStore::user_exists(std::string_view username) {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.find(std::string(username)) != users_.end();
}

void FileUserStore::add_user(const UserInfo& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    users_[user.username] = user;
    save_users();
}

void FileUserStore::save_users() {
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        return;
    }
    
    for (const auto& [username, user] : users_) {
        file << username << ':' 
             << user.password_hash << ':'
             << user.home_directory << ':';
        
        switch (user.permission) {
            case AccessPermission::READ: file << "read"; break;
            case AccessPermission::WRITE: file << "write"; break;
            case AccessPermission::ADMIN: file << "admin"; break;
            default: file << "none"; break;
        }
        
        file << ':' << (user.enabled ? "1" : "0") << '\n';
    }
}

// ============ AccessController 实现 ============

AccessController::AccessController(std::shared_ptr<IUserStore> user_store,
                                   const std::filesystem::path& root_dir)
    : user_store_(std::move(user_store))
    , root_directory_(root_dir)
{
}

AuthResult AccessController::authenticate(std::string_view username,
                                          std::string_view password) {
    auto user_opt = user_store_->get_user(username);
    
    if (!user_opt.has_value()) {
        return AuthResult::NO_SUCH_USER;
    }
    
    const auto& user = user_opt.value();
    
    if (!user.enabled) {
        return AuthResult::ACCOUNT_DISABLED;
    }
    
    if (user_store_->validate_password(user, password)) {
        return AuthResult::SUCCESS;
    }
    
    return AuthResult::WRONG_PASSWORD;
}

bool AccessController::check_permission(std::string_view username,
                                        const std::filesystem::path& path,
                                        AccessPermission required) {
    auto user_opt = user_store_->get_user(username);
    
    if (!user_opt.has_value()) {
        return false;
    }
    
    const auto& user = user_opt.value();
    
    // 检查路径是否在允许范围内
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    if (ec) return false;
    
    // 使用用户的主目录或全局根目录
    std::filesystem::path allowed_root;
    if (!user.home_directory.empty() && 
        std::filesystem::exists(user.home_directory)) {
        allowed_root = std::filesystem::canonical(user.home_directory);
    } else {
        allowed_root = std::filesystem::canonical(root_directory_);
    }
    
    if (canonical.string().find(allowed_root.string()) != 0) {
        return false;
    }
    
    // 检查权限级别
    if (required == AccessPermission::NONE) {
        return true;
    }
    
    if (required == AccessPermission::READ && 
        (user.permission == AccessPermission::READ || 
         user.permission == AccessPermission::WRITE || 
         user.permission == AccessPermission::ADMIN ||
         user.permission == AccessPermission::FULL)) {
        return true;
    }
    
    if (required == AccessPermission::WRITE && 
        (user.permission == AccessPermission::WRITE || 
         user.permission == AccessPermission::ADMIN ||
         user.permission == AccessPermission::FULL)) {
        return true;
    }
    
    if (required == AccessPermission::ADMIN && 
        user.permission == AccessPermission::ADMIN) {
        return true;
    }
    
    return false;
}

std::optional<UserInfo> AccessController::get_user(std::string_view username) {
    return user_store_->get_user(username);
}

std::filesystem::path AccessController::get_home_directory(
    std::string_view username) 
{
    auto user_opt = user_store_->get_user(username);
    if (user_opt.has_value() && !user_opt->home_directory.empty()) {
        return user_opt->home_directory;
    }
    return root_directory_;
}

} // namespace security
