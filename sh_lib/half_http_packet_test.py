import socket
import time

s = socket.socket()
s.connect(('192.168.182.128', 8888))

# 第一批：只发请求行
s.send(b"GET /ping HTTP/1.1\r\n")
time.sleep(1)

# 第二批：发 headers 但没有空行
s.send(b"Host: 192.168.182.128:8888\r\n")
time.sleep(1)

# 第三批：发空行，请求完整了
s.send(b"\r\n")

print(s.recv(1024))
s.close()