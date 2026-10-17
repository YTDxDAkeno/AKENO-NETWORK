#!/usr/bin/env bash
# AKENO LAN Play — real transport proof with an established tool (SoftEther VPN).
#
# This script does NOT implement any packet capture/injection/forwarding engine. It uses the
# maintained, open-source SoftEther VPN through its documented commands to build a Virtual Hub
# (a real L2 virtual Ethernet segment), connects two client adapters to it, isolates each in its
# own Linux network namespace, and then runs ordinary UDP traffic between them to verify:
#   1. real bidirectional UDP delivery over the tunnel,
#   2. UDP broadcast discovery crossing the L2 segment (what LAN games use and L3 VPNs cannot do),
#   3. latency/jitter/loss, including an optional WAN emulation (needs the sch_netem kernel module).
#
# It is a stand-in for "two isolated PC test environments on one virtual LAN". It proves the
# transport mechanics; it is NOT a game test. Run as root on a Linux host with /dev/net/tun.
#
#   sudo apt-get install -y softether-vpnserver softether-vpnclient softether-vpncmd iproute2
#   sudo bash docs/lanplay-proof/run-proof.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SUB=10.9.0
cleanup() {
  vpncmd /CLIENT localhost /CMD AccountDisconnect acc1 >/dev/null 2>&1 || true
  vpncmd /CLIENT localhost /CMD AccountDisconnect acc2 >/dev/null 2>&1 || true
  vpnclient stop >/dev/null 2>&1 || true
  vpnserver stop >/dev/null 2>&1 || true
  ip netns del ns1 2>/dev/null || true
  ip netns del ns2 2>/dev/null || true
}
trap cleanup EXIT
mkdir -p /run/softether

echo "== Start SoftEther VPN Server and create an L2 Virtual Hub =="
vpnserver start >/dev/null; sleep 3
vpncmd /SERVER localhost /PASSWORD: /CMD HubCreate AKENO /PASSWORD:hubpw >/dev/null
vpncmd /SERVER localhost /PASSWORD: /ADMINHUB:AKENO /CMD SecureNatDisable >/dev/null 2>&1 || true
for u in 1 2; do
  vpncmd /SERVER localhost /PASSWORD: /ADMINHUB:AKENO /CMD UserCreate u$u /GROUP:none /REALNAME:none /NOTE:none >/dev/null
  vpncmd /SERVER localhost /PASSWORD: /ADMINHUB:AKENO /CMD UserPasswordSet u$u /PASSWORD:pw$u >/dev/null
done

echo "== Connect two client adapters to the hub =="
vpnclient start >/dev/null; sleep 2
vpncmd /CLIENT localhost /CMD NicCreate se0 >/dev/null
vpncmd /CLIENT localhost /CMD NicCreate se1 >/dev/null
vpncmd /CLIENT localhost /CMD AccountCreate acc1 /SERVER:localhost:443 /HUB:AKENO /USERNAME:u1 /NICNAME:se0 >/dev/null
vpncmd /CLIENT localhost /CMD AccountPasswordSet acc1 /PASSWORD:pw1 /TYPE:standard >/dev/null
vpncmd /CLIENT localhost /CMD AccountCreate acc2 /SERVER:localhost:443 /HUB:AKENO /USERNAME:u2 /NICNAME:se1 >/dev/null
vpncmd /CLIENT localhost /CMD AccountPasswordSet acc2 /PASSWORD:pw2 /TYPE:standard >/dev/null
vpncmd /CLIENT localhost /CMD AccountConnect acc1 >/dev/null
vpncmd /CLIENT localhost /CMD AccountConnect acc2 >/dev/null
sleep 3

echo "== Isolate each adapter in its own namespace =="
ip netns add ns1; ip netns add ns2
ip link set vpn_se0 netns ns1
ip link set vpn_se1 netns ns2
ip netns exec ns1 sh -c "ip addr add $SUB.1/24 dev vpn_se0; ip link set vpn_se0 up; ip link set lo up"
ip netns exec ns2 sh -c "ip addr add $SUB.2/24 dev vpn_se1; ip link set vpn_se1 up; ip link set lo up"
sleep 2

echo "== L2/L3 reachability over SoftEther =="
ip netns exec ns1 ping -c 4 -W 2 $SUB.2

if [ "${WAN:-0}" = "1" ]; then
  echo "== WAN emulation: 120ms +/-15ms, 2% loss (needs sch_netem) =="
  ip netns exec ns1 tc qdisc add dev vpn_se0 root netem delay 120ms 15ms loss 2% || echo "sch_netem unavailable; skipping"
fi

echo "== Bidirectional UDP (500 datagrams, ns1 -> ns2 echo) =="
ip netns exec ns2 python3 "$HERE/udp_echo.py" & EPID=$!; sleep 1
ip netns exec ns1 python3 "$HERE/udp_client.py"
kill $EPID 2>/dev/null || true

echo "== UDP broadcast discovery across the L2 segment =="
ip netns exec ns2 python3 "$HERE/bcast_listener.py" & LPID=$!; sleep 1
ip netns exec ns1 python3 "$HERE/bcast_send.py"
wait $LPID 2>/dev/null || true

echo "== Hub traffic counters (frames really traversed SoftEther) =="
vpncmd /SERVER localhost /PASSWORD: /ADMINHUB:AKENO /CMD SessionGet SID-U1-1 2>/dev/null \
  | grep -iE "Outgoing Data Size|Incoming Data Size|Unicast Packets|Broadcast Packets" || true
echo "== Done =="
