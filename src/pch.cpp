#include "pch.h"

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

Config Config::load(const char *path) {
    return from(read_file(path));
}

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
    printf("read %s:%s\n", path, buf.c_str());
    fclose(fp);

    return buf;
}

void write_file(const String &path, const String &buf) {
    printf("write %s: %s\n", path.c_str(), buf.c_str());
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

String recv_all(int fd) {
    // 读数据
    String data;
    do {
        char buf[4096];
        int len = -1;
        if ((len = recv(fd, buf, sizeof(buf), 0)) == -1) {
            // 读取数据失败，打印错误，返回空字符串
            perror("recv failed");
            return "";
        }
        data += String(buf, buf + len); // 将数据追加到 data 当中

        // 如果数据小于缓冲区长度，表示数据已处理完
        if (len != sizeof(buf)) {
            break;
        }
    } while (true);
    return data;
}

void send_all(int fd, const String &buf) {
    if (send(fd, buf.data(), buf.size(), 0) != buf.size()) {
        char msg[1024] = {};
        sprintf(msg, "send %s failed\n", buf.c_str());
        perror(msg);
    }
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
        sprintf(buf, "connect to %d failed", port);
        perror(buf);
    }

    // 发送数据
    send_all(fd, req);
    String rep = recv_all(fd);
    close(fd);

    return rep;
}
