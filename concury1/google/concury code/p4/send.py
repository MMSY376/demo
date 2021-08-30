#!/usr/bin/env python
import argparse
import sys
import socket
import random
import struct
import math

from scapy.all import sendp, send, get_if_list, get_if_hwaddr
from scapy.all import Packet
from scapy.all import Ether, IP, UDP, TCP


def get_if():
    ifs = get_if_list()
    iface = None  # "h1-eth0"
    for i in get_if_list():
        if "eth0" in i:
            iface = i
            break
    if not iface:
        print "Cannot find eth0 interface"
        exit(1)
    return iface


def main():
    if len(sys.argv) < 2:
        print 'pass 1 arguments: "<message>"'
        exit(1)

    srcAddr = str(random.randint(0, 16 * 1024 * 1024))
    dstAddr = socket.gethostbyname("10.2.0.%d" % (random.randint(0, 128)))
    iface = get_if()

    print "sending on interface %s to %s" % (iface, str(dstAddr))
    pkt = Ether(src=get_if_hwaddr(iface), dst='ff:ff:ff:ff:ff:ff')
    pkt = pkt / IP(src=srcAddr, dst=dstAddr) / UDP(dport=1234, sport=random.randint(49152, 65535)) / sys.argv[1]
    # pkt.show2()
    sendp(pkt, iface=iface, verbose=False)


if __name__ == '__main__':
    main()
