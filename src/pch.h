#ifndef PCH_H
#define PCH_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../3rdparty/json/single_include/nlohmann/json.hpp"

using namespace std::chrono;

#if __cplusplus < 201403
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif

// type alias
template<typename _Ty>
using Vector = std::vector<_Ty>;
using String = std::string;
using Json = nlohmann::json;
template<typename _Kty, typename _Ty>
using HashMap = std::unordered_map<_Kty, _Ty>;

// vote operation
struct Vote {
    uint32_t term;
    static String request(uint32_t term) {
        Json obj = {
            { "op", "vote" },
            { "params", { { "term", term } } },
        };
        return obj.dump();
    }
    static String response(uint32_t count) {
        Json obj = {
            { "ticket", count },
        };
        return obj.dump();
    }
};

// heart operation
struct Heart {
    // 任期
    uint32_t term;
    static String request(uint32_t term) {
        Json obj = {
            { "op", "heart" },
            { "params", { { "term", term } } },
        };
        return obj.dump();
    }

    static String response() { return "{}"; }
};

// echo operation
struct Echo {
    static String response(uint16_t id) {
        Json obj = { { "id", id } };
        return obj.dump();
    }
};

// set operation
struct Set {
    static String response(const String &key, const String &value) {
        const Json obj = {
            { key, value },
        };
        return obj.dump();
    }
};

// get operation
struct Get {
    static String response(const String &key, const String *value) {
        if (value) {
            const Json obj = {
                { key, *value },
            };
            return obj.dump();
        } else {
            const Json obj = {
                { key, nullptr },
            };
            return obj.dump();
        }
    }
};


// 集群配置信息
struct Config {
    Vector<uint16_t> nodes;
    uint16_t leader;

    static Config from(const String &str);
    static Config load(const char *path);
};

// 定时器
class Timer {
public:
    Timer() {
        // 极大的超时时间
        tp_ = system_clock::now() + milliseconds(UINT32_MAX);

        thr_ = std::thread([&] {
            while (running_) {
                std::unique_lock<std::mutex> lock(mtx_);

                while (running_ && system_clock::now() < tp_) {
                    work_cv_.wait_until(lock, tp_);
                }
                // 执行回调函数
                if (callback_) {
                    callback_();
                }
            }
        });
    }

    Timer(const std::function<void()> &callback, uint32_t tp) : Timer() {
        set(callback, tp);
    }

    ~Timer() {
        running_ = false;
        callback_ = [] {};
        work_cv_.notify_one();
        thr_.join();
    }

    // 设置回调函数和回调时间
    void set(const std::function<void()> &callback, uint32_t tp) {
        // 设置新变量
        callback_ = callback;
        tp_ = system_clock::now() + milliseconds(tp);
        // 唤醒
        work_cv_.notify_one();
    }

    // 设置下一次回调的时间
    void set(uint32_t tp) { set(callback_, tp); }

private:
    // 超时时间
    time_point<system_clock> tp_;
    // 回调函数
    std::function<void()> callback_ = {};
    // 回调线程
    std::thread thr_ = {};
    // 工作状态
    std::atomic<bool> running_ = { true };
    // 互斥量
    std::mutex mtx_ = {};
    // 信号量
    std::condition_variable work_cv_ = {};
};

class Socket {
public:
    ~Socket() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    void send(const String &buf) {
        ssize_t len = 0;
        if ((len = ::send(fd_, buf.data(), buf.size(), 0)) != buf.size()) {
            char msg[1024] = {};
            sprintf(msg, "send: %llu, ret: %ll", buf.size(), len);
            perror(msg);
        }
    }

    // 创建套接字
    static std::unique_ptr<Socket> create(int family, int type, int protocol) {
        auto obj = std::make_unique<Socket>();
        assert((obj->fd_ = ::socket(family, type, protocol)) != -1);
        return obj;
    }

    //
    static std::unique_ptr<Socket> accept(int fd) {
        auto obj = std::make_unique<Socket>();
        if ((obj->fd_ = ::accept(fd, nullptr, nullptr)) != -1) {
            char msg[1024] = {};
            perror(msg);
        }
        return obj;
    }

private:
    int fd_;
};

// 读取文件
String read_file(const char *path);
// 写文件
void write_file(const String &path, const String &buf);
// 读取全部内容
String recv_all(int fd);
// 回复
void send_all(int fd, const String &buf);
// 发送请求
String send_request(uint16_t port, const String &request);

#endif
