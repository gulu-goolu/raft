# raft

* [Overview](#Overview)
* [Raft](#Raft)
* [RPC](#RPC)
* [测试](#测试)
* [其他](#其他)

## Overview

Raft 共识算法实现，项目包括两部分：

- C++ 编写的 Raft 实现
- Python 编写的命令行工具

### Requirements:

- CMake 3.13
- Python 3.7
- Clang 7.0 or visual studio 2015
- Git 2.21

### Build

```cpp
git clone --recursive https://github.com/murmur-wheel/raft.git
mkdir raft-build && cd raft-build
cmake ../raft
```

然后用 Visual Studio 打开 raft.sln 生成项目/ Linux 平台直接执行 build 命令

### Usage

```bash
./start.sh # 启动集群
python3 cli.py set key1 value1 # 设置 key 的值为 valu1
python3 cli.py get key1 # 获取 key 的值
python3 cli.py echo  # 打印 leader 信息
```

### Items

- **src** 项目源码
- **cli.py** 命令行客户端
- **CMakeLists.txt** 构建脚本
- **3rdparty** 第三方库

## 一、Raft

### 1.1 架构

我们的 Raft 实现由 MessageQueue + 主线程 + 线程池 + 定时器四个组件组成：

- **主线程** 更新节点状态
- **MessageQueue** 子线程和主线程之间的通信
- **线程池** 用来创建和回收子线程
- **定时器** 实现心跳，选举中的定时回调功能

有了这四个组件，就可以不用去考虑繁杂的多线程竞争关系，因为所有对主节点的读写操作都是在主线程中完成的，而主线程中所有的消息都来自于消息队列（线程安全的），子线程通过消息队列将需要主线程处理的逻辑传递给主线程，这样就有效地避免多个线程同时读写节点引入的临界区问题。

同时为了简化实现的难度，作如下约定：

1. 一个进程表示一个节点（贫穷而不能配置多节点的环境）
2. 所有节点都绑定在 `127.0.0.1` 这个 IP 上，同时所绑定的端口号就是节点的 id（不用进程号作为节点 id 是因为在进程重启前后的进程号是不同的）
3. 只有 leader 节点才允许监听 1024 端口（这个端口专门用来接收用户命令）

### 1.2 选主

选主的流程（图来自 raft 论文）：

![flow](/images/flow.png)

### 1.3 日志复制

用户命令分两阶段执行：

1. leader 向集群中的 follower 发送 append 指令
2. 在大部分节点的 `append` 指令执行完毕后，leader 在自己的状态机中执行用户命令

### 1.4 集群配置变更流程

## 二、RPC

### 2.1 消息格式

```cpp
struct Message {
    Ptr<TcpStream> stream;
    String op;
    Json params;
};
```

- **stream** TCP 连接上下文
- **op** 消息类型
- **params** 操作参数

响应：

```json
{
    ...
}
```

> 响应是一个 json 对象，其中包含具体的数据，即便没有任何数据需要被返回，也必须返回一个 `{}`。

### 2.2 内部消息

|op|描述|
|:-|:-|
|**timeout**|选举超时|
|**rollback**|回滚|
|**commit**|提交|
|**heartbeat**|向集群中其他节点发送心跳|

### 2.3 节点间消息

|op|描述|
|:-|:-|
|**vote**|选举|
|**append**|附加日志请求（也用做心跳）|

### 2.4 用户命令

|op|描述|
|:-|:-|
|**echo**|打印进程 id|
|**get**|设置某个 key|
|**set**|获取某个 key|


### 2.4 不同类型的节点收到用户命令时的行为

- **follower**
  - **vote** 更新 **term**，重设选举超时时间
  - **heart** 更新 **term**，重设选举超时时间
  - **log** 复制日志
- **candidate**
  - **heart** 更新 **term**
  - **vote** 选举
- **leader**
  - **user** 处理用户命令


## 三、测试

测试样例是以 XML 格式定义的

## 四、其他

### RAII 和引用计数

实现中的套接字和对象都是我们要管理的资源

### FAQ

#### **1.处理网络异常**

- 超时
- connection refused

#### **2.长连接？短连接？**

为了降低实现的难度，在实现 Raft 算法的过程中，我们使用短连接，即在会话结束后，就立即销毁连接。
