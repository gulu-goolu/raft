#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "pch.h"

struct Message {
    // 消息类型
    String op;
    // 消息参数
    Json params;
};

// 消息队列
class MessageQueue {
public:
    // 将消息入队列
    void enqueue(const Message& message) {
        {
            std::lock_guard<Mutex> lock(mtx_);
            messages_.push_back(message);
        }
        // 唤醒 dequeue 线程
        dequeue_cv_.notify_one();
    }
    
    // 从消息队列中获取一个消息，如果消息队列中不存在消息，将会阻塞
    Message dequeue() {
        std::unique_lock<Mutex> lock(mtx_);
        
        if (messages.empty() {
            dequeue_cv_.wait(lock, []{ return !messages_.empty() });
        }
        
        const auto message = messages_.front();
        messages_.pop_front();
        return message;
    }
    
    static MessageQueue* get() {
        static MessageQueue queue;
        return &queue;
    }
private:
    // 互斥量，用以避免同时访问 messages_
    std::mutex mtx_ = {};
    // 消息
    std::queue<Message> messages_ = {};
    // 用来阻塞 dequeue 的条件变量
    std::condition_variable dequeue_cv_ = {};
};

#endif
