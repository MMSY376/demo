#include <tins/tins.h>
#include <boost/shared_ptr.hpp>
#include <bm/Standard.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/protocol/TMultiplexedProtocol.h>

#include "hash.h"
#include "../concury.common.h"

using namespace std;
using namespace Tins;
namespace thrift_provider = apache::thrift;
namespace runtime = bm_runtime::standard;

const char* controlIf[] = { "enp0s31f6", "h1-eth0" };  // ;  // "enp0s31f6";
const char* communicateIf[] = { "enp0s31f6", "h1-eth1" };  // "h1-eth1"; // "enp0s31f6";
PacketSender sender;

string toBmStr(uint64_t data, int bitLen) {
  int l = (bitLen + 7) / 8;
  char *buff = new char[l];
  memset(buff, 0, l);
  char* p = (char*) &data;
  
  for (int i = l - 1; i >= 0; --i) {
    buff[i] = *p++;
  }
  string res = string(buff, l);
  delete[] buff;
  return res;
}

uint64_t bmStrToInt(const string &str) {
  uint64_t result = 0;
  int l = str.length();
  
  for (int i = 0; i < l; ++i) {
    result += (uint64_t) str[i] << (l - 1 - i) * 8;
  }
  
  return result;
}

class Client {
public:
  Client(const std::string &thrift_addr = "genoa.soe.ucsc.edu", const int thrift_port = 9090)
      : thrift_addr(thrift_addr), thrift_port(thrift_port) {
    using thrift_provider::protocol::TProtocol;
    using thrift_provider::protocol::TBinaryProtocol;
    using thrift_provider::protocol::TMultiplexedProtocol;
    using thrift_provider::transport::TSocket;
    using thrift_provider::transport::TTransport;
    using thrift_provider::transport::TBufferedTransport;
    
    boost::shared_ptr<TTransport> tsocket(new TSocket(thrift_addr, thrift_port));
    boost::shared_ptr<TTransport> transport(new TBufferedTransport(tsocket));
    boost::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    
    boost::shared_ptr<TMultiplexedProtocol> standard_protocol(new TMultiplexedProtocol(protocol, "standard"));
    
    bm_client = boost::shared_ptr<runtime::StandardClient>(new runtime::StandardClient(standard_protocol));
    
    transport->open();
  }
  
  boost::shared_ptr<bm_runtime::standard::StandardClient> get_client() {
    return bm_client;
  }
  
  int addEntry(const string &tableName, const string &actionName, std::vector<std::string> keys, std::vector<std::string> action_data) {
    runtime::BmMatchParams match_params(mapf(keys, function<runtime::BmMatchParam(const string&)>([](const string& key) {
      runtime::BmMatchParam match_param;
      match_param.type = runtime::BmMatchParamType::type::EXACT;

      runtime::BmMatchParamExact match_param_exact;
      match_param_exact.key = key;
      match_param.__set_exact(match_param_exact);

      return match_param;})));
    
    try {
      int handle = bm_client->bm_mt_add_entry(0, tableName, match_params, actionName, action_data, runtime::BmAddEntryOptions());
#ifndef NDEBUG
      runtime::BmMtEntry entry = getEntry(tableName, handle);
      assert(match_params == entry.match_key);
      auto gotActionData = mapf(entry.action_entry.action_data, function<uint64_t(const string&)>([](const string& key) {return bmStrToInt(key);}));
      auto actionData = mapf(action_data, function<uint64_t(const string&)>([](const string& key) {return bmStrToInt(key);}));
      assert(gotActionData == actionData);
#endif
      return handle;
    } catch (runtime::InvalidTableOperation &ito) {
      auto what = runtime::_TableOperationErrorCode_VALUES_TO_NAMES.find(ito.code)->second;
      std::cout << "Invalid table (" << tableName << ") operation (" << ito.code << "): " << what << std::endl;
      return -1;
    }
  }
  
  runtime::BmMtEntry getEntry(const string &tableName, int handle) {
    try {
      runtime::BmMtEntry _return;
      bm_client->bm_mt_get_entry(_return, 0, tableName, handle);
      return _return;
    } catch (runtime::InvalidTableOperation &ito) {
      auto what = runtime::_TableOperationErrorCode_VALUES_TO_NAMES.find(ito.code)->second;
      std::cout << "Invalid table (" << tableName << ") operation (" << ito.code << "): " << what << std::endl;
      return {};
    }
  }
  
