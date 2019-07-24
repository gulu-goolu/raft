#include "pch.h"

#include "node.h"

Node::~Node() {
    running_ = false;
    user_thr_.join();
}

Json Node::handle_request(const Json &request) {
    const String op = request["op"].get<String>();
    if (op == "vote") {
        const uint32_t term = request.at("params").at("term").get<uint32_t>();
        // 收到一个投票请求
        if (term_ < term && type_ == NodeType::Follower) {
            // 重置超时时间
            vote_timer_->set(150 + rand() % 150);
            // 更新任期
            term_ = term;
            return Json::parse(Vote::response(1));
        } else {
            return Json::parse(Vote::response(0));
        }
    }
    if (op == "heart") {

        vote_timer_->set(150 + rand() % 150);

        // 将状态设为 follower
        type_ = NodeType::Follower;

        return Json::parse(Heart::response());
    }
    return "{}";
}

void Node::user_thread() {
    int user_fd = -1;

    assert((user_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.leader);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    assert(bind(user_fd, (struct sockaddr *)(&addr), sizeof(addr)) != -1);

    assert(listen(user_fd, 5) != -1);

    while (running_) {
        int fd = -1;
        assert((fd = accept(user_fd, nullptr, 0)) != -1);

        const String buf = read_all(fd);
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

String Node::handle_user_command(const String &op, const Json &params) {
    if (op == "echo") {
        return std::to_string(id_);
    }
    return "invalid command";
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
        printf("%d is leader\n", id_);
        // 启动心跳,设置心跳的超时为 5ms 后
        heart_timer_ =
            std::make_unique<Timer>(std::bind(&Node::heart, this), 50);
        // 禁用选举定时器
        vote_timer_->set(UINT32_MAX);

        user_thr_ = std::thread(&Node::user_thread, this);
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

        auto request = read_all(conn_fd);

        // printf("%d receive: %s\n", id_, request.c_str());
        // 调用函数处理请求
        String response = handle_request(Json::parse(request)).dump();

        // 将回复写入流中
        assert(send(conn_fd, response.data(), response.size(), 0) ==
               response.size());

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
