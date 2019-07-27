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
    heart_timer_->set(config_.heart_period);
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

    /**
     * 从磁盘恢复数据
     */
    recover();

    /**
     * 监听端口，启动选举定时器，设置随机数种子（避免多个节点同时开始选举）
     */
    srand(static_cast<uint32_t>(time(nullptr)) * id_);
    listen_thr_ = std::thread(std::bind(&Node::listen, this, listener));
    vote_timer_ = Timer::create([this] { vote_tick(); }, config.timeout.rand());

    /**
     * 消息循环
     */
    message_loop();
}

void Node::recover() {
    /**
     * 从文件恢复当前节点的状态
     */
    String db = "storage/";
    String path = db + std::to_string(id_) + ".json";

    String buf = read_file(path.c_str());
    if (buf.empty()) {
        return;
    }

    current_term_ = 0;

    const auto data = Json::parse(buf);
    if (data.contains("current_term")) {
        current_term_ = data["current_term"].get<uint32_t>();
    }
    if (data.contains("logs")) {
        for (auto &log : data.at("logs")) {
            logs_.push_back({
                log.at("term").get<uint32_t>(),
                log.at("info"),
            });
        }
    }
}

void Node::flush() {
    /**
     * 将内存中的任期，日志等信息刷入磁盘
     */
    String db = "storage/";
    String path = db + std::to_string(id_) + ".json";

    Json logs;
    for (const auto &log : logs_) {
        logs.push_back({ { "term", log.term }, { "info", log.info } });
    }
    Json obj = {
        { "current_term", current_term_ },
        { "logs", logs },
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
        { "commit", &Node::on_commit_command },
        { "rollback", &Node::on_rollback_command },
    };
    /**
     * 消息循环
     */
    while (running_) {
        const auto msg = msg_queue_->dequeue();
        printf("%d recv op: %s, params: %s\n",
            id_,
            msg.op.c_str(),
            msg.params.dump().c_str());
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
        const auto args = VoteRequest::Params(
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

void Node::on_commit_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 在日志状态机中执行操作
     */
    const auto key = params.at("key").get<std::string>();
    const auto value = params.at("value").get<String>();
    pairs_[key] = value;
    ThreadPool::get()->execute([=] {
        /**
         * 向用户返回操作结果
         */
        stream->send({ { key, value } });
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
                const auto args =
                    AppendRequest::Params(current_term_, id_, 0, 0, {}, 0);
                const Json obj = {
                    { "op", "append" },
                    { "params", args.to_json() },
                };
                TcpSession::request(node, obj);
            });
        }
    }
}

void Node::on_elected_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 停止 vote timer
     */
    printf("%d is leader\n", id_);
    vote_timer_->set(UINT32_MAX);
    user_thread_ = std::thread([=] { listen_user_port(); });
    heart_timer_ = Timer::create([=] { heart_tick(); }, config_.heart_period);
}

void Node::on_vote_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理来自集群中其他 candidate 节点的选举指令
     * 投票的条件：
     *  1. candidate 的任期
     * 若决定投票给 candidate 后，会重置 timer
     */
    auto args = VoteRequest::Params(params);
    VoteRequest::Results results(current_term_, false);
    if (args.term > current_term_) {
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
     * 处理来自 leader 的 append 指令，并返回当前节点的 log_next_index
     * 1. 重置 vote timer
     */
    const auto args = AppendRequest::Params(params);
    VoteRequest::Results results(current_term_, false);
    if (current_term_ <= args.term) {
        vote_timer_->set(config_.timeout.rand());
        current_term_ = args.term;
    }

    int32_t current_index = last_log_index();
    if (false) {
        Log log = {};
        log.term = params.at("term").get<uint32_t>();
        log.info = params.at("info");
        logs_.push_back(log);
        /**
         * 将日志写入磁盘
         */
        flush();
    }

    /**
     * 返回结果
     */
    ThreadPool::get()->execute([=] { stream->send(results.to_json()); });
}

void Node::on_set_command(Ptr<TcpStream> stream, const Json &params) {
    /**
     * 处理用户的 set 指令，由两部分组成：
     * 1. 向集群中的 follower 节点提交日志
     * 2. 在 leader 节点中执行操作，将操作结果返给用户
     */

    /**
     * 提交日志，随后将日志写入磁盘
     */
    Log log = {};
    log.term = current_term_;
    log.info = { { "op", "set" }, { "params", params } };
    logs_.push_back(log);

    flush();

    ThreadPool::get()->execute([=] {
        /**
         * 消息队列 appends 用于接收各个节点日志复制的结果
         */
        const auto appends = ConcurrentQueue<uint32_t>::create();

        for (const auto &node : config_.nodes) {
            if (node != id_) {
                uint32_t log_index = last_log_index();
                ThreadPool::get()->execute([=] {
                    append(
                        current_term_, log_index, node, "set", params, appends);
                });
            }
        }

        uint32_t total = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            total += appends->dequeue();
            if (total >= (config_.nodes.size() + 1) / 2) {
                break;
            }
        }
        /**
         * 当大多数节点完成日志复制指令，将日志应用到 leader 的状态机中
         */
        Message msg = {};
        msg.stream = stream;
        msg.op = "commit";
        msg.params = params;
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
     * 打印 leader 的 id 和 term
     */
    ThreadPool::get()->execute([=] {
        stream->send({
            { "id", id_ },
            { "term", current_term_ },
        });
    });
}

void Node::append(uint32_t term,
    uint32_t index,
    uint16_t node,
    const String &op,
    const Json &params,
    Ptr<ConcurrentQueue<uint32_t>> results) {
    /**
     * 向集群中的其他节点发送 append 指令
     */

    const Json obj = {
        { "op", "append" },
        {
            "params",
            {
                { "term", term },
                { "index", index },
                { "info", { { "op", op }, { "params", params } } },
            },
        },
    };
    const auto buf = TcpSession::request(node, obj);
    /**
     * 处理 follower 对 append 指令的回复
     * 通过判断返回的 index 是否一致来判断 follower 执行 append 指令是否成功
     * 如果 append 成功，就将 1 放入 results，
     * 如果 append 失败，将 0 放入 results 当中，append
     * 失败后，另开一个子线程来处理失败的情形
     */
    if (!buf.empty()) {
        const auto obj = Json::parse(buf);
        if (obj.contains("index")) {
            const uint32_t follow_index = obj.at("index").get<uint32_t>();
            if (follow_index == index) {
                results->enqueue(1);
            } else {
                results->enqueue(0);
                Message msg = {};
                msg.op = "rollback";
                msg.params = { { "index", index } };
                msg_queue_->enqueue(msg);
            }
        }
    }
}
