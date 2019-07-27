#coding: utf-8
# usage:
#   python3 cli.py 1025 1026 1027 1028
import subprocess

if __name__ == "__main__":
    config = open("config.json")
    subprocess.getstatusoutput("./raft-node")
    print("good")
    pass
