# raft
使用 rust 实现 raft 共识算法

我们的 raft 实现分成两部分，node 和 cmd，node 根据 raft 算法提供了分布式锁等服务，cmd 是用于管理集群的命令行工具；本 raft 实现不是一个生产力工具，同 zookeeper 等比较成熟的分布式框架相比，性能和稳定性都不是我们实现的重点。因为作为一个 demo，可读性和维护性远比性能重要；
