# 把这个保存成 test_client.py 或者直接 python3 -c
import socket, struct

s = socket.socket()
s.connect(('127.0.0.1', 9000))

def send_frame(payload: bytes):
    header = struct.pack('>I', len(payload))   # > 表示大端,I 表示 uint32
    s.sendall(header + payload)

send_frame(b'hello')
send_frame(b'world')
send_frame(b'this is a longer message to test')

# 测试半帧:故意只发头
import time
header = struct.pack('>I', 10)
s.sendall(header)
time.sleep(2)              # 等 2 秒
s.sendall(b'1234567890')   # 再发 body

s.close()