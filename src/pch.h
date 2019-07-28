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

#include "../3rdparty/json/single_include/nlohmann/json.hpp"

using namespace std::chrono;

#define NOCOPYABLE_BODY(NAME) \
    NAME(const NAME &) = delete; \
    NAME(NAME &&) = delete; \
    NAME &operator=(const NAME &) = delete; \
    NAME &operator=(NAME &&) = delete;

// type alias
template<typename _Ty>
using Vector = std::vector<_Ty>;
using String = std::string;
using Json = nlohmann::json;
template<typename _Kty, typename _Ty>
using HashMap = std::unordered_map<_Kty, _Ty>;

/**
 * 配置选项
 */
struct Config {
    Vector<uint16_t> nodes;
    uint16_t user_port;
    struct Timeout {
        uint32_t min_val;
        uint32_t max_val;
        uint32_t rand() const {
            return min_val + ::rand() % (max_val - min_val);
        }
    } timeout;
    uint32_t heartbeat_period;

    static Config from(const Json &obj) {
        Config config = {};
        if (obj.contains("user_port")) {
            config.user_port = obj.at("user_port").get<uint16_t>();
        }
        if (obj.contains("heartbeat_period")) {
            config.heartbeat_period =
                obj.at("heartbeat_period").get<uint32_t>();
        }
        if (obj.contains("nodes")) {
            for (const auto &t : obj.at("nodes")) {
                config.nodes.push_back(t.get<uint16_t>());
            }
        }
        if (obj.contains("timeout")) {
            const auto &timeout = obj.at("timeout");
            if (timeout.contains("max")) {
                config.timeout.max_val = timeout.at("max").get<uint32_t>();
            }
            if (timeout.contains("min")) {
                config.timeout.min_val = timeout.at("min").get<uint32_t>();
            }
        }
        return config;
    }
};

String read_file(const char *path);
void write_file(const String &path, const String &buf);

#endif
