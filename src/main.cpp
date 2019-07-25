#include "node.h"
#include "pch.h"

int main(int argc, const char *argv[]) {
    // 读取配置
    Config config = {};
    config.user_port = 1024;
    config.heart_period = 1000;
    config.timeout.min_val = 1500;
    config.timeout.max_val = 3000;
    config.nodes = { 1025, 1026, 1027, 1028, 1029 };

    // 后台进程
    daemon(1, 1);

    // 开始执行
    Node node = {};
    node.run(config);
    return 0;
}
