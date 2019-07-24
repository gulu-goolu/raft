#include "node.h"
#include "pch.h"

int main(int argc, const char *argv[]) {
    // 读取配置
    Config config = { { 1025, 1026, 1027, 1028, 1029 }, 1024 };

    // 开始执行
    Node node = {};
    node.run(config);
}