#include "pch.h"

#include "node.h"

Node::~Node() {
    running_ = false;
    if (type_ == NodeType::Leader) {
        user_thr_.join();
    }
}

void Node::user() {
    int user_fd = -1;

    assert((user_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.user_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 尝试绑定端口，以 1 2 4 8 16 为时间间隔重试
    uint32_t retry_milliseconds = 1;
    while (bind(user_fd, (struct sockaddr *)(&addr), sizeof(addr)) != 0) {
        std::this_thread::sleep_for(milliseconds(retry_milliseconds));
        retry_milliseconds *= 2;
    }

    listen(user_fd);

    close(user_fd);
}

void Node::message_loop() {
    while (running_) {
        const auto msg = message_queue_.dequeue();
        printf("%d recv fd:%d op: %s, params: %s\n",
            id_,
            msg.fd,
            msg.op.c_str(),
            msg.params.dump().c_str());

        if (msg.op == "timeout") {
            // 开始选举
            term_++;
            ticket_count_ = 1;
            type_ = NodeType::Candidate;
            // 向所有节点发送选举命令
            for (auto &node : config_.nodes) {
                if (node != id_) {
                    Json req = {
                        { "op", "vote" },
                        { "params", { { "term", term_ } } },
                    };
                    ThreadPool::get()->execute([=] {
                        const auto buf = send_request(node, req.dump());
                        if (!buf.empty()) {
                            const auto rep = Json::parse(buf);
                            Message msg = {};
                            msg.op = "ticket";
                            msg.params = {
                                { "count", rep.at("count").get<uint32_t>() },
                                { "term", rep.at("term").get<uint32_t>() },
                            };
                            message_queue_.enqueue(msg);
                        }
                    });
                }
            }
        } else if (msg.op == "ticket") {
            if (type_ != NodeType::Candidate) {
                continue;
            }
            // 收到来自其他节点的投票
            uint32_t term = msg.params.at("term").get<uint32_t>();
            uint32_t count = msg.params.at("count").get<uint32_t>();
            if (term == term_) {
                ticket_count_ += count;
            }
            if (ticket_count_ > (config_.nodes.size() + 1) / 2) {
                // 已获得大多数节点的投票
                // 停止选举超时
                type_ = NodeType::Leader;
                printf("%d is leader\n", id_);

                // 切换成主节点，启动用户线程，开始向其他节点发送心跳
                vote_timer_->set(UINT32_MAX);
                user_thr_ = std::thread(&Node::user, this);
                heart_timer_ = Timer::create(
                    std::bind(&Node::heart, this), config_.heart_period);
            }
        } else if (msg.op == "vote") {
            if (type_ != NodeType::Follower) {
                continue;
            }
            // 收到来自其他节点的投票请求
            uint32_t term = msg.params.at("term").get<uint32_t>();
            uint32_t count = 0;
            if (term_ < term) {
                // 重设选举超时定时器
                term_ = term;
                count = 1;
                vote_timer_->set(config_.timeout.rand());
            }
            // 返回 term 和票数
            ThreadPool::get()->execute([=] {
                send(msg.fd,
                    {
                        { "term", term },
                        { "count", count },
                    });
            });
        } else if (msg.op == "heart") {
            // 收到心跳包
            uint32_t term = msg.params.at("term").get<uint32_t>();
            if (term_ <= term) {
                // 收到心跳包
                type_ = NodeType::Follower;
                term_ = term;
                vote_timer_->set(config_.timeout.rand());
            }
            // 返回 {} 对象
            ThreadPool::get()->execute([=] { send(msg.fd, Json({})); });
        } else if (msg.op == "log") {
            // 日志复制
        } else if (msg.op == "echo") {
            // 打印进程 id 和 term
            ThreadPool::get()->execute([=] {
                send(msg.fd,
                    {
                        { "id", id_ },
                        { "term", term_ },
                    });
            });
        } else if (msg.op == "set") {
            // 设置 key value
            const auto key = msg.params.at("key").get<std::string>();
            const auto val = msg.params.at("value").get<String>();
            pairs_[key] = val;
            ThreadPool::get()->execute([=] { send(msg.fd, { { key, val } }); });
        } else if (msg.op == "get") {
            const auto key = msg.params.at("key").get<std::string>();
            const auto it = pairs_.find(key);
            if (it == pairs_.end()) {
                ThreadPool::get()->execute([=] {
                    send(msg.fd, { { key, nullptr } });
                });
            } else {
                String value = it->second;
                ThreadPool::get()->execute([=] {
                    send(msg.fd, { { key, value } });
                });
            }
        } else if (msg.op == "kill") {
        } else if (msg.op == "exit") {
            // 退出线程
            return;
        } else {
        }
    }
}

void Node::vote() {
    // 发送超时消息
    Message msg = {};
    msg.op = "timeout";
    msg.params = {};
    message_queue_.enqueue(msg);

    // 重设
    vote_timer_->set(1500 + rand() % 1500);
}

void Node::heart() {
    // 必须是 leader 节点才能发送消息
    assert(type_ == NodeType::Leader);

    // 向所有的节点发送心跳包
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            ThreadPool::get()->execute([=] {
                Json obj = {
                    { "op", "heart" },
                    { "params", { { "term", term_ } } },
                };
                send_request(node, obj.dump());
            });
        }
    }
    // 设置下一次心跳时间
    heart_timer_->set(config_.heart_period);
}

