import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('0.0.0.0', 40000))
while True:
    data, addr = s.recvfrom(2048)
    s.sendto(data, addr)   # echo back
