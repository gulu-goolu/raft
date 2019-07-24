#!/usr/bin/python3
# coding:utf-8
# 命令行工具

import argparse
import socket
import json
import sys


# 发送请求
def send_request(request: str) -> str:
    # 创建套接字
    client = socket.socket(
        socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_TCP)

    # 连接
    client.connect(("127.0.0.1", 1024))

    # 发送请求
    client.send(request.encode())

    # 接受回复
    response = client.recv(65536).decode()

    # 关闭套接字
    client.close()

    # 打印操作结果
    print("request: {}".format(request))
    print("response: {}".format(response))

    return response


# 设置
def send_set_request(key, value):
    request = {
        "op": "set",
        "params": {
            "key": key,
            "value": value
        }
    }
    response = send_request(json.dumps(request))
    print(response)


# 获取
def send_get_request(key):
    request = {
        "op": "get",
        "params": {
            "key": "key"
        }
    }
    response = send_request(json.dumps(request))
    print(response)


# 退出
def send_exit_request():
    request = {
        "op": "exit",
        "params": {}
    }
    response = send_request(json.dumps(request))


# echo
def send_echo_request():
    request = {
        "op": "echo",
        "params": {}
    }
    print(send_request(json.dumps(request)))


# 主函数
if __name__ == "__main__":
    if len(sys.argv) == 1:
        print("usage: cli.py <cmd> <params>")
    else:
        try:
            cmd = sys.argv[1]
            params = sys.argv[2:]
            if cmd == "echo":
                send_echo_request()
            elif cmd == "set":
                try:
                    send_set_request(params[0], params[1])
                except IndexError:
                    print("usage: cli.py set <key> <value>")
            elif cmd == "get":
                try:
                    send_get_request(params[0])
                except IndexError:
                    print("usage: cli.py get <key>")
            elif cmd == "exit":
                send_exit_request()
            else:
                print("usage: cli.py <cmd> <params>")
        except Exception as e:
            print(e)
