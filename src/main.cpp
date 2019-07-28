#include "pch.h"

#include "node.h"

int main(int argc, const char *argv[]) {
    Config config = {};
    if (argc == 1) {
        config.user_port = 1024;
        config.heartbeat_period = 3000;
        config.timeout.min_val = 7500;
        config.timeout.max_val = 15000;
        config.nodes = { 1025, 1026, 1027 };
    } else {
        /* 从启动参数加载配置 */
        printf("config: %s\n", argv[1]);
        config = Config::from(Json::parse(argv[1]));
    }

#ifndef _WIN32
    daemon(1, 1);
#endif

    Node node = {};
    node.run(config);
    return 0;
}
