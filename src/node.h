#ifndef NODE_H
#define NODE_H

#include "net.h"
#include "pch.h"
#include "utils.h"

enum class NodeType {
    Follower,
    Candidate,
    Leader,
};

struct Message {
    Ptr<TcpStream> stream;
    String op;
    Json params;
};

struct Log {
    /**
     * 任期和日志信息
     */
    uint32_t term;
    Json info;
};

/**
 * 节点
 */
class Node {
public:
    NOCOPYABLE_BODY(Node)

    Node();
    ~Node();
    void listen_user_port();
    void listen(Ptr<TcpListener> listener);
    void vote_tick();
    void heart_tick();
    void run(const Config &config);
    void load();
    void flush();

    void message_loop();
    /**
     * 节点内部回调
     */
    void on_timeout_command(Ptr<TcpStream> stream, const Json &params);
    void on_ballot_command(Ptr<TcpStream> stream, const Json &params);
    void on_commit_command(const Ptr<TcpStream> stream, const Json &params);

    /**
     * 集群内部命令回调
     */
    void on_heart_command(Ptr<TcpStream> stream, const Json &params);
    void on_vote_command(Ptr<TcpStream> stream, const Json &params);
    void on_append_command(Ptr<TcpStream> stream, const Json &params);

    /**
     * 用户命令回调
     */
    void on_set_command(Ptr<TcpStream> stream, const Json &params);
    void on_get_command(Ptr<TcpStream> stream, const Json &params);
    void on_echo_command(Ptr<TcpStream> stream, const Json &params);

    static void append(uint32_t term,
        uint32_t index,
        uint16_t node,
        const String &op,
        const Json &params,
        Ptr<BlockQueue<uint32_t>> results);

private:
    /**
     * 运行状态，节点 id 和节点类型
     */
    std::atomic<bool> running_ = { true };
    Config config_ = {};
    uint16_t id_ = 0;
    NodeType type_ = NodeType::Follower;
    /**
     * 任期和当前任期获得的选票数目
     */
    uint32_t term_ = 0;
    uint32_t ticket_count_ = 0;

    /**
     * 定时器和后台监听线程，以及后台线程和主线程通信的消息队列
     */
    std::unique_ptr<Timer> vote_timer_;
    std::unique_ptr<Timer> heart_timer_;
    std::thread user_thr_ = {};
    std::thread listen_thr_ = {};
    Ptr<BlockQueue<Message>> msg_queue_;

    /**
     * 日志索引和状态机（这是使用了一个 HashMap 来做状态机）
     */
    uint32_t commit_index_ = 0;
    HashMap<String, String> pairs_;
    HashMap<uint32_t, Log> logs_ = {};
};

#endif
