#ifndef NODE_H
#define NODE_H

#include "net.h"
#include "pch.h"
#include "utils.h"

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
 * 投票请求
 */
struct VoteRequest {
    struct Params {
        uint32_t term;
        uint32_t candidate_id;
        uint32_t last_log_index;
        uint32_t last_log_term;

        Params(uint32_t term,
            uint32_t candidate_id,
            uint32_t last_log_index,
            uint32_t last_log_term) :
          term(term),
          candidate_id(candidate_id), last_log_index(last_log_index),
          last_log_term(last_log_term) {}

        Params(const Json &obj) {
            term = obj.at("term").get<uint32_t>();
            candidate_id = obj.at("candidate_id").get<uint32_t>();
            last_log_index = obj.at("last_log_index").get<uint32_t>();
            last_log_term = obj.at("last_log_term").get<uint32_t>();
        }

        /**
         * 转成 json
         */
        Json to_json() const {
            return Json{
                { "term", term },
                { "candidate_id", candidate_id },
                { "last_log_index", last_log_index },
                { "last_log_term", last_log_term },
            };
        }
    };

    struct Results {
        uint32_t term;
        bool granted;

        Results(uint32_t term, bool granted) : term(term), granted(granted) {}

        Results(const Json &params) {
            term = params.at("term").get<uint32_t>();
            granted = params.at("granted").get<bool>();
        }

        Json to_json() const {
            return Json{
                { "term", term },
                { "granted", granted },
            };
        }
    };
};

/**
 * 追加日志请求（也用作心跳包）
 */
struct AppendRequest {
    struct Params {
        uint32_t term;
        uint32_t leader_id;
        uint32_t prev_log_index;
        uint32_t prev_log_term;
        Vector<Log> entries;
        uint32_t leader_commit;

        Params(uint32_t term,
            uint32_t leader_id,
            uint32_t prev_log_index,
            uint32_t prev_log_term,
            const Vector<Log> &entries,
            uint32_t leader_commit) :
          term(term),
          leader_id(leader_id), prev_log_index(prev_log_index),
          entries(entries), leader_commit(leader_commit) {}

        Params(const Json &obj) {
            term = obj.at("term").get<uint32_t>();
            leader_id = obj.at("leader_id").get<uint32_t>();
            prev_log_index = obj.at("prev_log_index").get<uint32_t>();
            prev_log_term = obj.at("prev_log_term").get<uint32_t>();
            if (obj.contains("entries")) {
                for (const auto &t : obj.at("entries")) {
                    Log log;
                    log.term = t.at("term").get<uint32_t>();
                    log.info = t.at("info");
                    entries.push_back(log);
                }
            }
            leader_commit = obj.at("leader_commit").get<uint32_t>();
        }

        Json to_json() const {
            Json arr;
            return Json{
                { "term", term },
                { "leader_id", leader_id },
                { "prev_log_index", prev_log_index },
                { "prev_log_term", prev_log_term },
                { "entries", arr },
                { "leader_commit", leader_commit },
            };
        }
    };

    struct Results {
        uint32_t term;
        bool success;

        Results(uint32_t term, bool success) : term(term), success(success) {}

        Results(const Json &obj) {
            term = obj.at("term").get<uint32_t>();
            success = obj.at("success").get<bool>();
        }

        Json to_json() const {
            return Json{
                { "term", term },
                { "success", success },
            };
        }
    };
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
    void recover();
    void flush();

    void message_loop();
    /**
     * 节点内部回调
     */
    void on_timeout_command(Ptr<TcpStream> stream, const Json &params);
    void on_commit_command(Ptr<TcpStream> stream, const Json &params);
    void on_rollback_command(Ptr<TcpStream> stream, const Json &params);
    void on_heartbeat_command(Ptr<TcpStream> stream, const Json &params);
    void on_elected_command(Ptr<TcpStream> stream, const Json &params);

    /**
     * 集群内部命令回调
     */
    void on_vote_command(Ptr<TcpStream> stream, const Json &params);
    void on_append_command(Ptr<TcpStream> stream, const Json &params);

    /**
     * 用户命令回调
     */
    void on_set_command(Ptr<TcpStream> stream, const Json &params);
    void on_get_command(Ptr<TcpStream> stream, const Json &params);
    void on_echo_command(Ptr<TcpStream> stream, const Json &params);

    void append(uint32_t term,
        uint32_t index,
        uint16_t node,
        const String &op,
        const Json &params,
        Ptr<ConcurrentQueue<uint32_t>> results);

    uint32_t last_log_index() const {
        return static_cast<int32_t>(logs_.size());
    }

    uint32_t last_log_term() {
        uint32_t val = 0;
        if (!logs_.empty()) {
            val = logs_.end()->term;
        }
        return val;
    }

private:
    /**
     * 定时器和监听子线程，以及消息队列
     */
    std::unique_ptr<Timer> vote_timer_;
    std::unique_ptr<Timer> heart_timer_;
    std::thread user_thread_ = {};
    std::thread listen_thr_ = {};
    Ptr<ConcurrentQueue<Message>> msg_queue_;

    /**
     * 运行状态，节点 id 和节点类型
     */
    std::atomic<bool> running_ = { true };
    Config config_ = {};
    uint16_t id_ = 0;

    /**
     * 任期和当前任期获得的选票数目
     */
    uint32_t current_term_ = 0;
    uint32_t voted_for_ = 0;
    Vector<Log> logs_ = {};

    /**
     * 日志和状态机（这里的状态机是一个 HashMap）
     */
    uint32_t commit_index_ = 0;
    uint32_t last_applied_ = 0;

    Vector<uint32_t> next_index_ = {};
    Vector<uint32_t> match_index_ = {};

    HashMap<String, String> pairs_;
};

#endif
