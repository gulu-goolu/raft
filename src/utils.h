#ifndef UTILS_H
#define UTILS_H

#include "pch.h"

// 定时器
class Timer {
public:
    Timer() {
        // UINT32_MAX 表示永远阻塞
        tp_ = system_clock::now() + milliseconds(UINT32_MAX);

        thr_ = std::thread([&] {
            while (running_) {
                // 阻塞直到到达 timer 是规定的时间
                do {
                    std::unique_lock<std::mutex> lock(mtx_);
                    cv_.wait_until(lock, tp_);
                } while (running_ && system_clock::now() < tp_);
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
        callback_ = {};
        cv_.notify_one();
        thr_.join();
    }

    // 设置回调函数和回调时间
    void set(const std::function<void()> &callback, uint32_t tp) {
        std::lock_guard<std::mutex> lock(mtx_);
        // 设置新变量
        callback_ = callback;
        tp_ = system_clock::now() + milliseconds(tp);
        // 唤醒，如果新设置的 tp 小于旧的 tp，则需要唤醒重新设置
        cv_.notify_one();
    }

    // 设置回调的时间
    void set(uint32_t tp) { set(callback_, tp); }

    static std::unique_ptr<Timer> create(const std::function<void()> &callback,
        uint32_t tp) {
        return std::make_unique<Timer>(callback, tp);
    }

private:
    // 互斥量
    std::mutex mtx_ = {};
    // 超时时间和回调函数
    time_point<system_clock> tp_;
    std::function<void()> callback_ = {};
    // 用以执行回调的线程
    std::thread thr_ = {};
    // 工作状态
    std::atomic<bool> running_ = { true };
    // 信号量，用以唤醒回调线程
    std::condition_variable cv_ = {};
};

struct Message {
    // 消息通信描述符
    int fd = -1;
    // 消息类型
    String op;
    // 消息参数
    Json params;
};

// 消息队列
template<typename _Ty>
class BlockQueue {
public:
    // 消息入队列
    void enqueue(const _Ty &message) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            messages_.push(message);
        }
        // 唤醒 dequeue 线程
        dequeue_cv_.notify_one();
    }

    // 从消息队列中获取一个消息，如果消息队列中不存在消息，将会阻塞知道有可用消息
    _Ty dequeue() {
        std::unique_lock<std::mutex> lock(mtx_);

        if (messages_.empty()) {
            dequeue_cv_.wait(lock, [&] { return !messages_.empty(); });
        }

        const auto message = messages_.front();
        messages_.pop();
        return message;
    }

private:
    // 互斥量，用以避免同时访问 messages_
    std::mutex mtx_ = {};
    // 消息
    std::queue<_Ty> messages_ = {};
    // 用来阻塞 dequeue 的条件变量
    std::condition_variable dequeue_cv_ = {};
};

class ThreadPool {
public:
    // 启动线程池
    ThreadPool() {
        for (auto &thr : thrs_) {
            thr = std::thread([&] {
                while (true) {
                    const auto func = queue_.dequeue();
                    if (func) {
                        func();
                    } else {
                        break;
                    }
                }
            });
        }
    }

    // 退出线程池
    ~ThreadPool() {
        for (auto &thr : thrs_) {
            queue_.enqueue({});
        }
        for (auto &thr : thrs_) {
            thr.join();
        }
    }

    // 执行
    void execute(const std::function<void()> &func) { queue_.enqueue(func); }

    static ThreadPool *get() {
        static ThreadPool thr_pool;
        return &thr_pool;
    }

private:
    BlockQueue<std::function<void()>> queue_;
    std::array<std::thread, 5> thrs_ = {};
};

// RPC 消息定义
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


String recv(int fd);
void send(int fd, const String &buf);
void send(int fd, const Json &obj);
// 发送请求并等待响应
String send_request(uint16_t port, const String &request);
#endif