  void modifyEntry(const string &tableName, const string &actionName, const int handle, std::vector<std::string> action_data, std::vector<std::string> keys = {}) {
    try {
      bm_client->bm_mt_modify_entry(0, tableName, handle, actionName, action_data);
      
      runtime::BmMatchParams match_params(mapf(keys, function<runtime::BmMatchParam(const string&)>([](const string& key) {
        runtime::BmMatchParam match_param;
        match_param.type = runtime::BmMatchParamType::type::EXACT;

        runtime::BmMatchParamExact match_param_exact;
        match_param_exact.key = key;
        match_param.__set_exact(match_param_exact);

        return match_param;})));
      
#ifndef NDEBUG
      runtime::BmMtEntry entry = getEntry(tableName, handle);
      assert(match_params == entry.match_key);
      assert(mapf(entry.action_entry.action_data, function<uint64_t(const string&)>([](const string& key) {return bmStrToInt(key);})) == mapf(action_data, function<uint64_t(const string&)>([](const string& key) {return bmStrToInt(key);})));
#endif
    } catch (runtime::InvalidTableOperation &ito) {
      auto what = runtime::_TableOperationErrorCode_VALUES_TO_NAMES.find(ito.code)->second;
      std::cout << "Invalid table (" << tableName << ") operation (" << ito.code << "): " << what << std::endl;
    }
  }
  
  void deleteEntry(const string &tableName, const int handle) {
    try {
      bm_client->bm_mt_delete_entry(0, tableName, handle);
    } catch (runtime::InvalidTableOperation &ito) {
      auto what = runtime::_TableOperationErrorCode_VALUES_TO_NAMES.find(ito.code)->second;
      std::cout << "Invalid table (" << tableName << ") operation (" << ito.code << "): " << what << std::endl;
    }
  }
  
  void clearEntries(const string &tableName) {
    try {
      bm_client->bm_mt_clear_entries(0, tableName, false);
    } catch (runtime::InvalidTableOperation &ito) {
      auto what = runtime::_TableOperationErrorCode_VALUES_TO_NAMES.find(ito.code)->second;
      std::cout << "Invalid table (" << tableName << ") operation (" << ito.code << "): " << what << std::endl;
    }
  }
  
private:
  std::string thrift_addr {};
  int thrift_port {};
  boost::shared_ptr<bm_runtime::standard::StandardClient> bm_client { nullptr };
};

Client client;

void send(uint src, uint16_t srcPort, uint dst, uint16_t dstPort, uint8_t protocol) {
  EthernetII eth = EthernetII("20:00:00:00:02:01", "10:00:00:00:01:02");
  IP ip = IP(IPv4Address(Endian::do_change_endian(dst)), IPv4Address(Endian::do_change_endian(src)));
  ip.protocol(protocol);
  
//  cout << "sent a pkt" << endl;
//  cout << eth.src_addr() << "->" << eth.dst_addr() << endl;
//  cout << ip.src_addr() << ":" << srcPort << "->" << ip.dst_addr() << ":" << dstPort << "@" << ip.protocol() << endl;
  
  if (protocol == 17) {
    UDP pdu = UDP(dstPort, srcPort) / RawPDU(string("foo\0", 4));
    auto pkt = eth / ip / pdu;
    sender.send(pkt);
  } else {
    TCP pdu = TCP(dstPort, srcPort) / RawPDU(string("foo\0", 4));
    auto pkt = eth / ip / pdu;
    sender.send(pkt);
  }
}

Addr_Port process(Sniffer &sniffer) {
  do {
    try {
      const PDU& pdu = *sniffer.next_packet();
      EthernetII eth = pdu.rfind_pdu<EthernetII>();
      IP ip = eth.rfind_pdu<IP>();
      
//      cout << "got a packet" << endl;
//      cout << eth.src_addr() << "->" << eth.dst_addr() << endl;
      
      try {
        UDP udp = ip.rfind_pdu<UDP>();
//        cout << ip.src_addr() << ":" << udp.sport() << "->" << ip.dst_addr() << ":" << udp.dport() << "@" << ip.protocol() << "--" << (char*) &udp.rfind_pdu<RawPDU>().payload()[0] << endl;
        return {Endian::do_change_endian(ip.dst_addr()), udp.dport()};   // addr has to be changed, but port has been changed. fxxk
      } catch (exception &e) {
        TCP tcp = ip.rfind_pdu<TCP>();
//        cout << ip.src_addr() << ":" << tcp.sport() << "->" << ip.dst_addr() << ":" << tcp.dport() << "@" << ip.protocol() << "--" << (char*) &tcp.rfind_pdu<RawPDU>().payload()[0] << endl;
        return {Endian::do_change_endian(ip.dst_addr()), tcp.dport()};
      }
    } catch (exception &e) {
    }
  } while (true);
}

