#!/usr/bin/env python3
"""UDP broadcast sender for the AKENO LAN transport proof.

Sends one broadcast beacon to the virtual LAN's broadcast address. Pair it with
bcast_listener.py on the other machine. Run the listener first.

Usage:
    python3 bcast_send.py [--bcast 10.9.0.255] [--port 45000] [--bind 10.9.0.1]
                          [--message "AKENO-LAN-DISCOVERY ..."]

--bcast must be the broadcast address of the virtual-LAN subnet (for 10.9.0.0/24
that is 10.9.0.255). Ordinary UDP socket with SO_BROADCAST; nothing is captured
or injected.
"""
import argparse
import socket


def main() -> None:
    ap = argparse.ArgumentParser(description="UDP broadcast sender (AKENO LAN proof).")
    ap.add_argument("--bcast", default="10.9.0.255", help="broadcast address (default 10.9.0.255)")
    ap.add_argument("--port", type=int, default=45000, help="UDP port (default 45000)")
    ap.add_argument("--bind", default="0.0.0.0", help="local address to send from (default 0.0.0.0)")
    ap.add_argument("--message", default="AKENO-LAN-DISCOVERY akeno-party=demo game=f1-23",
                    help="beacon payload")
    args = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind((args.bind, 0))
    s.sendto(args.message.encode(), (args.bcast, args.port))
    print(f"broadcast beacon sent to {args.bcast}:{args.port}")


if __name__ == "__main__":
    main()
