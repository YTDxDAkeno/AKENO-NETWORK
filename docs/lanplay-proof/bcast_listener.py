#!/usr/bin/env python3
"""UDP broadcast listener for the AKENO LAN transport proof.

Waits for one broadcast beacon on a port and reports whether it arrived. This is
what proves the L2 overlay carries LAN-discovery broadcasts (what most LAN games
use to find each other). Run this on the SECOND machine, then run bcast_send.py
on the first within the timeout window.

Usage:
    python3 bcast_listener.py [--port 45000] [--timeout 5.0]

Exit code 0 if a beacon was received, 1 if it timed out. Ordinary UDP socket.
"""
import argparse
import socket
import sys


def main() -> None:
    ap = argparse.ArgumentParser(description="UDP broadcast listener (AKENO LAN proof).")
    ap.add_argument("--port", type=int, default=45000, help="UDP port (default 45000)")
    ap.add_argument("--timeout", type=float, default=5.0, help="seconds to wait (default 5.0)")
    args = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", args.port))
    s.settimeout(args.timeout)
    print(f"listening for a broadcast beacon on 0.0.0.0:{args.port} "
          f"(up to {args.timeout:.0f}s)", flush=True)
    try:
        data, addr = s.recvfrom(2048)
        print(f"DISCOVERY RECEIVED from {addr[0]}: {data.decode(errors='replace')}")
        sys.exit(0)
    except socket.timeout:
        print("NO BROADCAST RECEIVED (discovery failed)")
        sys.exit(1)


if __name__ == "__main__":
    main()
