#include "pch.h"

#include "node.h"

Node::Node() {
    /**
     * 初始化消息队列
     */
    msg_queue_ = ConcurrentQueue<Message>::create();
}

Node::~Node() {
    running_ = false;
    user_thread_.join();
}

void Node::listen_user_port() {
    /**
     * 监听用户命令
     * 如果监听失败，会执行重试操作
     */
    Ptr<TcpListener> listener;

    uint32_t retry = 1;
    while (!(listener = TcpListener::bind("127.0.0.1", config_.user_port))) {
        std::this_thread::sleep_for(milliseconds(retry));
        retry *= 2;
    };

    listen(listener);
}

void Node::listen(Ptr<TcpListener> listener) {
    /**
     * 监听端口，并将从端口获取的请求转发到 MessageQueue 中
     */

    while (running_) {
        const auto stream = listener->accept();
        if (stream == nullptr) {
            break;
        }
        const auto buf = stream->recv();
        if (!buf.empty()) {
            const auto req = Json::parse(buf);
            Message msg = {};
            msg.stream = std::move(stream);
            msg.op = req.at("op").get<std::string>();
            msg.params = req.at("params");
            msg_queue_->enqueue(msg);
        } else {
            stream->send({
                { "receiver", id_ },
            });
        }
    }
}

void Node::vote_tick() {
    Message msg = {};
    msg.op = "timeout";
    msg.params = {};
    msg_queue_->enqueue(msg);

    // reset vote timer
    vote_timer_->set(config_.timeout.rand());
}

void Node::heart_tick() {
    Message msg = {};
    msg.op = "heartbeat";
    msg.params = {};
    msg_queue_->enqueue(msg);
    /**
     * 重置心跳超时时间
     */
    heart_timer_->set(config_.heartbeat_period);
}

void Node::run(const Config &config) {
    Ptr<TcpListener> listener;
    for (auto node : config.nodes) {
        listener = TcpListener::bind("127.0.0.1", node);
        if (listener) {
            id_ = node;
            printf("bind succeed\n");
            break;
        }
    }
    if (listener == nullptr) {
        return;
    }

    config_ = config;
    running_ = true;

    /* 从磁盘恢复数据 */
    recover_from_disk();

    /* 监听端口，启动选举定时器，设置随机数种子 */
    srand(static_cast<uint32_t>(time(nullptr)) * id_);
    listen_thr_ = std::thread([=] { listen(listener); });
    vote_timer_ = Timer::create([this] { vote_tick(); }, config.timeout.rand());

    /* 进入消息循环 */
    message_loop();
}

void Node::recover_from_disk() {
    /**
     * 从文件恢复当前节点的状态
     */
    String db = "storage/";
    String path = db + std::to_string(id_) + ".json";

    String buf = read_file(path.c_str());
    if (buf.empty()) {
        return;
    }

    current_term_ = -1;
    commit_index_ = -1;
    last_applied_ = -1;

    const auto data = Json::parse(buf);
    if (data.contains("current_term")) {
        current_term_ = data["current_term"].get<uint32_t>();
    }
    if (data.contains("logs")) {
        for (auto &log : data.at("logs")) {
            logs_.push_back(Log(log));
        }
    }
    if (data.contains("commit_index")) {
        commit_index_ = data.at("commit_index").get<int32_t>();
    }
    /* 执行日志 */
    last_applied_ = -1;
    for (const auto &log : logs_) {
        apply_log(log.info);
    }
}

void Node::flush_to_disk() {
    /**
     * 将内存中的任期，日志等信息刷入磁盘
     */
    String db = "storage/";
    String path = db + std::to_string(id_) + ".json";

    Json logs = Json::array({});
    for (const auto &log : logs_) {
        logs.push_back({ { "term", log.term }, { "info", log.info } });
    }
    Json pairs = Json::array({});
    for (const auto &pair : pairs_) {
        pairs.push_back({ pair.first, pair.second });
    }
    Json obj = {
        { "current_term", current_term_ },
        { "commit_index", commit_index_ },
        { "logs", logs },
        { "pairs", pairs },
    };
    write_file(path, obj.dump());
}

