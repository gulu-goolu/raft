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
        sprintf(msg, "send %s failed", buf.c_str());
        perror(msg);
    }
}

