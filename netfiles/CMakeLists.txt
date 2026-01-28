cmake_minimum_required(VERSION 3.28)
project(FTPDBServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-D_WIN32_WINNT=0x0601)

# Boost.Asio (header-only, 但需要链接pthread)
set(Boost_DIR E:/prj/open/boost-1.87.0/install)
find_package(Boost REQUIRED PATHS E:/prj/open/boost-1.87.0/install COMPONENTS filesystem system)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

link_directories(E:/prj/open/boost-1.87.0/install/lib)

# OpenSSL for TLS (optional)
find_package(OpenSSL REQUIRED)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g3 -O0 -fsanitize=address)
endif()

add_executable(ftpd_server
    src/main.cpp
    src/core/io_context_pool.cpp
    src/core/async_fs.cpp
    src/ftp/ftp_session.cpp
    src/ftp/ftp_command_processor.cpp
    src/ftp/data_channel.cpp
    src/webdav/http_parser.cpp
    src/webdav/dav_handler.cpp
    src/webdav/dav_resource.cpp
    src/security/access_control.cpp
)

target_link_libraries(ftpd_server
    Boost::filesystem
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 安装配置
install(TARGETS ftpd_server RUNTIME DESTINATION bin)
install(FILES server.conf DESTINATION etc)
