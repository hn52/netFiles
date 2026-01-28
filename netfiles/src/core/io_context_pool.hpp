#ifndef IO_CONTEXT_POOL_HPP
#define IO_CONTEXT_POOL_HPP

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/basic_socket.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace core {

/**
 * IO上下文池 - 管理多个io_context实例用于负载均衡
 * 使用轮询策略分发请求到不同的IO线程
 */
class IOContextPool {
public:
    explicit IOContextPool(std::size_t pool_size) 
        : next_io_context_(0)
    {
        if (pool_size == 0) {
            throw std::invalid_argument("Pool size must be positive");
        }
        
        // 创建io_context和守护工作
        for (std::size_t i = 0; i < pool_size; ++i) {
            auto io_context = std::make_shared<boost::asio::io_context>();
            auto work = boost::asio::make_work_guard(*io_context);
            io_contexts_.push_back(io_context);
            works_.push_back(std::move(work));
        }
    }

    // 禁用拷贝
    IOContextPool(const IOContextPool&) = delete;
    IOContextPool& operator=(const IOContextPool&) = delete;

    // 获取下一个io_context（轮询）
    boost::asio::io_context& get_io_context() {
        auto& ctx = *io_contexts_[next_io_context_++];
        if (next_io_context_ >= io_contexts_.size()) {
            next_io_context_ = 0;
        }
        return ctx;
    }

    // 获取指定索引的io_context
    boost::asio::io_context& get_io_context(std::size_t index) {
        return *io_contexts_[index % io_contexts_.size()];
    }

    // 启动所有io_context运行
    void run() {
        std::vector<std::thread> threads;
        threads.reserve(io_contexts_.size());

        for (auto& io_context : io_contexts_) {
            threads.emplace_back([io_context]() {
                try {
                    io_context->run();
                } catch (const std::exception& e) {
                    std::cerr << "IO thread exception: " << e.what() << std::endl;
                }
            });
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    // 异步运行（不阻塞当前线程）
    void run_async() {
        for (auto& io_context : io_contexts_) {
            std::thread([io_context]() {
                io_context->run();
            }).detach();
        }
    }

    // 优雅停止
    void stop() {
        works_.clear(); // 移除工作守卫，允许io_context.run()结束
        for (auto& io_context : io_contexts_) {
            io_context->stop();
        }
    }

    std::size_t size() const { return io_contexts_.size(); }

private:
    std::vector<std::shared_ptr<boost::asio::io_context>> io_contexts_;
    std::vector<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>> works_;
    std::atomic<std::size_t> next_io_context_;
};

/**
 * 协程工具类 - 封装常用的协程操作
 */
class CoroutineUtils {
public:
    /**
     * 在指定io_context上启动协程
     */
    template<typename Handler>
    static void spawn(boost::asio::io_context& io_context, 
                      Handler&& handler,
                      boost::asio::yield_context yield = boost::asio::yield_context()) {
        boost::asio::spawn(io_context, std::forward<Handler>(handler), yield);
    }

    /**
     * 创建定时器Awaitable
     */
    static boost::asio::awaitable<void> sleep(
        boost::asio::io_context& io_context,
        std::chrono::milliseconds duration) 
    {
        boost::asio::steady_timer timer(io_context);
        timer.expires_after(duration);
        co_await timer.async_wait(boost::asio::use_awaitable);
    }

    /**
     * TCP连接Awaitable
     */
    static boost::asio::awaitable<boost::asio::ip::tcp::socket> connect(
        boost::asio::io_context& io_context,
        const boost::asio::ip::tcp::endpoint& endpoint)
    {
        boost::asio::ip::tcp::socket socket(io_context);
        co_await socket.async_connect(endpoint, boost::asio::use_awaitable);
        co_return socket;
    }

    /**
     * 接受连接Awaitable
     */
    static boost::asio::awaitable<boost::asio::ip::tcp::socket> accept(
        boost::asio::ip::tcp::acceptor& acceptor)
    {
        boost::asio::ip::tcp::socket socket(co_await acceptor.async_accept(
            boost::asio::use_awaitable));
        co_return socket;
    }
};

} // namespace core

#endif // IO_CONTEXT_POOL_HPP