void Node::message_loop() {
    typedef void (Node::*Handler)(Ptr<TcpStream>, const Json &);
    HashMap<String, Handler> handlers = {
        { "timeout", &Node::on_timeout_command },
        { "heartbeat", &Node::on_heartbeat_command },
        { "elected", &Node::on_elected_command },
        { "vote", &Node::on_vote_command },
        { "echo", &Node::on_echo_command },
        { "set", &Node::on_set_command },
        { "get", &Node::on_get_command },
        { "append", &Node::on_append_command },
        { "apply", &Node::on_apply_command },
        { "rollback", &Node::on_rollback_command },
    };
    /**
     * 消息循环
     */
    while (running_) {
        const auto msg = msg_queue_->dequeue();
        /*
        printf("%d recv op: %s, params: %s\n",
            id_,
            msg.op.c_str(),
            msg.params.dump().c_str());
         */
        /**
         * 根据 op 来调用对应的处理函数
         */
        auto it = handlers.find(msg.op);
        if (it != handlers.end()) {
            (this->*it->second)(msg.stream, msg.params);
        } else {
            printf("undefined command\n");
        }
    }
}

void Node::on_timeout_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 选举定时器超时，向集群中的各个节点发出选举请求
     * 给自己投票
     */
    current_term_++;
    voted_for_ = id_;

    /**
     * 在子线程中执行投票请求
     */
    ThreadPool::get()->execute([=] {
        /**
         * 构造选举参数
         */
        const auto args = VoteRequest::Arguments(
            current_term_, id_, last_log_index(), last_log_term());
        Json req = {
            { "op", "vote" },
            { "params", args.to_json() },
        };

        /**
         * 发起 TCP 会话，
         */
        auto granteds = ConcurrentQueue<bool>::create();
        for (const auto &node : config_.nodes) {
            if (node != id_) {
                ThreadPool::get()->execute([=] {
                    const auto buf = TcpSession::request(node, req);
                    /**
                     * 将消息投票请求的结果放入队列 granteds 中
                     */
                    if (!buf.empty()) {
                        granteds->enqueue(
                            VoteRequest::Results(Json::parse(buf)).granted);
                    } else {
                        granteds->enqueue(false);
                    }
                });
            }
        }

        /**
         * 等待其他节点投票结果
         */
        uint32_t num = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            if (granteds->dequeue()) {
                num += 1;
            }
            if (num >= (config_.nodes.size() + 1) / 2) {
                Message msg = {};
                msg.op = "elected";
                msg.params = {};
                msg_queue_->enqueue(msg);
                break;
            }
        }
    });
}

void Node::on_apply_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 在状态机上应用日志
     */
    apply_log(params);

    ThreadPool::get()->execute([=] {
        /**
         * 向用户返回操作结果
         */
        stream->send({ { "success", true } });
    });
}

void Node::on_rollback_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * append 失败，重新发送 append 命令
     */
}

void Node::on_heartbeat_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 向所有节点发送心跳请求
     */
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            ThreadPool::get()->execute([=] {
                const auto args = AppendRequest::Arguments(
                    current_term_, id_, -1, -1, {}, -1);
                const Json obj = {
                    { "op", "append" },
                    { "params", args.to_json() },
                };
                const auto buf = TcpSession::request(node, obj);
            });
        }
    }
}

void Node::on_elected_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 当选主节点，停止 vote timer
     */
    printf("%d is leader\n", id_);
    vote_timer_->set(UINT32_MAX);
    user_thread_ = std::thread([=] { listen_user_port(); });
    heart_timer_ =
        Timer::create([=] { heart_tick(); }, config_.heartbeat_period);
}

void Node::on_vote_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理来自集群中其他 candidate 节点的选举指令
     * 投票的条件：
     *  1. candidate 必须拥有全部的数据
     * 若决定投票给 candidate 后，会重置 timer
     */
    auto args = VoteRequest::Arguments(params);
    VoteRequest::Results results(current_term_, false);

    /* 只有拥有最新日志的 candidate 才有资格获得选票 */
    if (args.term > current_term_ && args.last_log_index >= last_log_index() &&
        args.last_log_term >= last_log_term()) {
        /**
         * 投票给 candidate
         * 根据请求设置 term 和 vote timer
         */
        current_term_ = args.term;
        voted_for_ = args.candidate_id;

        results.term = current_term_;
        results.granted = true;

        vote_timer_->set(config_.timeout.rand());
    }
    /**
     * 回复
     */
    ThreadPool::get()->execute([=] { stream->send(results.to_json()); });
}

