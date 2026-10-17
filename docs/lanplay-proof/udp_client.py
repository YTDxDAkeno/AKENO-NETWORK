import socket, time, sys
N = 500
dst = ('10.9.0.2', 40000)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(1.0)
s.bind(('10.9.0.1', 0))
rtts = []; recv = 0
for i in range(N):
    t0 = time.perf_counter()
    s.sendto(f'ping-{i}-{t0}'.encode(), dst)
    try:
        data, _ = s.recvfrom(2048)
        rtts.append((time.perf_counter()-t0)*1000)
        recv += 1
    except socket.timeout:
        pass
    time.sleep(0.002)
rtts.sort()
loss = 100*(N-recv)/N
avg = sum(rtts)/len(rtts) if rtts else 0
p50 = rtts[len(rtts)//2] if rtts else 0
p95 = rtts[int(len(rtts)*0.95)] if rtts else 0
# jitter: mean abs consecutive diff
jit = sum(abs(rtts[i]-rtts[i-1]) for i in range(1,len(rtts)))/(len(rtts)-1) if len(rtts)>1 else 0
print(f'sent={N} echoed={recv} loss={loss:.1f}% rtt_avg={avg:.2f}ms p50={p50:.2f}ms p95={p95:.2f}ms jitter={jit:.2f}ms')
