#ifndef UTILS_H
#define UTILS_H

#include "pch.h"

/**
 * 引用计数基类
 */
class RefCounted {
public:
    NOCOPYABLE_BODY(RefCounted)

    RefCounted() = default;
    virtual ~RefCounted() = default;

    void inc_ref();
    void dec_ref();

private:
    std::atomic<int> ref_count_ = { 1 };
};

/**
 * 引用计数智能指针
 */
template<typename T>
class Ptr {
public:
    Ptr() = default;
    Ptr(std::nullptr_t) : ptr_(nullptr) {}
    Ptr(const Ptr<T> &other) : ptr_(nullptr) { set(other.ptr_); }
    Ptr(Ptr<T> &&other) noexcept : ptr_(nullptr) {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    ~Ptr() { set(nullptr); }

    Ptr<T> &operator=(const Ptr<T> &other) {
        set(other.ptr_);
        return *this;
    }
    Ptr<T> &operator=(Ptr<T> &&other) noexcept {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        return *this;
    }

    T *operator->() const { return ptr_; }

    operator bool() const { return ptr_ != nullptr; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }

    void set(T *ptr) {
        if (ptr) {
            ptr->inc_ref();
        }
        if (ptr_) {
            ptr_->dec_ref();
        }
        ptr_ = ptr;
    }

    static Ptr<T> from(T *ptr) {
        Ptr<T> t;
        t.ptr_ = ptr;
        return t;
    }

    template<typename... Args>
    static Ptr<T> make(Args &&... args) {
        return Ptr<T>::from(new T(std::forward<Args>(args)...));
    }

private:
    T *ptr_ = nullptr;
};

/**
 * 定时器，由后台进程，条件变量两部分组成：
 *   后台进程：执行回调函数
 *   条件变量：超时等待，和主线程同步
 */
class Timer {
public:
    NOCOPYABLE_BODY(Timer)

    Timer() {
        // UINT32_MAX 表示一个永不可达的时间
        tp_ = system_clock::now() + milliseconds(UINT32_MAX);

        thr_ = std::thread([&] {
            while (running_) {
                do {
                    std::unique_lock<std::mutex> lock(mtx_);
                    cv_.wait_until(lock, tp_);
                } while (running_ && system_clock::now() < tp_);

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

    void set(const std::function<void()> &callback, uint32_t tp) {
        /**
         * 设置回调函数和回调时间
         * 设置完成后通过信号量唤醒回调线程以应用新的超时时间
         */

        std::lock_guard<std::mutex> lock(mtx_);
        callback_ = callback;
        tp_ = system_clock::now() + milliseconds(tp);
        cv_.notify_one();
    }

    void set(uint32_t tp) { set(callback_, tp); }

    static std::unique_ptr<Timer> create(const std::function<void()> &callback,
        uint32_t tp) {
        return std::unique_ptr<Timer>(new Timer(callback, tp));
    }

private:
    std::mutex mtx_ = {};
    std::condition_variable cv_ = {};
    time_point<system_clock> tp_;
    std::function<void()> callback_ = {};
    std::thread thr_ = {};
    std::atomic<bool> running_ = { true };
};

/**
 * 消息队列
 */
template<typename _Ty>
class BlockQueue : public RefCounted {
public:
    NOCOPYABLE_BODY(BlockQueue)

    BlockQueue() = default;
    ~BlockQueue() = default;

    void enqueue(const _Ty &message) {
        /**
         * 消息入队列
         */
        {
            std::lock_guard<std::mutex> lock(mtx_);
            messages_.push(message);
        }
        cv_.notify_one();
    }

    _Ty dequeue() {
        /**
         * 消息出队，如果队列中不存在任何消息，将会阻塞直到有消息可供处理为止
         */
        std::unique_lock<std::mutex> lock(mtx_);

        if (messages_.empty()) {
            cv_.wait(lock, [&] { return !messages_.empty(); });
        }

        const auto message = messages_.front();
        messages_.pop();
        return message;
    }

    /**
     * 创建消息队列
     */
    static Ptr<BlockQueue<_Ty>> create() {
        return Ptr<BlockQueue<_Ty>>::from(new BlockQueue<_Ty>());
    }

private:
    std::mutex mtx_ = {};
    std::queue<_Ty> messages_ = {};
    std::condition_variable cv_ = {};
};

/**
 * 线程池有两部分组成：
 *   1. 执行任务的后台线程
 *   2. 管理任务的消息队列
 */
class ThreadPool {
public:
    NOCOPYABLE_BODY(ThreadPool)

    ThreadPool() {
        for (auto &thr : threads_) {
            thr = std::thread([&] {
                /**
                 * 执行任务的线程
                 */
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

    ~ThreadPool() {
        for (auto &thr : threads_) {
            queue_.enqueue({});
        }
        for (auto &thr : threads_) {
            thr.join();
        }
    }

    void execute(const std::function<void()> &func) {
        /**
         * 将任务放入消息队列中，放入队列中任务将会被后台线程执行
         */
        queue_.enqueue(func);
    }

    static ThreadPool *get() {
        static ThreadPool thr_pool;
        return &thr_pool;
    }

private:
    BlockQueue<std::function<void()>> queue_;
    std::array<std::thread, 5> threads_ = {};
};

#endif