/**
 * read connection info from stdin, and log important cases:
 * hash conflicts of different conn: associate mem occupy (size and taken-up),
 * served 1M packets: time and average speed
 *
 * format: 1 byte: ip version (server(dst)/client(src): 0x44, 0x46, 0x64, 0x66),
 * 1 byte Protocol Number (tcp 6/udp 17),
 * 4/16 byte src addr, 2 byte src port,
 * 4/16 byte dst addr, 2 byte dst port.
 */
void genSendAndCheck() {
  // Sniff on the provided controlIf in promiscuous mode
  SnifferConfiguration config;
  config.set_promisc_mode(true);
  // Use immediate mode so we get the packets as fast as we can
  config.set_immediate_mode(true);
  config.set_filter("(udp or tcp) and not ether src 10:00:00:00:01:02");
  
  // Only capture udp packets sent to port 53
  Sniffer *sniffer;
  try {
    sniffer = new Sniffer(communicateIf[0], config);
  } catch (exception &e) {
    sniffer = new Sniffer(communicateIf[1], config);
  }
  
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  struct timeval start, curr, last;
  gettimeofday(&start, NULL);
  last = start;
  int i = 0, round = 0;
  bool found = false;
  int stupid = 0;
  
  while (round < 5) {
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    
    tuple3Gen.gen(&tuple);
    if (tuple.protocol & 1) tuple.protocol = 17;
    else tuple.protocol = 6;
    
    vip.addr = addr++;
    vip.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: send and receive
    send(tuple.src.addr, tuple.src.port, vip.addr, vip.port, tuple.protocol);
    Addr_Port p4Dip = process(*sniffer);
    
#ifndef NDEBUG
    // Step 3: check
    // // Step 1: lookup the VIPTable to get VIPInd
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    // // Step 2: lookup corresponding Othello array
    uint16_t dipInd = othelloForQuery[vipInd].query(tuple);
    dipInd &= (HT_SIZE - 1);
    DIP& cDip = dipPools[vipInd][ht[vipInd][dipInd]];
    
    // // Step 3: lookup corresponding Othello array
    if (!(cDip.addr == p4Dip)) {
      throw exception();
    }
#endif
    
    i++;
    if (i == LOG_INTERVAL) {
      i = 0;
      round++;
      gettimeofday(&curr, NULL);
      int diff = diff_ms(curr, start);
      int mp = round * (LOG_INTERVAL / 1.0E6);
      
      printf("%dM packets time: %dms, Average speed: %lfMpps\n", mp, diff, mp * 1000.0 / diff);
      
      last = curr;
    }
  }
  delete sniffer;
}

int *othelloHandles;

void configureDataPlane(int vipInd) {
  client.addEntry("seedA", "getSeed", { toBmStr(vipInd, 8) }, { toBmStr(conn[vipInd].getHa().s, 32) });
  client.addEntry("seedB", "getSeed", { toBmStr(vipInd, 8) }, { toBmStr(conn[vipInd].getHb().s, 32) });
  
  for (int htInd = 0; htInd < HT_SIZE; ++htInd) {
    DIP& dip = dipPools[vipInd][newHt[vipInd][htInd]];
    
    int res = client.addEntry("ht", "getDIP", { toBmStr(vipInd, 8), toBmStr(htInd, 12) }, { toBmStr(dip.addr.addr, 32), toBmStr(dip.addr.port, 16) });
    assert(res == vipInd * HT_SIZE + htInd);
  }
  
  int ma = conn[vipInd].getMa();
  int mb = conn[vipInd].getMb();
  
  int handle = client.addEntry("lengthA", "getLength", { toBmStr(vipInd, 8) }, { toBmStr(ma, 32) });
  assert(vipInd == handle);
  handle = client.addEntry("lengthB", "getLength", { toBmStr(vipInd, 8) }, { toBmStr(mb, 32) });
  assert(vipInd == handle);
  
  for (int i = 0; i < ma; ++i) {
    othelloHandles[CONN_NUM * 3 * vipInd + i] = client.addEntry("othelloA", "getOthello", { toBmStr(vipInd, 8), toBmStr(i, 32) }, { toBmStr(conn[vipInd].memGet(i), 12) });
  }
  
  for (int i = 0; i < mb; ++i) {
    othelloHandles[CONN_NUM * 3 * vipInd + CONN_NUM * 2 + i] = client.addEntry("othelloB", "getOthello", { toBmStr(vipInd, 8), toBmStr(i, 32) }, { toBmStr(conn[vipInd].memGet(ma + i), 12) });
  }
}

