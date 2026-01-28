#include <iostream>
#include <string>
#include <string_view>
#include <chrono>
//std::chrono::milliseconds(500);
#include <memory>
#include <iomanip>
#include <signal.h>
#ifdef _WIN32
#include <io.h>
#define access _access
// ... other Windows-specific code ...
#else
#include <unistd.h>
// ... POSIX-specific code ...
#endif

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/co_spawn.hpp>
#include "core/io_context_pool.hpp"
#include "core/async_fs.hpp"
#include "ftp/ftp_session.hpp"
#include "webdav/dav_handler.hpp"

namespace asio = boost::asio;


// 服务器配置
struct ServerConfig {
	std::string bind_address = "0.0.0.0";
	uint16_t ftp_port = 21;
	uint16_t webdav_port = 8080;
	std::string root_directory = "d:";
	std::size_t io_pool_size = 4;
	bool use_tls = false;
	std::string cert_file;
	std::string key_file;
};

// 全局停止标志
std::atomic<bool> g_stop{ false };

/**
 * 服务器主类
 */
class Server {
public:
	Server(const ServerConfig& config);

	void start();
	void stop();

private:
	ServerConfig config_;
	core::IOContextPool io_pool_;
	std::shared_ptr<core::IAsyncFileSystem> file_system_;

	std::unique_ptr<asio::ip::tcp::acceptor> ftp_acceptor_;
	std::unique_ptr<asio::ip::tcp::acceptor> webdav_acceptor_;

	boost::asio::awaitable<void> handle_ftp_connection(asio::ip::tcp::socket socket);
	boost::asio::awaitable<void> handle_webdav_connection(asio::ip::tcp::socket socket);

	boost::asio::awaitable<void> accept_loop(
		asio::ip::tcp::acceptor& acceptor,
		bool is_ftp);
	boost::asio::awaitable<void> ftpSessionStart(std::shared_ptr<ftp::FTPSession >& ftp_session);
};

Server::Server(const ServerConfig& config)
	: config_(config)
	, io_pool_(config_.io_pool_size)
	, file_system_(std::make_shared<core::AsyncFileSystemPOSIX>(
		io_pool_.get_io_context()))
{
}

void Server::start() {
	utils::log_info("Starting FTP/WebDAV Server...");
	utils::log_info("Root directory: " + config_.root_directory);
	utils::log_info("IO pool size: " + std::to_string(config_.io_pool_size));

	// 确保根目录存在
	std::error_code ec;
	std::filesystem::create_directories(config_.root_directory, ec);
	if (ec) {
		utils::log_error("Failed to create root directory: " + ec.message());
		return;
	}

	try {
		// 创建FTP acceptor
		ftp_acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
			io_pool_.get_io_context(0));

		asio::ip::tcp::endpoint ftp_endpoint(
			asio::ip::make_address(config_.bind_address),
			config_.ftp_port
		);
		ftp_acceptor_->open(ftp_endpoint.protocol());
		ftp_acceptor_->set_option(asio::socket_base::reuse_address(true));
		ftp_acceptor_->bind(ftp_endpoint);
		ftp_acceptor_->listen();

		utils::log_info("FTP server listening on " +
			config_.bind_address + ":" + std::to_string(config_.ftp_port));

		// 创建WebDAV acceptor
		webdav_acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
			io_pool_.get_io_context(1));

		asio::ip::tcp::endpoint webdav_endpoint(
			asio::ip::make_address(config_.bind_address),
			config_.webdav_port
		);
		webdav_acceptor_->open(webdav_endpoint.protocol());
		webdav_acceptor_->set_option(asio::socket_base::reuse_address(true));
		webdav_acceptor_->bind(webdav_endpoint);
		webdav_acceptor_->listen();

		utils::log_info("WebDAV server listening on " +
			config_.bind_address + ":" + std::to_string(config_.webdav_port));

	}
	catch (const std::exception& e) {
		utils::log_error("Failed to start servers: " + std::string(e.what()));
		return;
	}

	// 启动接受循环
	asio::co_spawn(ftp_acceptor_->get_executor(), accept_loop(*ftp_acceptor_, true), asio::detached);

	asio::co_spawn(webdav_acceptor_->get_executor(), accept_loop(*webdav_acceptor_, false), asio::detached);

	// 设置信号处理
	asio::signal_set signals(io_pool_.get_io_context());
	signals.add(SIGINT);
	signals.add(SIGTERM);

	signals.async_wait([&](const boost::system::error_code& ec, int sig) {
		utils::log_info("Received signal " + std::to_string(sig) + ", shutting down...");
		stop();
		});

	// 运行IO池
	io_pool_.run();
}

