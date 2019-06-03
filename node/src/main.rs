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
///     "bind": {
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
    bind: Addr,
    peers: Vec<Addr>,
}

impl Node {
    ///
    /// 从命令行初始化节点
    fn from_args() -> Node {
        for argument in env::args() {
            println!("args = {}", argument);
        }
        // 初始化节点
        Node {
            bind: {
                ip: String::from("123"),
                port: 0,
            },
            peers: vec![],
        }
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
