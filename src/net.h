#ifndef NET_H
#define NET_H

#include "pch.h"
#include "util.h"

#ifdef _WIN32

#include <WS2tcpip.h>
#include <WinSock2.h>

using Socket = SOCKET;
inline int close(Socket fd) {
    return closesocket(fd);
}

#else

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

using Socket = int;
#define INVALID_SOCKET (-1)

#endif

class TcpStream : public RefCounted {
public:
    NOCOPYABLE_BODY(TcpStream)

    TcpStream(Socket fd) : fd_(fd) {}
    ~TcpStream();

    const Socket &handle() const { return fd_; }

    void send(const String &buf) const;
    void send(const Json &obj) const;
    String recv() const;

    // connect
    static Ptr<TcpStream> connect(const char *ip, uint16_t port);

private:
    Socket fd_ = -1;
};

class TcpListener : public RefCounted {
public:
    NOCOPYABLE_BODY(TcpListener)

    TcpListener(Socket fd) : fd_(fd) {}
    ~TcpListener();

    const Socket &handle() const { return fd_; }

    Ptr<TcpStream> accept() const;

    static Ptr<TcpListener> bind(const char *ip, uint16_t port);

private:
    Socket fd_ = -1;
};

class TcpSession {
public:
    static String request(uint16_t port, const String &req);
    static String request(uint16_t port, const Json &req);
};

#endif
