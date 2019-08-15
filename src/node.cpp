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

    /* 绑定 1024 端口，绑定操作会以 0.2*2^n 的间隔重试，n = 1, 2, 3 ... */
    /* 这样做可以避免 TIME_WAIT 而导致绑定失败 */
    uint32_t retry = 1;
    while (!(listener = TcpListener::bind("127.0.0.1", config_.user_port))) {
        std::this_thread::sleep_for(milliseconds(retry * 200));
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

            /* shutdown 关闭对此端口的绑定 */
            if (msg.op == "shutdown") {
                /* 校验发送者的 id，避免误操作 */
                if (msg.params.at("id") == id_) {
                    return;
                }
            }

            /* 将消息送入消息队列 */
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

    /* 重置选举定时器 */
    vote_timer_->set(config_.timeout.rand());
}

void Node::heart_tick() {
    Message msg = {};
    msg.op = "heartbeat";
    msg.params = {};
    msg_queue_->enqueue(msg);

    /* 重置心跳定时器 */
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

    /* 设置随机数种子，监听端口，启动选举定时器 */
    srand(static_cast<uint32_t>(time(nullptr)) + id_);
    listen_thr_ = std::thread([=] { listen(listener); });
    vote_timer_ = Timer::create([this] { vote_tick(); }, config.timeout.rand());

    /* 进入消息循环 */
    message_loop();
}

void Node::recover_from_disk() {
    /**
     * 从磁盘恢复数据
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
}

void Node::flush_to_disk() {
    /**
     * 将内存中的数据存入磁盘
     */
    String db = "storage/";
    String path = db + std::to_string(id_) + ".json";

    Json logs = Json::array({});
    for (const auto &log : logs_) {
        logs.push_back({ { "term", log.term }, { "info", log.info } });
    }
    Json pairs = {};
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
    /**
     * 消息循环
     */
    /* 注册消息处理函数 */
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
        { "refuse", &Node::on_refuse_command },
        { "accept", &Node::on_accept_command },
        { "commit", &Node::on_commit_command },
    };

    /* 消息循环 */
    while (true) {
        const auto msg = msg_queue_->dequeue();

        printf("%d recv op: %s, params: %s\n",
            id_,
            msg.op.c_str(),
            msg.params.dump().c_str());

        if (msg.op == "exit") {
            return;
        }
        /* 根据 op 来调用对应的处理函数 */
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
     * 选举定时器超时，开始新的一轮选举
     */
    current_term_++;
    voted_for_ = id_;

    /* 在子线程中向集群的其他节点发送选举请求 */
    uint32_t _last_log_index = last_log_index();
    uint32_t _last_log_term = last_log_term();
    ThreadPool::get()->execute([=] {
        /* 选举参数 */
        const auto args = VoteRequest::Arguments(
            current_term_, id_, _last_log_index, _last_log_term);
        Json req = {
            { "op", "vote" },
            { "params", args.to_json() },
        };

        auto results = ConcurrentQueue<bool>::create();
        for (const auto &node : config_.nodes) {
            if (node != id_) {
                ThreadPool::get()->execute([=] {
                    const auto buf = TcpSession::request(node, req);
                    /* 将操作结果放入 grenteds */
                    if (!buf.empty()) {
                        const VoteRequest::Results rep(Json::parse(buf));
                        results->enqueue(rep.granted);
                    } else {
                        results->enqueue(false);
                    }
                });
            }
        }

        /* 等待投票结果，若获得大多数节点的投票，将 elected 消息放入消息队列 */
        uint32_t num = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            num += results->dequeue() ? 1 : 0;
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

void Node::on_commit_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 提交日志，并将日志应用到状态机中
     */
    commit_index_++;
    if (last_applied_ < commit_index_) {
        apply_log(params);
    }

    ThreadPool::get()->execute([=] {
        /* 向用户返回操作结果 */
        stream->send({ { "success", true } });
    });
}

void Node::on_refuse_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * leader 发起的 append 请求被 follower 拒绝
     */
    const uint16_t node = params.at("node").get<uint16_t>();
    const int32_t index = params.at("index").get<int32_t>();

    /* 将 next_index 减一，然后重试 */
    next_index_[node] = index - 1;
    const int32_t log_index = next_index_[node];
    const int32_t log_term = term_of_log(log_index);
    std::vector<Log> logs;
    for (int32_t i = log_index + 1; i < commit_index_; ++i) {
        logs.push_back(logs_[i]);
    }
    const AppendRequest::Arguments args(
        current_term_, id_, log_index, log_term, logs, commit_index_);
    ThreadPool::get()->execute([=] {
        /* 再次发起请求 */
        append_request(node, args, nullptr, msg_queue_);
    });
}

void Node::on_accept_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * leader 发起的 append 请求被接受，更新 match index 和 next_index，
     * 一旦 append 请求被接受，说明下次发起 append 请求就从 match + 1 开始
     */
    const uint16_t node = params.at("node").get<uint16_t>();
    const int32_t index = params.at("index").get<int32_t>();
    match_index_[node] = index;
    next_index_[node] = index + 1;
}

