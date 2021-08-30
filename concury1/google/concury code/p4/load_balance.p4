/* -*- P4_16 -*- */
#include <core.p4>
#include <v1model.p4>
#include "../../config.h"

/*************************************************************************
*********************** H E A D E R S  ***********************************
*************************************************************************/
header ethernet_t {
  bit<48> dstAddr;
  bit<48> srcAddr;
  bit<16> etherType;
}

header ipv4_t {
  bit<4>  version;
  bit<4>  ihl;
  bit<8>  diffserv;
  bit<16> totalLen;
  bit<16> identification;
  bit<3>  flags;
  bit<13> fragOffset;
  bit<8>  ttl;
  bit<8>  protocol;
  bit<16> hdrChecksum;
  bit<32> srcAddr;
  bit<32> dstAddr;
}

header tcp_t {
  bit<16> srcPort;
  bit<16> dstPort;
  bit<32> seqNo;
  bit<32> ackNo;
  bit<4>  dataOffset;
  bit<3>  res;
  bit<3>  ecn;
  bit<6>  ctrl;
  bit<16> window;
  bit<16> checksum;
  bit<16> urgentPtr;
}

header udp_t {
    bit<16> srcPort;
    bit<16> dstPort;
    bit<16> length;
    bit<16> checksum;
}

header_union l4header {
  tcp_t tcp;
  udp_t udp; 
}

struct metadata {
  bit<8> vipIndex;
  bit<12> htIndex;
  bit<32> dipAddr;
  bit<16> dipPort;

  bit<32> length;
  bit<32> hash;
  bit<32> seed;
}

struct headers {
  ethernet_t ethernet;
  ipv4_t     ipv4;
  l4header   l4;
}

/*************************************************************************
*********************** P A R S E R  ***********************************
*************************************************************************/
parser MyParser(packet_in packet,
        out headers hdr,
        inout metadata meta,
        inout standard_metadata_t standard_metadata) {
  state start {
    transition parse_ethernet;
  }
  state parse_ethernet {
    packet.extract(hdr.ethernet);
    transition select(hdr.ethernet.etherType) {
      0x800: parse_ipv4;
      default: accept;
    }
  }
  state parse_ipv4 {
    packet.extract(hdr.ipv4);

    meta.vipIndex = (bit<8>)hdr.ipv4.dstAddr;
    meta.htIndex = 0;
    
    transition select(hdr.ipv4.protocol) {
      6: parse_tcp;
      17: parse_udp;
      default: accept;
    }
  }
  state parse_tcp {
    packet.extract(hdr.l4.tcp);
    transition accept;
  }
  state parse_udp {
    packet.extract(hdr.l4.udp);
    transition accept;
  }
}

/*************************************************************************
************   C H E C K S U M    V E R I F I C A T I O N   *************
*************************************************************************/
control MyVerifyChecksum(inout headers hdr, inout metadata meta) {
  apply { }
}

