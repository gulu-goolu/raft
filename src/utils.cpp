#include "utils.h"

String recv(int fd) {
    //
    int conn_fd = -1;
    if ((conn_fd = accept(fd, nullptr, nullptr)) == -1) {
        char msg[1024] = {};
        sprintf(msg, "accept failed");
        perror(msg);
        abort();
    }

    return recv_all(conn_fd);
}

void send(int fd, const String &buf) {
    send_all(fd, buf);
    // 关闭套接字
    close(fd);
}

void send(int fd, const Json &obj) {
    const String buf = obj.dump();
    send(fd, obj.dump());
}

String send_request(uint16_t port, const String &req) {
    String rep;
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
    } else {
        // 发送数据
        send_all(fd, req);
        rep = recv_all(fd);
    }

    close(fd);

    return rep;
}
