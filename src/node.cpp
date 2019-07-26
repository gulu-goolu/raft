#include "pch.h"

#include "node.h"

Node::~Node() {
    running_ = false;
    if (type_ == NodeType::Leader) {
        user_thr_.join();
    }
}

void Node::listen_user_port() {
    uint32_t retry = 1;
    Ptr<TcpListener> listener;
    do {
        std::this_thread::sleep_for(milliseconds(retry));
        retry *= 2;
        listener = TcpListener::bind("127.0.0.1", config_.user_port);
    } while (listener == nullptr);

    listen(listener);
}

void Node::message_loop() {
    //
    while (running_) {
        const auto msg = message_queue_.dequeue();
        printf("%d recv op: %s, params: %s\n",
            id_,
            msg.op.c_str(),
            msg.params.dump().c_str());

        if (msg.op == "timeout") {
            on_timeout_command(msg.stream, msg.params);
        } else if (msg.op == "ballot") {
            on_ballot_command(msg.stream, msg.params);
        } else if (msg.op == "vote") {
            on_vote_command(msg.stream, msg.params);
        } else if (msg.op == "heart") {
            on_heart_command(msg.stream, msg.params);
        } else if (msg.op == "log") {
        } else if (msg.op == "echo") {
            on_echo_command(msg.stream, msg.params);
        } else if (msg.op == "set") {
            on_set_command(msg.stream, msg.params);
        } else if (msg.op == "get") {
            const auto key = msg.params.at("key").get<std::string>();
            const auto it = pairs_.find(key);
            if (it == pairs_.end()) {
                ThreadPool::get()->execute([=] {
                    msg.stream->send({ { key, nullptr } });
                });
            } else {
                const String value = it->second;
                ThreadPool::get()->execute([=] {
                    msg.stream->send({ { key, value } });
                });
            }
        } else if (msg.op == "append") {
            on_append_command(msg.stream, msg.params);
        } else if (msg.op == "exit") {
            return;
        } else {
        }
    }
}

void Node::vote_tick() {
    Message msg = {};
    msg.op = "timeout";
    msg.params = {};
    message_queue_.enqueue(msg);

    // reset vote timer
    vote_timer_->set(config_.timeout.rand());
}

void Node::heart_tick() {
    assert(type_ == NodeType::Leader);

    for (const auto &node : config_.nodes) {
        if (node != id_) {
            ThreadPool::get()->execute([=] {
                Json obj = {
                    { "op", "heart" },
                    { "params", { { "term", term_ } } },
                };
                TcpSession::request(node, obj);
            });
        }
    }
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
    type_ = NodeType::Follower;
    running_ = true;

    // load();
    system_clock::now().time_since_epoch().count();
    srand(static_cast<uint32_t>(time(nullptr)));
    listen_thr_ = std::thread(std::bind(&Node::listen, this, listener));
    const auto vote_func = std::bind(&Node::vote_tick, this);
    vote_timer_ = Timer::create(vote_func, config.timeout.rand());

    message_loop();

    dump();
}

void Node::load() {
    String db = "db/";
    String path = db + std::to_string(id_) + ".json";

    String buf = read_file(path.c_str());
    if (buf.empty()) {
        buf = "{}";
    }

    const auto data = Json::parse(buf);
    if (data.contains("term")) {
        term_ = data["term"].get<uint32_t>();
    } else {
        term_ = 0;
    }
}

void Node::dump() {
    String db = "db/";
    String path = db + std::to_string(id_) + ".json";

    Json obj = {
        { "term", term_ },
    };
    write_file(path, obj.dump());
}

void Node::listen(Ptr<TcpListener> listener) {
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
            message_queue_.enqueue(msg);
        } else {
            stream->send({
                { "receiver", id_ },
            });
        }
    }
}

