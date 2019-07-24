#include "pch.h"

String read_file(const char *path) {
    FILE *fp = nullptr;
    fp = fopen(path, "r");
    if (!fp) {
        // 文件不存在，返回空字符串
        return String();
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    String buf;
    buf.resize(len);

    if (fread(&buf[0], 1, len, fp) != len) {
        throw std::logic_error("");
    }
    fclose(fp);

    return buf;
}

void write_file(const String &path, const String &buf) {
    printf("write path: %s, %s\n", path.c_str(), buf.c_str());
    FILE *fp = nullptr;
    fp = fopen(path.c_str(), "w+");
    if (!fp) {
        perror("open failed");
        throw std::logic_error("open file failed");
    }

    if (fwrite(buf.c_str(), 1, buf.size(), fp) != buf.size()) {
        perror("write failed");
        throw std::logic_error("write failed");
    }

    // 关闭文件
    fclose(fp);
}

Config Config::from(const String &str) {
    Config config = {};

    const auto obj = Json::parse(str);
    if (obj.contains("leader")) {
        config.leader = obj["leader"].get<uint16_t>();
    }
    if (obj.contains("nodes")) {
        for (const auto &node : obj["nodes"]) {
            config.nodes.push_back(node.get<uint16_t>());
        }
    }

    return config;
}

String read_all(int fd) {
    // 读数据
    String request;
    do {
        char buf[4096];
        int len = -1; // 接受数据长度
        if ((len = recv(fd, buf, sizeof(buf), 0)) == -1) {
            perror("recv failed");
            abort();
        }
        request += String(buf, buf + len); // 将数据添加到 request 当中

        // 如果数据小于缓冲区长度，表示数据已处理完
        if (len != sizeof(buf)) {
            break;
        }
    } while (true);
    return request;
}

String send_request(uint16_t port, const String &req) {
    int fd = -1;
    assert((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)(&addr), sizeof(addr)) == -1) {
        char buf[1024] = {};
        sprintf(buf, "connection to %d failed", port);
        perror(buf);
    }

    // 发送数据
    assert(send(fd, req.data(), req.size(), 0) == req.size());

    String rep;

    while (true) {
        char buf[4096];
        int len = recv(fd, buf, sizeof(buf), 0);
        if (len > 0) {
            rep.append(buf, buf + len);
        }
        if (len != 4096) {
            break;
        }
    }

    close(fd);

    return rep;
}
