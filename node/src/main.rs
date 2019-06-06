use std::cell::RefCell;
use std::env;
use std::net::{TcpListener, TcpStream, Ipv4Addr};

const USAGE: &'static str = "
Usage:
  node '{}'
";

/// 启动参数
/// ```json
/// {
///     "listen": {
///         "ip": "127.0.0.1",
///         "port": 8889
///     },
///     "peers":[{
///         "ip": "12.17.0.2",
///         "port": 9900
///     }]
/// }
/// ```
struct Addr {
    ip: String,
    port: u32,
}
struct Node {
    /// 监听的端口
    listen: Addr,
    /// 
    peers: Vec<Addr>,
    
    /// leader 选举
  
    /// 日志复制
    commitIdx: u32,
}

impl Node {
    ///
    /// 从命令行初始化节点
    fn from_args() -> Rc<Node> {
    }

    /// 运行此节点
    fn run(&self) {
        println!("run");
    }
}

fn main() {
    println!("{:?}", args);
    let node = Node::from_args();
    node.run();
}
