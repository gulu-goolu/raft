# raft

C++ 实现 Raft 共识算法

## 一、流程图

![flow](/images/flow.png)

## 二、RPC

### 2.1 数据格式

```json
{
    "op":"OP",
    "params":{
        ...
    }
}
```

- **op** 操作类型，必须时 string 类型
- **params** 参数，必须时一个 object 类型

### 2.2 内部指令

- **vote** 选举
- **heart** 心跳
- **log** 日志复制

### 2.3 用户指令

- **echo** 打印进程 id
- **get** 设置某个 key
- **set** 获取某个 key


### 2.4 不同类型的节点再收到用户命令时的行为

- **follower**
  - **vote** 更新 **term**，重设选举超时时间
  - **heart** 更新 **term**，重设选举超时时间
  - **log** 复制日志
- **candidate**
  - **heart** 更新 **term**
  - **vote** 选举
- **leader**
   - **user** 处理用户命令


## 三、其他

### **1.处理网络异常**

- 超时
- connection refused

### **2.多线程**

我们的实现包括 4 个线程：

- **主线程** 初始化，监听端口
- **选举线程** 选举 leader，通过 Timer 实现
- **心跳线程** 维护心跳，通过 Timer 实现
- **用户线程** 监听 1024 端口，处理用户命令
