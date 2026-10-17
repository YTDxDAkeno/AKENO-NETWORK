import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.bind(('10.9.0.1', 0))
s.sendto(b'AKENO-LAN-DISCOVERY akeno-party=demo game=f1-23', ('10.9.0.255', 45000))
print('broadcast beacon sent to 10.9.0.255:45000')
