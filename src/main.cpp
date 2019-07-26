#include "pch.h"

#include "node.h"

int main(int argc, const char *argv[]) {
    // init config
    Config config = {};
    config.user_port = 1024;
    config.heart_period = 3000;
    config.timeout.min_val = 7500;
    config.timeout.max_val = 15000;
    config.nodes = { 1025, 1026, 1027 };

#ifndef _WIN32
    daemon(1, 1);
#endif

    Node node = {};
    node.run(config);
    return 0;
}
