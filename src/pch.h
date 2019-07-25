#ifndef PCH_H
#define PCH_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../3rdparty/json/single_include/nlohmann/json.hpp"

using namespace std::chrono;

#if __cplusplus < 201403
namespace std {
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
#endif

// type alias
template<typename _Ty>
using Vector = std::vector<_Ty>;
using String = std::string;
using Json = nlohmann::json;
template<typename _Kty, typename _Ty>
using HashMap = std::unordered_map<_Kty, _Ty>;

// 集群配置信息
struct Config {
    Vector<uint16_t> nodes;
    uint16_t user_port;
    // 选举超时时间
    struct Timeout {
        uint32_t min_val;
        uint32_t max_val;
        uint32_t rand() const {
            return min_val + ::rand() % (max_val - min_val);
        }
    } timeout;
    uint32_t heart_period; // 心跳周期
};

// 读取文件
String read_file(const char *path);
// 写文件
void write_file(const String &path, const String &buf);
// 读取全部内容
String recv_all(int fd);
// 回复
void send_all(int fd, const String &buf);

#endif
