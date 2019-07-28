#coding: utf-8
# 执行自动测试

import os
from xml.etree.ElementTree import *
import subprocess
import shutil


# 设置 storage 文件夹
def setup_storage_directory(path: str, element: ElementTree):
    pass


class Test:
    def __init__(self, element: Element):
        for it in element.iter("config"):
            self.config = it.text
        self.nodes = {}
        for it in element.iter("storage"):
            for n in it.iter("node"):
                self.nodes[n.get("id")] = n.text
        pass

    def run(self):
        if not os.path.exists("raft-node"):
            print("在目录下没有找到 raft-node")
        self.prepare_storage()
        # subprocess.run("./raft-node" + "\'" + self.config + "\'")
        pass

    def prepare_storage(self):
        shutil.rmtree("storage")
        os.makedirs("storage", 0o777, True)
        for node in self.nodes.items():
            fd = open("storage/" + str(node[0]) + ".json", "w+").write(node[1])
        pass


if __name__ == "__main__":
    registry = parse("registry.xml")
    root = registry.getroot()
    tests = []
    for var in root.iter("test"):
        tests.append(Test(var))
    for test in tests:
        test.run()
    pass
