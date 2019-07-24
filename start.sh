#!/bin/bash

# 创建一个 db 文件夹
mkdir -p db

# 启动 5 个子进程
for ((i = 1; i <= 5; i++))
do (
    ./raft-node &
)
done

pidof raft-node
