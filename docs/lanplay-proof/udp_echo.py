#!/usr/bin/env python3
"""UDP echo server for the AKENO LAN transport proof.

Binds a UDP port and echoes every datagram back to its sender. This is one half
of the bidirectional-UDP check; run it on the machine that acts as the echo peer
(by convention the SECOND machine), then run udp_client.py on the first.

Usage:
    python3 udp_echo.py [--bind 0.0.0.0] [--port 40000]

It uses only an ordinary UDP socket. There is no packet capture, injection or
forwarding here: it is a normal sendto/recvfrom echo.
"""
import argparse
import socket


def main() -> None:
    ap = argparse.ArgumentParser(description="UDP echo server (AKENO LAN proof).")
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on (default 0.0.0.0)")
    ap.add_argument("--port", type=int, default=40000, help="UDP port (default 40000)")
    args = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((args.bind, args.port))
    print(f"echo server listening on {args.bind}:{args.port} (Ctrl+C to stop)", flush=True)
    while True:
        data, addr = s.recvfrom(2048)
        s.sendto(data, addr)  # echo back


if __name__ == "__main__":
    main()
