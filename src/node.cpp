#include "pch.h"

#include "node.h"

Node::~Node() {
    if (type_ == NodeType::Leader) {
        running_ = false;
        user_thr_.join();
    }
}

void Node::user() {
    int user_fd = -1;

    assert((user_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.leader);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 尝试绑定端口，会以 1 2 4 8 16 为时间间隔重试
    uint32_t retry_milliseconds = 1;
    while (bind(user_fd, (struct sockaddr *)(&addr), sizeof(addr)) != 0) {
        std::this_thread::sleep_for(milliseconds(retry_milliseconds));
        retry_milliseconds *= 2;
    }

    assert(listen(user_fd, 5) != -1);

    while (running_) {
        int fd = -1;
        assert((fd = accept(user_fd, nullptr, 0)) != -1);

        const String buf = recv_all(fd);
        String rep;
        if (!buf.empty()) {
            const Json req = Json::parse(buf);
            rep = handle_user_command(
                req.at("op").get<String>(), req.at("params"));
        }
        // 回复
        assert(send(fd, rep.data(), rep.size(), 0) == rep.size());

        // 关闭套接字，终止会话
        close(fd);
    }

    close(user_fd);
}

String Node::handle_internal_command(const String &op, const Json &params) {
    if (op == "vote") {
        const uint32_t term = params.at("term").get<uint32_t>();
        if (term_ < term && type_ == NodeType::Follower) {
            vote_timer_->set(150 + rand() % 150);
            term_ = term;
            // 写入文件
            dump();
            return Vote::response(1);
        } else {
            return Vote::response(0);
        }
    } else if (op == "heart") {
        type_ = NodeType::Follower;
        vote_timer_->set(150 + rand() % 150);
        return "{}";
    } else if (op == "log") {
        return "{}";
    } else {
        return "{}";
    }
}

String Node::handle_user_command(const String &op, const Json &params) {
    if (op == "echo") {
        // echo operation
        return Echo::response(id_);
    } else if (op == "set") {
        // set operation
        const auto key = params.at("key").get<String>();
        const auto value = params.at("value").get<String>();
        pairs_[key] = value;
        printf("set %s => %s\n", value.c_str(), key.c_str());
        return "{}";
    } else if (op == "get") {
        // get operation
        const auto key = params.at("key").get<String>();

        const auto it = pairs_.find(key);
        if (it == pairs_.end()) {
            printf("get %s\n", key.c_str());
            return Get::response(key, nullptr);
        } else {
            printf("get %s => %s\n", key.c_str(), it->second.c_str());
            return Get::response(key, &it->second);
        }
    } else {
        // undefined operation
        return "{}";
    }
}

void Node::vote() {
    printf("%d start vote\n", id_);
    // 开始新的一轮选举， 更新任期和身份
    term_++;
    type_ = NodeType::Candidate;

    // 总票数
    uint32_t ticket = 1;
    for (const auto &node : config_.nodes) {
        if (node != id_) {
            const auto rep = send_request(node, Vote::request(term_));
            if (!rep.empty()) {
                ticket += Json::parse(rep).at("ticket").get<uint32_t>();
            }
        }
    }

    // 节点成为主节点
    if (ticket > (config_.nodes.size() + 1) / 2) {
        type_ = NodeType::Leader;
        printf("%d is leader, ticket: %d\n", id_, ticket);
        // 启动心跳,设置心跳的超时为 5ms 后
        heart_timer_ =
            std::make_unique<Timer>(std::bind(&Node::heart, this), 50);
        // 禁用选举定时器
        vote_timer_->set(UINT32_MAX);

        user_thr_ = std::thread(&Node::user, this);
    } else {
        // 选举失败，重新设置选举超时时间
        vote_timer_->set(150 + rand() % 150);
    }
}

void Node::heart() {
    while (type_ == NodeType::Leader) {
        // 向所有的节点发送心跳包
        for (const auto &node : config_.nodes) {
            if (node != id_) {
                send_request(node, Heart::request(term_));
            }
        }
    }

    // 设置下一次地心跳时间
    heart_timer_->set(50);
}

void Node::run(const Config &config) {
    // 初始 IPV4 套接字
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
    // 设置选举定时器
    // 设置随机数种子
    srand(id_);
    vote_timer_ = std::make_unique<Timer>(
        std::bind(&Node::vote, this), 150 + rand() % 150);

    // 从 db 恢复数据
    load();

    // 监听端口
    assert(listen(fd, 10) != -1);
    // 命令处理主循环
    while (true) {
        // 接受连接
        int conn_fd = -1;
        assert((conn_fd = accept(fd, nullptr, nullptr)) != -1);

        auto buf = recv_all(conn_fd);
        if (!buf.empty()) {
            const Json req = Json::parse(buf);
            // printf("%d receive: %s\n", id_, request.c_str());
            // 调用函数处理请求
            String rep = handle_internal_command(
                req.at("op").get<String>(), req.at("params"));
            // 将响应写入 TCP 流中
            assert(send(conn_fd, rep.data(), rep.size(), 0) == rep.size());
        }

        // 关闭套接字
        close(conn_fd);
    }

    // 保存数据
    dump();

    // 关闭套接字
    close(fd);
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

    std::stringstream stream;
    stream << "{";
    stream << "\"term\":" << term_;
    stream << "}";
    write_file(path, stream.str());
}