/*************************************************************************
**************  I N G R E S S   P R O C E S S I N G   *******************
*************************************************************************/
control MyIngress(inout headers hdr, inout metadata meta,
          inout standard_metadata_t standard_metadata) {
  action drop() {
    mark_to_drop();
  }

  action getLength(bit<32> length) {
    meta.length = length;
  }
  
  table lengthA {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
    }
    actions = {
      getLength;
      drop;
    }
    size = VIP_NUM;
    default_action = drop;
  }

  table lengthB {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
    }
    actions = {
      getLength;
      drop;
    }
    size = VIP_NUM;
    default_action = drop;
  }

  action getSeed(bit<32> seed) {
    meta.seed = seed;
  }
  
  table seedA {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
    }
    actions = {
      getSeed;
      drop;
    }
    size = VIP_NUM;
    default_action = drop;
  }

  table seedB {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
    }
    actions = {
      getSeed;
      drop;
    }
    size = VIP_NUM;
    default_action = drop;
  }

  action getHash() {
    bit<16> srcPort;
    bit<16> dstPort;
    if(hdr.ipv4.protocol==6) {
      srcPort = hdr.l4.tcp.srcPort;
      dstPort = hdr.l4.tcp.dstPort;
    } else {
      srcPort = hdr.l4.udp.srcPort;
      dstPort = hdr.l4.udp.dstPort;
    }

    hash(meta.hash,
      HashAlgorithm.crc32,
      16w0,
      {
        hdr.ipv4.srcAddr + meta.seed,
        srcPort,
        hdr.ipv4.protocol,
        8w0
      },
      meta.length
    );
  }
  
  table hashA {   // inserted by ipc
    key = {
    }
    actions = {
      getHash;
    }
    size = 1;
    default_action = getHash;
  }

  table hashB {   // inserted by ipc
    key = {
    }
    actions = {
      getHash;
    }
    size = 1;
    default_action = getHash;
  }

  action getOthello(bit<12> queryResult) {
    meta.htIndex = meta.htIndex ^ queryResult;
  }

  table othelloA {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
      meta.hash : exact;
    }
    actions = {
      getOthello;
      drop;
    }
    size = CONN_NUM * 2 + 256;   //128*4096*4096  // TODO dynamically resize??
    default_action = drop;
  }

  table othelloB {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
      meta.hash : exact;
    }
    actions = {
      getOthello;
      drop;
    }
    size = CONN_NUM + 256;   //128*4096*4096  // TODO dynamically resize??
    default_action = drop;
  }

  action getDIP(bit<32> ip, bit<16> port) {
    meta.dipAddr = ip;
    meta.dipPort = port;
  }

  table ht {   // inserted by ipc
    key = {
      meta.vipIndex: exact;
      meta.htIndex: exact;
    }
    actions = {
      getDIP;
      drop;
    }
    size = VIP_NUM * HT_SIZE;   //128*4096*4096  // TODO dynamically resize??
    default_action = drop;
  }

  action setNextHopDummy() {
    hdr.ethernet.dstAddr = 48w0x100000000101;
    hdr.ipv4.dstAddr = meta.dipAddr;
    if(hdr.l4.tcp.isValid())
      hdr.l4.tcp.dstPort = meta.dipPort;
    else
      hdr.l4.udp.dstPort = meta.dipPort;
    standard_metadata.egress_spec = 1;
    hdr.ipv4.ttl = hdr.ipv4.ttl - 1;
  }

  action bypass() {
    standard_metadata.egress_spec = 1;
  }

  table next_hop {
    key = {
      hdr.ipv4.dstAddr: lpm;
    }
    actions = {
      setNextHopDummy;
      bypass;
    }
    size = 1;
    default_action = bypass;
  }
  
  apply {
    if (hdr.ipv4.isValid() && hdr.ipv4.ttl > 0) {
      lengthA.apply();
      seedA.apply();
      hashA.apply();
      othelloA.apply();

      lengthB.apply();
      seedB.apply();
      hashB.apply();
      othelloB.apply();

      ht.apply();
    }
    
    next_hop.apply();
  }
}

/*************************************************************************
****************  E G R E S S   P R O C E S S I N G   *******************
*************************************************************************/
control MyEgress(inout headers hdr,
         inout metadata meta,
         inout standard_metadata_t standard_metadata) {
  action rewrite_mac(bit<48> smac) {
    hdr.ethernet.srcAddr = smac;
  }

  action drop() {
    mark_to_drop();
  }

  table send_frame {
    key = {
      standard_metadata.egress_port: exact;
    }
    actions = {
      rewrite_mac;
      drop;
    }
    size = 256;
    default_action = drop;
  }
  
  apply {
    send_frame.apply();
  }
}

/*************************************************************************
*************   C H E C K S U M    C O M P U T A T I O N   **************
*************************************************************************/
control MyComputeChecksum(inout headers hdr, inout metadata meta) {
  apply {
    update_checksum(
      hdr.ipv4.isValid(),
      {
        hdr.ipv4.version,
        hdr.ipv4.ihl,
        hdr.ipv4.diffserv,
        hdr.ipv4.totalLen,
        hdr.ipv4.identification,
        hdr.ipv4.flags,
        hdr.ipv4.fragOffset,
        hdr.ipv4.ttl,
        hdr.ipv4.protocol,
        hdr.ipv4.srcAddr,
        hdr.ipv4.dstAddr 
      },
      hdr.ipv4.hdrChecksum,
      HashAlgorithm.csum16
    );
  }
}

/*************************************************************************
***********************  D E P A R S E R  *******************************
*************************************************************************/
control MyDeparser(packet_out packet, in headers hdr) {
  apply {
    packet.emit(hdr.ethernet);
    packet.emit(hdr.ipv4);
    packet.emit(hdr.l4);
  }
}

/*************************************************************************
***********************  S W I T C H  *******************************
*************************************************************************/
V1Switch(
  MyParser(),
  MyVerifyChecksum(),
  MyIngress(),
  MyEgress(),
  MyComputeChecksum(),
  MyDeparser()
) main;