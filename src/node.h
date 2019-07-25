#ifndef NODE_H
#define NODE_H

#include "pch.h"
#include "utils.h"

// 节点类型
enum class NodeType {
    Follower,
    Candidate,
    Leader,
};

// 日志
struct Entity {
    // 任期
    uint32_t term;
    // 日志索引
    uint32_t index;
};

// 节点
class Node {
public:
    Node() = default;
    ~Node();
    void user();
    // 消息循环
    void message_loop();
    String handle_internal_command(const String &op, const Json &params);
    String handle_user_command(const String &op, const Json &params);
    // 选举
    void vote();
    void heart();
    // 启动
    void run(const Config &config);
    void load();
    void dump();
    void listen(int fd);

private:
    // 节点 id
    uint16_t id_ = 0;
    std::atomic<bool> running_ = { true };
    // 互斥
    std::mutex mtx_ = {};
    // 节点类型
    NodeType type_ = NodeType::Follower;
    // 任期
    uint32_t term_ = 0;
    uint32_t ticket_count_ = 0;
    // 选举定时器
    std::unique_ptr<Timer> vote_timer_;
    // 心跳定时器
    std::unique_ptr<Timer> heart_timer_;
    // 用户线程
    std::thread user_thr_ = {};
    // 监听线程
    std::thread listen_thr_ = {};
    // 集群配置
    Config config_ = {};
    // 存储的数据
    HashMap<String, String> pairs_;
    // 消息队列
    BlockQueue<Message> message_queue_;
};

#endif