void Node::on_timeout_command(Ptr<TcpStream> stream, const Json &params) {
    term_++;
    ticket_count_ = 1;
    type_ = NodeType::Candidate;
    for (auto &node : config_.nodes) {
        if (node != id_) {
            ThreadPool::get()->execute([=] {
                Json req = {
                    { "op", "vote" },
                    { "params",
                        {
                            { "term", term_ },
                            { "sender", id_ },
                        } },
                };
                const auto buf = TcpSession::request(node, req);

                if (!buf.empty()) {
                    const auto rep = Json::parse(buf);
                    Message msg = {};
                    msg.op = "ballot";
                    msg.params = {
                        { "sender", id_ },
                        { "count", rep.at("count").get<uint32_t>() },
                        { "term", rep.at("term").get<uint32_t>() },
                    };
                    message_queue_.enqueue(msg);
                }
            });
        }
    }
}

void Node::on_ballot_command(Ptr<TcpStream> stream, const Json &params) {
    if (type_ != NodeType::Candidate) {
        return;
    }
    uint32_t term = params.at("term").get<uint32_t>();
    uint32_t count = params.at("count").get<uint32_t>();
    if (term == term_) {
        ticket_count_ += count;
    }
    if (ticket_count_ > (config_.nodes.size() + 1) / 2) {
        type_ = NodeType::Leader;
        printf("%d is leader\n", id_);

        vote_timer_->set(UINT32_MAX);
        user_thr_ = std::thread(&Node::listen_user_port, this);
        heart_timer_ = Timer::create(
            std::bind(&Node::heart_tick, this), config_.heart_period);
    }
}

void Node::on_heart_command(Ptr<TcpStream> stream, const Json &params) {
    const uint32_t term = params.at("term").get<uint32_t>();
    if (term_ <= term) {
        if (type_ == NodeType::Leader) {
            // TODO
        }
        type_ = NodeType::Follower;
        term_ = term;
        vote_timer_->set(config_.timeout.rand());
    }

    ThreadPool::get()->execute([=] {
        stream->send({
            { "receiver", id_ },
        });
    });
}

void Node::on_vote_command(Ptr<TcpStream> stream, const Json &params) {
    //

    if (type_ != NodeType::Follower) {
        return;
    }
    uint32_t term = params.at("term").get<uint32_t>();
    uint32_t count = 0;
    if (term_ < term) {
        term_ = term;
        count = 1;
        vote_timer_->set(config_.timeout.rand());
    }
    ThreadPool::get()->execute([=] {
        stream->send({
            { "receiver", id_ },
            { "term", term },
            { "count", count },
        });
    });
}

void Node::on_append_command(Ptr<TcpStream> stream, const Json &params) {
    //

    ThreadPool::get()->execute([=] {
        stream->send({
            { "receiver", id_ },
            { "count", 1 },
        });
    });
}

void Node::on_set_command(Ptr<TcpStream> stream, const Json &params) {
    //

    const uint32_t index = commit_index_++;
    const auto key = params.at("key").get<std::string>();
    const auto val = params.at("value").get<String>();
    pairs_[key] = val;
    ThreadPool::get()->execute([=] {
        BlockQueue<uint32_t> results;
        for (const auto &node : config_.nodes) {
            if (node != id_) {
                ThreadPool::get()->execute([&] {
                    // avoid reference of node
                    const uint32_t _node = node;
                    append(term_, index, _node, "set", params, results);
                });
            }
        }

        uint32_t total = 1;
        for (size_t i = 1; i < config_.nodes.size(); ++i) {
            total += results.dequeue();

            if (total >= (config_.nodes.size() + 1) / 2) {
                printf("commit now\n");
            }
        }

        stream->send({
            { key, val },
        });
    });
}

void Node::on_echo_command(Ptr<TcpStream> stream, const Json &params) {
    ThreadPool::get()->execute([=] {
        stream->send({
            { "id", id_ },
            { "term", term_ },
        });
    });
}

void Node::append(uint32_t term,
    uint32_t index,
    uint16_t node,
    const String &op,
    const Json &params,
    BlockQueue<uint32_t> &results) {
    const Json obj = {
        { "op", "append" },
        {
            "params",
            {
                { "op", op },
                { "params", params },
                { "term", term },
                { "index", index },
            },
        },
    };
    const auto buf = TcpSession::request(node, obj);
    uint32_t count = 0;
    if (!buf.empty()) {
        const auto obj = Json::parse(buf);
        if (obj.contains("count")) {
            count += obj.at("count").get<uint32_t>();
        }
    }

    results.enqueue(count);
}
