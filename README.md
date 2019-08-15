# raft

## Overview

Raft 共识算法实现，项目包括两部分：

- C++ 编写的 Raft 实现
- Python 编写的命令行工具

### Requirements

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

### 1.3 日志复制和安全性

日志复制的目的在于使 follower 和 leader 的已提交的日志保持一致。leader 会用 [next_index[node], commit_index] 这个区间的日志来构造一个附加日志请求，follower 会根据自身的状态以及请求的内容来决定接受或者拒绝请求，然后 leader 依据 follower 返回的结果来决定如何更新 match_index[node], next_index[node]以及是否要开始下一次尝试。

有以下几点需要注意：

1. commit_index 是已经提交的日志，last_applied 是已经应用到状态机的最后一条日志，last_log_index 是已经固化的最后一条日志，之所以用三个独立的变量来描述日志是因为：并不是所有日志都是已提交的状态，并不是所有已提交的日志都被状态机执行。

提交日志的时机：

1. 对于 leader 来说，只要日志被复制到绝大多数的节点，日志就可以被提交
2. 对于 follower 来说，只要日志已被 leader 提交，日志就可以被提交，但是因为在某些节点上，拥有的日志并不完全，所有 follower 决定哪些日志是可提交的时候还会考虑本节点的 last_log_index。因为很显然的是，只有本届上已经存在的日志，才是可以被提交的日志。
3. 一旦日志被提交，就不能再更改，即是说，commit_index 是单调递增的

应用日志的时机：

1. 一旦日志已被提交，就可以被应用到状态机上。在我们的实现中，提交日志和应用日志是紧接着的。

> note: next_index 和 match_index 并不是数组，而是 map，保存每个节点的 next_index 和 match_index

### 1.4 集群配置变更流程

## 二、消息

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
|**apply**|在状态机上应用日志|
|**heartbeat**|向集群中其他节点发送心跳|
|**refuse**|append 被 follower 拒绝|
|**accept**|append 请求被 follower 接受|

### 2.3 节点消息

|op|描述|
|:-|:-|
|**vote**|选举请求投票|
|**append**|附加日志请求（也用做心跳）|

### 2.4 用户消息

|op|描述|
|:-|:-|
|**echo**|打印进程 id|
|**get**|设置某个 key|
|**set**|获取某个 key|
|**add**|添加节点|
|**remove**|删除节点|

## 三、其他

### 3.1 资源管理

在我们的 raft 实现中，我们使用了引用计数 + RAII 的方式来管理我们的套接字以及内存资源。实现中使用的引用计数是线程安全的。鉴于此，如果你要在 lambda 中应用引用计数，请使用 `=` （值传递）来捕获变量，因为通过引用来捕获变量，可能会有提前释放资源的风险。
