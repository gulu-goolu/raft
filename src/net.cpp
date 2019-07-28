#include "pch.h"

#include "net.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")

void init_socket() {
    static bool started = false;
    if (!started) {
        WSADATA Ws;
        if (WSAStartup(MAKEWORD(2, 2), &Ws) != 0) {
            return;
        }
        started = true;
    }
}
void print_error(const char *err_msg) {
    char msg[1024] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        msg,
        (sizeof(msg) / sizeof(char)),
        nullptr);
    fprintf(stderr, "%s:%s", err_msg, msg);
}
#else
void init_socket() {}
void print_error(const char *err_msg) {
    perror(err_msg);
}
#endif

TcpStream::~TcpStream() {
    if (fd_ != INVALID_SOCKET) {
        close(fd_);
    }
}

void TcpStream::send(const String &buf) const {
    if (::send(fd_, buf.data(), static_cast<int>(buf.size()), 0) == -1) {
        char msg[1024] = {};
        snprintf(msg, 1024, "send %s failed", buf.c_str());
        print_error(msg);
    }
}

void TcpStream::send(const Json &obj) const {
    send(obj.dump());
}

String TcpStream::recv() const {
    String data;
    do {
        char buf[4096];
        int len = -1;
        if ((len = ::recv(fd_, buf, sizeof(buf), 0)) == -1) {
            perror("receive failed");
            return "";
        }
        data += String(buf, buf + len);

        if (len != sizeof(buf)) {
            break;
        }
    } while (true);
    return data;
}

Ptr<TcpStream> TcpStream::connect(const char *ip, uint16_t port) {
    int ret = 0;
    Socket fd = -1;
    do {
        if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
            break;
        }

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if ((ret = inet_pton(AF_INET, ip, &address.sin_addr)) == -1) {
            char msg[1024] = {};
            snprintf(msg, sizeof(msg), "%s/%d", ip, port);
            print_error(msg);
            break;
        }

        if ((ret = ::connect(fd,
                 reinterpret_cast<const sockaddr *>(&address),
                 sizeof(address))) == -1) {
            break;
        }

        return Ptr<TcpStream>::make(fd);
    } while (false);

    if (fd != INVALID_SOCKET) {
        close(fd);
    }
    return nullptr;
}

Ptr<TcpStream> TcpListener::accept() const {
    Socket fd = -1;
    if ((fd = ::accept(fd_, nullptr, nullptr)) != INVALID_SOCKET) {
        return Ptr<TcpStream>::make(fd);
    }
    perror("accept failed");
    return nullptr;
}

TcpListener::~TcpListener() {
    if (fd_ != INVALID_SOCKET) {
        close(fd_);
    }
}

Ptr<TcpListener> TcpListener::bind(const char *ip, uint16_t port) {
    init_socket();

    int ret = 0;
    Socket fd = -1;
    do {
        if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) ==
            INVALID_SOCKET) {
            break;
        }

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if ((ret = inet_pton(AF_INET, ip, &address.sin_addr)) == -1) {
            char msg[1024] = {};
            snprintf(msg, sizeof(msg), "%s/%d", ip, port);
            print_error(msg);
            break;
        }

        if (::bind(fd,
                reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == -1) {
            char msg[1024] = {};
            snprintf(msg, sizeof(msg), "bind %s/%d failed", ip, port);
            print_error(msg);
            break;
        }

        if (listen(fd, 10) == -1) {
            char msg[1024] = {};
            snprintf(msg, sizeof(msg), "listen  %s/%d failed", ip, port);
            print_error(msg);
            break;
        }

        return Ptr<TcpListener>::make(fd);
    } while (false);

    if (fd != INVALID_SOCKET) {
        close(fd);
    }
    return nullptr;
}

String TcpSession::request(uint16_t port, const String &buf) {
    const auto stream = TcpStream::connect("127.0.0.1", port);
    if (stream) {
        stream->send(buf);
        return stream->recv();
    }
    return "";
}

String TcpSession::request(uint16_t port, const Json &obj) {
    return request(port, obj.dump());
}
