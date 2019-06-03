# raft
使用 rust 实现 raft 共识算法

我们的 raft 实现分成两部分，node 和 cmd，node 根据 raft 算法提供了分布式锁等服务，cmd 是用于管理集群的命令行工具；