void Node::on_append_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理来自 leader 的 append 指令
     */
    const auto args = AppendRequest::Arguments(params);
    AppendRequest::Results results(current_term_, false);
    if (current_term_ <= args.term) {
        /* 重置 vote timer 更新 term */
        vote_timer_->set(config_.timeout.rand());
        current_term_ = args.term;

        results.term = current_term_;
    }

    /* 追加日志 */
    int32_t current_index = last_log_index();
    if (!args.entries.empty() &&
        term_of_log(args.prev_log_index) == args.prev_log_term) {
        /**
         *  更新日志，用 args.entries 强制覆盖 args.prev_log_index 后的所有内容
         */
        if (args.prev_log_index != -1) {
            logs_.resize(args.prev_log_index + 1);
        }
        for (const auto &t : args.entries) {
            logs_.push_back(t);
        }

        /* 更新 commit_index 和 last_applied index */
        commit_index_ = last_log_index();

        /* 将日志写入磁盘 */
        flush_to_disk();

        /* 追加日志成功 */
        results.success = true;

        /* 在状态机上执行命令 */
    }

    /**
     * 返回结果
     */
    ThreadPool::get()->execute([=] { stream->send(results.to_json()); });
}

void Node::on_set_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理用户的 set 指令，首先附加日志到本地日志中，然后通知 follower
     * 附加日志，在超过半数的 follower 成功附加日志之后，leader
     * 才会将操作应用到状态机中，之后在向用户返回操作的结果
     */

    /* 附加日志到本地，随后写入磁盘 */
    int32_t prev_log_index = last_log_index();
    int32_t prev_log_term = last_log_term();
    const Log log = {
        current_term_ /* term */,
        { { "op", "set" }, { "params", params } } /* info */,
    };
    logs_.push_back(log);
    commit_index_++;

    flush_to_disk();

    ThreadPool::get()->execute([=] {
        /* 通知 follower 附加日志 */
        const auto appends = ConcurrentQueue<bool>::create();

        for (const auto &node : config_.nodes) {
            if (node != id_) {
                int32_t log_index = last_log_index();
                ThreadPool::get()->execute([=] {
                    const auto args = AppendRequest::Arguments(current_term_,
                        static_cast<int32_t>(id_),
                        prev_log_index,
                        prev_log_term,
                        { log },
                        commit_index_);
                    const Json obj = {
                        { "op", "append" },
                        { "params", args.to_json() },
                    };
                    const String buf = TcpSession::request(node, obj);
                    printf("send to %d: %s\n", node, obj.dump().c_str());
                    if (!buf.empty()) {
                        const AppendRequest::Results results(Json::parse(buf));
                        appends->enqueue(results.success);

                        /* 检测到新周期 */
                        if (results.term > current_term_) {
                            printf("%d discover new term\n", id_);
                        }
                    } else {
                        appends->enqueue(false);
                    }
                });
            }
        }

        /* 等待 follower 日志附加的结果 */
        int32_t total = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            total += appends->dequeue() ? 1 : 0;
            if (total >= (config_.nodes.size() + 1) / 2) {
                break;
            }
        }

        /* 通知主线程，将日志应用到状态机中 */
        Message msg = {};
        msg.stream = stream;
        msg.op = "apply";
        msg.params = log.info;
        msg_queue_->enqueue(msg);
    });
}

void Node::on_get_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理用户的 get 指令，查找 key 对应的 value，如果不存在，返回 null
     */

    const auto key = params.at("key").get<std::string>();
    const auto it = pairs_.find(key);
    if (it == pairs_.end()) {
        ThreadPool::get()->execute([=] { stream->send({ { key, nullptr } }); });
    } else {
        const String value = it->second;
        ThreadPool::get()->execute([=] { stream->send({ { key, value } }); });
    }
}

void Node::on_echo_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 打印本节点的相关信息
     */

    ThreadPool::get()->execute([=] {
        stream->send({
            { "id", id_ },
            { "term", current_term_ },
            { "commit_index", commit_index_ },
            { "last_applied", last_applied_ },
        });
    });
}

void Node::apply_log(const Json &info) {
    /* 在状态机上应用日志 */
    const auto &op = info.at("op").get<std::string>();
    const auto &params = info.at("params");
    /* 应用操作日志 */
    if (op == "set") {
        /* 设置 key - value */
        const auto key = params.at("key").get<std::string>();
        const auto value = params.at("value").get<std::string>();
        pairs_[key] = value;
    }
    last_applied_++;
}