void updateDataPlaneCallBack(int vipInd) {
  cout << "--updateDataPlaneCallBack@" << vipInd << endl;
  
  client.modifyEntry("seedA", "getSeed", vipInd, { toBmStr(conn[vipInd].getHa().s, 32) }, { toBmStr(vipInd, 8) });
  client.modifyEntry("seedB", "getSeed", vipInd, { toBmStr(conn[vipInd].getHb().s, 32) }, { toBmStr(vipInd, 8) });
  
  // Step3: write back the new ht and new othelloForQuery
  
  for (int htInd = 0; htInd < HT_SIZE; ++htInd) {
    DIP& dip = dipPools[vipInd][newHt[vipInd][htInd]];
    
    client.modifyEntry("ht", "getDIP", HT_SIZE * vipInd + htInd, { toBmStr(dip.addr.addr, 32), toBmStr(dip.addr.port, 16) }, { toBmStr(vipInd, 8), toBmStr(htInd, 12) });
  }
  
  int oldMa = othelloForQuery[vipInd].getMa();
  int oldMb = othelloForQuery[vipInd].getMb();
  int ma = conn[vipInd].getMa();
  int mb = conn[vipInd].getMb();
  
  client.modifyEntry("lengthA", "getLength", vipInd, { toBmStr(ma, 32) }, { toBmStr(vipInd, 8) });
  client.modifyEntry("lengthB", "getLength", vipInd, { toBmStr(mb, 32) }, { toBmStr(vipInd, 8) });
  
  for (int i = 0; i < ma; ++i) {
    if (i < oldMa) {
      client.modifyEntry("othelloA", "getOthello", othelloHandles[CONN_NUM * 3 * vipInd + i], { toBmStr(conn[vipInd].memGet(i), 12) }, { toBmStr(vipInd, 8), toBmStr(i, 32) });
    } else {
      othelloHandles[CONN_NUM * 3 * vipInd + i] = client.addEntry("othelloA", "getOthello", { toBmStr(vipInd, 8), toBmStr(i, 32) }, { toBmStr(conn[vipInd].memGet(i), 12) });
    }
  }
  
  for (int i = 0; i < mb; ++i) {
    if (i < oldMb) {
      client.modifyEntry("othelloB", "getOthello", othelloHandles[CONN_NUM * 3 * vipInd + CONN_NUM * 2 + i], { toBmStr(conn[vipInd].memGet(ma + i), 12) }, { toBmStr(vipInd, 8), toBmStr(i, 32) });
    } else {
      othelloHandles[CONN_NUM * 3 * vipInd + CONN_NUM * 2 + i] = client.addEntry("othelloB", "getOthello", { toBmStr(vipInd, 8), toBmStr(i, 32) }, { toBmStr(conn[vipInd].memGet(ma + i), 12) });
    }
  }
  
  memcpy(ht[vipInd], newHt[vipInd], HT_SIZE * sizeof(uint16_t));
  othelloForQuery[vipInd].updateFromControlPlane(conn[vipInd]);
}

void init() {
  commonInit();
  othelloHandles = new int[CONN_NUM * 3 * VIP_NUM];

  try {
    sender.default_interface(communicateIf[0]);
  } catch (exception &e) {
    sender.default_interface(communicateIf[1]);
  }
  
  cout << "--clearEntries" << endl;
  client.clearEntries("seedA");
  client.clearEntries("seedB");
  client.clearEntries("ht");
  client.clearEntries("lengthA");
  client.clearEntries("lengthB");
  client.clearEntries("othelloA");
  client.clearEntries("othelloB");
  
  cout << "--initDipPool" << endl;
  initDipPool();
}

int main(int argc, char **argv) {
  cout << "--init" << endl;
  init();
  
  cout << "--simulateConnectionAdd" << endl;
  simulateConnectionAdd();
  cout << "--simulateUpdatePoolData" << endl;
  simulateUpdatePoolData();
  cout << "--updateDataPlane" << endl;
  updateDataPlane();
  
  genSendAndCheck();
  
  cout << "--simulateConnectionLeave" << endl;
  simulateConnectionLeave();
  cout << "--simulateUpdatePoolData" << endl;
  simulateUpdatePoolData();
  cout << "--updateDataPlane" << endl;
  updateDataPlane();
  
  genSendAndCheck();
  
  return 0;
}
