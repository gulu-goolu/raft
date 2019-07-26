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

struct Entity {
    uint32_t term;
    uint32_t index;
};

/**
 * 节点
 */
class Node {
public:
    Node() = default;
    ~Node();
    void listen_user_port();
    void message_loop();
    void vote_tick();
    void heart_tick();
    void run(const Config &config);
    void load();
    void dump();
    void listen(Ptr<TcpListener> listener);

    /**
     * 节点内部回调
     */
    void on_timeout_command(Ptr<TcpStream> stream, const Json &params);
    void on_ballot_command(Ptr<TcpStream> stream, const Json &params);

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
    void on_echo_command(Ptr<TcpStream> stream, const Json &params);

    static void append(uint32_t term,
        uint32_t index,
        uint16_t node,
        const String &op,
        const Json &params,
        BlockQueue<uint32_t> &results);

private:
    uint16_t id_ = 0;
    std::atomic<bool> running_ = { true };
    NodeType type_ = NodeType::Follower;
    uint32_t term_ = 0;
    uint32_t ticket_count_ = 0;
    std::unique_ptr<Timer> vote_timer_;
    std::unique_ptr<Timer> heart_timer_;
    std::thread user_thr_ = {};
    std::thread listen_thr_ = {};
    Config config_ = {};
    HashMap<String, String> pairs_;
    BlockQueue<Message> message_queue_;
    uint32_t commit_index_ = 0;
};

#endif