void Node::run(const Config &config) {
    int fd = -1;
    assert((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);

    // 绑定端口，端口号将作为节点的 id
    for (size_t i = 0; i < config.nodes.size(); ++i) {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.nodes[i]);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (bind(fd, (struct sockaddr *)(&addr), sizeof(addr)) == 0) {
            id_ = config.nodes[i];
            break;
        }
    }
    // 绑定失败，直接返回
    if (id_ == 0) {
        return;
    }

    // 设置初始参数
    config_ = config;
    type_ = NodeType::Follower;
    running_ = true;
    // 从 db 恢复数据
    //  load();

    // 初始化监听线程，选举线程
    srand(id_);
    listen_thr_ = std::thread(std::bind(&Node::listen, this, fd));
    const auto vote_func = std::bind(&Node::vote, this);
    vote_timer_ = Timer::create(vote_func, config.timeout.rand());
    // 进入消息循环
    message_loop();

    // 保存数据
    // 关闭套接字
    close(fd);
    dump();
}

void Node::load() {
    // 计算路径
    String db = "db/";
    String path = db + std::to_string(id_) + ".json";

    // 读取文件
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
    // 计算路径
    String db = "db/";
    String path = db + std::to_string(id_) + ".json";

    Json obj = {
        { "term", term_ },
    };
    write_file(path, obj.dump());
}

void Node::listen(int fd) {
    // 监听端口
    // 并将从端口获取的数据写入消息队列
    int ret = 0;
    if ((ret = ::listen(fd, 10)) == -1) {
        char msg[1024] = {};
        sprintf(msg, "listen(fd:%d) => ret: %d", fd, ret);
        perror(msg);
    }
    // 命令处理主循环
    while (running_) {
        int conn_fd = -1;
        if ((conn_fd = accept(fd, nullptr, nullptr)) == -1) {
            char msg[1024] = {};
            sprintf(msg, "accept failed");
            perror(msg);
            // 如果套接字已关闭，或者其他情形，直接返回
            return;
        }

        const auto buf = recv_all(conn_fd);
        printf("%s\n", buf.c_str());
        if (!buf.empty()) {
            // 读取 op params，然后放入消息队列
            const auto req = Json::parse(buf);
            Message msg = {};
            msg.fd = conn_fd;
            msg.op = req.at("op").get<std::string>();
            msg.params = req.at("params");
            message_queue_.enqueue(msg);
        } else {
            // 如果请求中无任何内容，返回一个 {} 对象
            send(conn_fd, String("{}"));
        }
    }
}
