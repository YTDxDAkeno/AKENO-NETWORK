import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 45000))
s.settimeout(5.0)
try:
    data, addr = s.recvfrom(2048)
    print(f'DISCOVERY RECEIVED from {addr[0]}: {data.decode()}')
except socket.timeout:
    print('NO BROADCAST RECEIVED (discovery failed)')
