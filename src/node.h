#ifndef NODE_H
#define NODE_H

#include "pch.h"

// 节点类型
enum class NodeType {
    Follower,
    Candidate,
    Leader,
};

// Node 内部包括四个线程
//   主线程
//   选举线程，负责发起选举
//   下面两个是只有 Leader 才有的线程
//       心跳线程，负责向 follower 发送心跳包
//       用户线程，监听 1024 端口，处理用户发出的命令
class Node {
public:
    Node() = default;
    ~Node();
    // 处理请求
    Json handle_request(const Json &request);
    void user_thread();
    // 处理用户命令
    String handle_user_command(const String &op, const Json &params);
    // 选举
    void vote();
    void heart();
    void run(const Config &config);
    void load();
    void dump();

private:
    std::atomic<bool> running_ = { true };
    // 互斥
    std::mutex mtx_ = {};
    // 节点类型
    NodeType type_ = NodeType::Follower;
    // 任期
    uint32_t term_ = 0;
    // 选举定时器
    std::unique_ptr<Timer> vote_timer_;
    // 心跳时钟
    std::unique_ptr<Timer> heart_timer_;
    // 用户线程
    std::thread user_thr_ = {};
    // 集群配置
    Config config_ = {};
    // 节点 id
    uint16_t id_ = 0;
    // 存储的数据
    HashMap<String, String> pairs_;
};

#endif
