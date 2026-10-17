#!/usr/bin/env python3
"""UDP latency/loss client for the AKENO LAN transport proof.

Sends N datagrams to a udp_echo.py peer and measures how many come back and how
long each round trip took. Run udp_echo.py on the peer first, then run this on
the FIRST machine pointing --dst at the peer's virtual-LAN address.

Usage:
    python3 udp_client.py --dst 10.9.0.2 [--port 40000] [--bind 10.9.0.1]
                          [--count 500] [--interval 0.002] [--timeout 1.0]

Prints one summary line: sent/echoed/loss and rtt avg/p50/p95/jitter in ms.
Ordinary UDP sockets only; no packet capture, injection or forwarding.
"""
import argparse
import socket
import time


def main() -> None:
    ap = argparse.ArgumentParser(description="UDP latency/loss client (AKENO LAN proof).")
    ap.add_argument("--dst", required=True, help="echo peer's virtual-LAN address")
    ap.add_argument("--port", type=int, default=40000, help="echo peer UDP port (default 40000)")
    ap.add_argument("--bind", default="0.0.0.0", help="local address to send from (default 0.0.0.0)")
    ap.add_argument("--count", type=int, default=500, help="datagrams to send (default 500)")
    ap.add_argument("--interval", type=float, default=0.002, help="seconds between sends (default 0.002)")
    ap.add_argument("--timeout", type=float, default=1.0, help="per-datagram reply timeout s (default 1.0)")
    args = ap.parse_args()

    dst = (args.dst, args.port)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(args.timeout)
    s.bind((args.bind, 0))
    rtts: list[float] = []
    recv = 0
    for i in range(args.count):
        t0 = time.perf_counter()
        s.sendto(f"ping-{i}-{t0}".encode(), dst)
        try:
            s.recvfrom(2048)
            rtts.append((time.perf_counter() - t0) * 1000)
            recv += 1
        except socket.timeout:
            pass
        time.sleep(args.interval)
    rtts.sort()
    n = args.count
    loss = 100 * (n - recv) / n if n else 0
    avg = sum(rtts) / len(rtts) if rtts else 0
    p50 = rtts[len(rtts) // 2] if rtts else 0
    p95 = rtts[int(len(rtts) * 0.95)] if rtts else 0
    jit = (sum(abs(rtts[i] - rtts[i - 1]) for i in range(1, len(rtts))) / (len(rtts) - 1)
           if len(rtts) > 1 else 0)
    print(f"sent={n} echoed={recv} loss={loss:.1f}% rtt_avg={avg:.2f}ms "
          f"p50={p50:.2f}ms p95={p95:.2f}ms jitter={jit:.2f}ms")


if __name__ == "__main__":
    main()