void Node::on_heartbeat_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 向所有节点发送心跳请求
     */
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            /* 发送心跳 */
            /* 我们用发送 logs_[next_index[node], commit_index]
             * 之间的内容来表示心跳，一般情况下，这段数据的长度应该是 0 */
            const int32_t next_index = next_index_.at(node);
            const int32_t prev_log_index = next_index - 1;
            const int32_t prev_log_term = term_of_log(prev_log_index);
            std::vector<Log> logs;
            for (int32_t i = next_index; i <= commit_index_; ++i) {
                logs.push_back(logs_[i]);
            }
            ThreadPool::get()->execute([=] {
                const AppendRequest::Arguments args(current_term_,
                    id_,
                    prev_log_index,
                    prev_log_term,
                    logs,
                    commit_index_);
                append_request(node, args, nullptr, msg_queue_);
            });
        }
    }
}

void Node::on_elected_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 当选主节点，停止 vote timer，并在子线程中监听 user_port，开始定期发送心跳
     */
    printf("%d is leader\n", id_);
    vote_timer_->set(UINT32_MAX);

    /* 初始化 next index */
    next_index_.clear();
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            next_index_[node] = last_log_index() + 1;
        }
    }
    match_index_.clear();
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            match_index_[node] = -1;
        }
    }
    /* 执行日志，将状态机更新到最新的状态 */
    for (int32_t i = last_applied_ + 1; i <= commit_index_; ++i) {
        apply_log(logs_[i].info);
    }
    /* 等待用户输入指令 */
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
    if (current_term_ <= args.term) {
        /* 重置 vote timer 更新 term */
        vote_timer_->set(config_.timeout.rand());
        current_term_ = args.term;
    }
    AppendRequest::Results results(current_term_, false);

    /* 追加日志 */
    if (term_of_log(args.prev_log_index) == args.prev_log_term) {
        /* 复制日志 */
        logs_.resize(args.prev_log_index + 1);
        for (const auto &t : args.entries) {
            logs_.push_back(t);
        }

        /* 如果日志有变更，将日志写入磁盘 */
        flush_to_disk();

        /* 更新 commit_index */
        commit_index_ = std::min<int32_t>(args.leader_commit, last_log_index());

        /* 追加日志成功 */
        results.success = true;

        /* 将日志 logs_[last_applied + 1, commit_index) 应用到状态机 */
        if (last_applied_ < commit_index_) {
            for (int32_t i = last_applied_ + 1; i < commit_index_; ++i) {
                apply_log(logs_[i].info);
            }
        }
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

    /* 附加日志到本地，并写入磁盘 */
    int32_t prev_log_index = last_log_index();
    int32_t prev_log_term = last_log_term();
    const Log log = {
        current_term_ /* term */,
        { { "op", "set" }, { "params", params } } /* info */,
    };
    logs_.push_back(log);
    flush_to_disk();

    ThreadPool::get()->execute([=] {
        /* 通知 follower 附加日志 */
        const auto results = ConcurrentQueue<bool>::create();

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
                    append_request(node, args, results, msg_queue_);
                });
            }
        }

        /* 等待 follower 日志附加的结果 */
        int32_t total = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            total += results->dequeue() ? 1 : 0;
            if (total >= (config_.nodes.size() + 1) / 2) {
                /* 大多数节点已经成功的附加日志，在 leader 中提交日志 */
                Message msg = {};
                msg.stream = stream;
                msg.op = "commit";
                msg.params = log.info;
                msg_queue_->enqueue(msg);

                break;
            }
        }
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
    Json next_index = {};
    for (const auto &index : next_index_) {
        next_index[std::to_string(index.first)] = index.second;
    }
    Json match_index = {};
    for (const auto &index : match_index_) {
        match_index[std::to_string(index.first)] = index.second;
    }
    ThreadPool::get()->execute([=] {
        stream->send({
            { "id", id_ },
            { "term", current_term_ },
            { "commit_index", commit_index_ },
            { "last_applied", last_applied_ },
            { "next_index", next_index },
            { "match_index", match_index },
        });
    });
}

void Node::on_add_command(Ptr<TcpStream> stream, const Json &params) {}

void Node::on_remove_command(Ptr<TcpStream> stream, const Json &params) {}

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

void Node::append_request(uint32_t node,
    const AppendRequest::Arguments &args,
    Ptr<ConcurrentQueue<bool>> results,
    Ptr<ConcurrentQueue<Message>> msg_queue) {
    /* 发起 append 请求 */

    const Json obj = {
        { "op", "append" },
        { "params", args.to_json() },
    };
    const String buf = TcpSession::request(node, obj);

    if (!buf.empty()) {
        const AppendRequest::Results result(Json::parse(buf));

        /* 反馈结果 */
        if (results) {
            results->enqueue(result.success);
        }

        /* 更新 index */
        if (result.success) {
            /* append 操作成功 */
            Message msg = {};
            msg.op = "accept";
            const int32_t count = static_cast<int32_t>(args.entries.size());
            msg.params = {
                { "node", node },
                { "index", args.prev_log_index + count },
            };
            msg_queue->enqueue(msg);
        } else {
            /* append 请求被拒绝 */
            Message msg = {};
            msg.op = "refuse";
            msg.params = {
                { "node", node },
                { "index", args.prev_log_index },
            };
            msg_queue->enqueue(msg);
        }
    } else {
        /* 因网络故障导致的失败，不做处理 */
        if (results) {
            results->enqueue(false);
        }
    }
}
