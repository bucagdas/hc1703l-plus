#!/bin/sh
# HC1703L Plus - cloud block without netfilter (this 3.18 kernel has no iptables). MIT License.
# Two /1 reject routes cover the whole public IPv4 space. Private ranges are routed to the LAN gateway,
# DNS points at 127.0.0.1. The default route is left alone (p2pcam reads it). Re-applied in a loop.
# hosts-file blocking alone is not enough: p2pcam also connects to hard-coded IP addresses.
# Disable: touch <SD>/clean/NONETGUARD and reboot.
PF=/tmp/netguard.pid
p=; read p < $PF 2>/dev/null
if [ -n "$p" ] && kill -0 "$p" 2>/dev/null && grep -q netguard /proc/$p/cmdline 2>/dev/null; then exit 0; fi
echo $$ > $PF
trap '' HUP
# /etc/resolv.conf links to a file on flash. Cover it with a read-only RAM copy: nothing can write to flash
# here any more, and p2pcam cannot point DNS at the router for the seconds before the loop below corrects it.
RC=/home/etc/resolv.conf
if [ -f $RC ] && ! grep -q " $RC " /proc/mounts; then
  echo 'nameserver 127.0.0.1' > /tmp/resolv.plus &&
    mount --bind /tmp/resolv.plus $RC && mount -o remount,ro,bind $RC
fi
n=0
while :; do
  route add -net 0.0.0.0 netmask 128.0.0.0 reject 2>/dev/null
  route add -net 128.0.0.0 netmask 128.0.0.0 reject 2>/dev/null
  GW=$(route -n 2>/dev/null | awk '$1=="0.0.0.0" && $4 ~ /G/ {print $2; exit}')
  if [ -n "$GW" ]; then
    route add -net 10.0.0.0 netmask 255.0.0.0 gw $GW 2>/dev/null
    route add -net 172.16.0.0 netmask 255.240.0.0 gw $GW 2>/dev/null
    route add -net 192.168.0.0 netmask 255.255.0.0 gw $GW 2>/dev/null
  fi
  route add -host 255.255.255.255 dev wlan0 2>/dev/null
  grep -q '^nameserver 127.0.0.1' /etc/resolv.conf 2>/dev/null || echo 'nameserver 127.0.0.1' > /etc/resolv.conf
  # fast during the first two minutes (WiFi and DHCP come up), then once a minute
  n=$((n+1)); if [ $n -lt 24 ]; then sleep 5; else sleep 60; fi
done