void Server::stop() {
	g_stop = true;
	io_pool_.stop();
}

boost::asio::awaitable<void> Server::accept_loop(
	asio::ip::tcp::acceptor& acceptor,
	bool is_ftp)
{
	while (!g_stop) {
		try {
			asio::ip::tcp::socket socket(co_await acceptor.async_accept(
				asio::use_awaitable));

			if (is_ftp) {
				//handle_ftp_connection(std::move(socket));
				asio::co_spawn(socket.get_executor(), handle_ftp_connection(std::move(socket)), asio::detached);
			}
			else {
				asio::co_spawn(socket.get_executor(), handle_webdav_connection(std::move(socket)), asio::detached);
				//handle_webdav_connection(std::move(socket));
			}
		}
		catch (const std::exception& e) {
			if (!g_stop) {
				utils::log_error(std::string(is_ftp ? "FTP" : "WebDAV") +
					" accept error: " + e.what());
			}
		}
	}
}

boost::asio::awaitable<void>  Server::ftpSessionStart(std::shared_ptr<ftp::FTPSession >& ftp_session)
{
	ftp_session->start().await_ready();
	co_return;
}

boost::asio::awaitable<void> Server::handle_ftp_connection(asio::ip::tcp::socket socket) {
	std::cout << "FTP connection from " << socket.remote_endpoint() << std::endl;

	auto session = std::make_shared<ftp::FTPSession>(
		std::move(socket),
		socket.get_executor(),
		file_system_,
		config_.root_directory
	);

	co_await session->start();
	co_return;
}

boost::asio::awaitable<void> Server::handle_webdav_connection(asio::ip::tcp::socket socket) {
	std::cout << "WebDAV connection from " << socket.remote_endpoint() << std::endl;
	auto handler = std::make_shared<webdav::DavRequestHandler>(
		std::move(socket),
		socket.get_executor(),
		file_system_,
		config_.root_directory
	);
	co_await handler->handle_request();
	co_return;
}



/**
 * 主函数
 */
int main(int argc, char* argv[]) {
	std::cout << "======================================\n";
	std::cout << "  FTP/WebDAV Server (C++23 + Asio)\n";
	std::cout << "======================================\n\n";

	// 解析命令行参数
	ServerConfig config;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];

		if (arg == "-h" || arg == "--help") {
			std::cout << "Usage: " << argv[0] << " [options]\n\n";
			std::cout << "Options:\n";
			std::cout << "  -a, --address <addr>  Bind address (default: 0.0.0.0)\n";
			std::cout << "  -p, --port <port>     FTP port (default: 21)\n";
			std::cout << "  -w, --webdav-port <port>  WebDAV port (default: 8080)\n";
			std::cout << "  -r, --root <dir>     Root directory (default: /var/ftpd/root)\n";
			std::cout << "  -t, --threads <n>    IO thread pool size (default: 4)\n";
			std::cout << "  -h, --help           Show this help\n";
			return 0;
		}
		else if ((arg == "-a" || arg == "--address") && i + 1 < argc) {
			config.bind_address = argv[++i];
		}
		else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
			config.ftp_port = static_cast<uint16_t>(std::stoul(argv[++i]));
		}
		else if ((arg == "-w" || arg == "--webdav-port") && i + 1 < argc) {
			config.webdav_port = static_cast<uint16_t>(std::stoul(argv[++i]));
		}
		else if ((arg == "-r" || arg == "--root") && i + 1 < argc) {
			config.root_directory = argv[++i];
		}
		else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
			config.io_pool_size = std::stoul(argv[++i]);
		}
	}

	// 创建并启动服务器
	Server server(config);

	std::cout << "Configuration:\n";
	std::cout << "  Bind Address: " << config.bind_address << "\n";
	std::cout << "  FTP Port: " << config.ftp_port << "\n";
	std::cout << "  WebDAV Port: " << config.webdav_port << "\n";
	std::cout << "  Root Directory: " << config.root_directory << "\n";
	std::cout << "  IO Pool Size: " << config.io_pool_size << "\n\n";

	server.start();

	return 0;
}
