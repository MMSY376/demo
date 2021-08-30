#include "common.h"
//#include "libcuckoo/cuckoohash_map.hh"
#include "presized_cuckoo/control_plane_cuckoo_map.h"
#include "hash.h"

vector<Hasher32<Tuple5>> hashers;
vector<DataPlaneCuckooMap<Tuple5, uint8_t>> dpConnTables;                   // 5-tuple to version: 16 -> 6
//map<pair<uint8_t, uint16_t>, pair<const Tuple5, uint8_t>> connMemo;  // stage, digest -> conn, version
//Tuple5 dummyConn;
//vector<int> dummyCntOfStage;
//int dummyCnt = 0;
//int fail = 0;

vector<vector<DIP>> dipPools[VIP_NUM];  // vipIndex, version, dipindex -> dip

vector<ControlPlaneCuckooMap<Tuple5, uint8_t>> cpConnTables;
ControlPlaneCuckooMap<Addr_Port, uint8_t> vipTable(VIP_NUM);             // vip->version: 144 -> 6
ControlPlaneCuckooMap<pair<Addr_Port, uint8_t>, uint8_t> dipPoolTable(DIP_NUM);    // vip, version->dip_pool: 144 -> DIPPoolIndex 6

void printMemoryUsage() {
  uint64_t size = 0;
  
  for (int i = 0; i < dpConnTables.size(); ++i) {
    size += cpConnTables[i].EntryCount();
    cout << "connTrackingTable" << i << " entries: " << human(cpConnTables[i].EntryCount()) << endl;
  }
  size = size * sizeof(DataPlaneCuckooMap<Tuple5, uint8_t>::Bucket) / DataPlaneCuckooMap<Tuple5, uint8_t>::kSlotsPerBucket;
  
  size += VIP_NUM * sizeof(ControlPlaneCuckooMap<Addr_Port, uint8_t>::Bucket) / ControlPlaneCuckooMap<Addr_Port, uint8_t>::kSlotsPerBucket;
  size += DIP_NUM * sizeof(ControlPlaneCuckooMap<pair<Addr_Port, uint8_t>, uint8_t>::Bucket) / ControlPlaneCuckooMap<pair<Addr_Port, uint8_t>, uint8_t>::kSlotsPerBucket;
  
  size *= 1.1;
  
  // dipTable
  size += VIP_NUM * HT_SIZE * sizeof(uint16_t) + DIP_NUM * sizeof(DIP);
  
  cout << "Total memory usage: " << size << endl;
  memoryLog << CONN_NUM << " " << size << endl;
}

uint64_t getTotalEntires() {
  uint64_t size = 0;
  for (int i = 0; i < cpConnTables.size(); ++i) {
    size += cpConnTables[i].EntryCount();
  }
  
  return size;
}

/**
 * perform lookup, and do the collision detection later
 */
bool bigVirtualConnTable(const Tuple5& incoming, uint8_t& result) {
  for (int i = 0; i < dpConnTables.size(); ++i) {
    if (dpConnTables[i].Find(incoming, &result)) {  // hit in current stage table
      return true;
    } else {
      // miss in curr stage connTable, maybe in next stage
    }
  }
  
  return false;
}

/**
 * conntable mustn't have the incoming tuples
 *
 * insert tuple to this stage or later stages. keep these characteristics:
 * 1. all tuples have different digests.
 */
bool insertToConnTable(deque<pair<const Tuple5, uint8_t>>& waiting, uint8_t stage) {
  if (stage >= cpConnTables.size()) {
    if (stage < 250) {
      int size = cpConnTables.back().EntryCount() >> 2;
      cpConnTables.push_back(ControlPlaneCuckooMap<Tuple5, uint8_t>(size));
      dpConnTables.push_back(DataPlaneCuckooMap<Tuple5, uint8_t>(cpConnTables.back()));   // cascade, initially 4 empty table
      
      // refresh all stage reference. fuck c++
      for (int i = 0; i < cpConnTables.size(); ++i) {
        cpConnTables[i].SetAssociated(dpConnTables[i]);
      }
      hashers.push_back(cpConnTables.back().getDigestFunction());
    } else {
      return false;
    }
  }
  
  deque<pair<const Tuple5, uint8_t>> nextStage;
  
  while (!waiting.empty()) {
    auto tmp = waiting.front();
    waiting.pop_front();
    
    const Tuple5 tuple = tmp.first;
    uint8_t version = tmp.second;
    const Tuple5 *insRes = cpConnTables[stage].InsertAvoidDigestCollision(tuple, version);
    
    if (insRes == &tuple) {
      // valid insertion!
    } else {
      nextStage.push_back(make_pair(tuple, version));  //invalid insertion, move to next stage
      
      if (insRes != 0) {
        // insertion invalid because of the collision, must move the two to next stage.
        uint8_t collidedVersion;
        bool findRes = cpConnTables[stage].Find(*insRes, &collidedVersion);
        assert(findRes);
        bool removeRes = cpConnTables[stage].Remove(*insRes);
        assert(removeRes);
        
        nextStage.push_back(make_pair(*insRes, collidedVersion));
      } else { // full, just move to next stage
      }
    }
  }
  
  if (nextStage.size()) return insertToConnTable(nextStage, stage + 1);
  else return true;
}

bool insertToConnTable(const Tuple5& incoming, uint8_t version) {
  deque<pair<const Tuple5, uint8_t>> waiting;
  waiting.push_back(make_pair(incoming, version));
  
  return insertToConnTable(waiting, 0);
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
void serve() {
  struct timeval start, curr, last;
  
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  gettimeofday(&start, NULL);
  last = start;
  int i = 0, round = 0;
  int stupid = 0;
  
  while (round < 5) {
    bool failed = false;
    
    // Step 1: read 5-tuple of a packet
    Tuple5 tuple;
    tuple3Gen.gen((Tuple3*) &tuple.src);
    tuple.dst.addr = addr++;
    tuple.dst.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // we do not simulate SYN, so use the number of iteration to judge.
    bool syn = false;
    
    bool hit = false;
    uint8_t version;
    
    // Step 2: lookup the ConnTable
    // note: handle SYN packets: syn packets should be directly inserted into the connTable to pypass the lookup
    hit = bigVirtualConnTable(tuple, version);
    assert(hit);
    
    // Step 4: lookup the DipPoolTable
    uint8_t dipPoolIndex;
    dipPoolTable.Find(make_pair(tuple.dst, version), &dipPoolIndex);  // must hit
    
    uint16_t vipInd = tuple.dst.addr & VIP_MASK;
    auto &pool = dipPools[vipInd][dipPoolIndex];
    DIP dip = pool[hashers[0](tuple) % (pool.size())];
    
    stupid += dip.addr.addr;   //prevent optimize
    i++;
    
    if (i == LOG_INTERVAL) {
      i = 0;
      round++;
      // gettimeofday(&curr, NULL);
      // int diff = diff_ms(curr, start);
      // int mp = round * (LOG_INTERVAL / 1.0E6);
      // cout << mp << "M packets time: " << diff << "ms, Average speed: " << mp * 1000.0 / diff << "Mpps, " << diff / mp << "nspp" << endl;
      // last = curr;
    }
  }
  
  sync_printf("%d\b \b", stupid & 7);
  gettimeofday(&curr, NULL);
  int diff = diff_ms(curr, start);
  queryLog << 1 << " " << CONN_NUM << " " << 5 * LOG_INTERVAL / (diff / 1000.0) << endl;
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
void simulateConnectionAdd(int limit = 0, int prestart = 0) {
  struct timeval start, curr, last;
  int addr = 0x0a800000 + prestart % VIP_NUM;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, prestart);
  
  gettimeofday(&start, NULL);
  last = start;
  
  limit = limit ? limit : STO_NUM;
  for (int i = 0; i < limit; i++) {
    bool failed = false;
    
    // Step 1: read 5-tuple of a packet
    Tuple5 tuple;
    tuple3Gen.gen((Tuple3*) &tuple.src);
    tuple.dst.addr = addr++;
    tuple.dst.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the ConnTable
    // note: handle SYN packets: syn packets should be directly inserted into the connTable to pypass the lookup
    bool hit = false;
    uint8_t version;
    
    hit = bigVirtualConnTable(tuple, version);
    assert(!hit);
    
    // Step 3: if not hit in the ConnTable, insert
    // lookup viptable to get current version and insert the result to the conntable,
    // and resolve any collisions in the process.
    bool findRes = vipTable.Find(tuple.dst, &version);
    assert(findRes);
    
    bool insSucc = insertToConnTable(tuple, version);
    assert(insSucc);
    
#ifndef NDEBUG
    bool found = false;
    for (auto& table : cpConnTables) {
      if (table.Find(tuple, &version)) found = true;
    }
    assert(found);
#endif
    
    assert(getTotalEntires() == i + 1);
  }
  
  gettimeofday(&curr, NULL);
  double diff = diff_us(curr, start) / 1000.0;
  cout << "Cnt: " << limit << ", insert time: " << diff << "ms" << endl;
  addLog << limit << " " << diff << endl;
}

/**
 * Get dip pool info from stdin. format:
 * 1 byte: ip version (server: 6/4),
 * 4/16 byte dip addr start, 2 byte dip port,
 * 4/16 byte vip addr, 2 byte dst port.
 * I just assume dips of a vip are sequential,
 * and are of the same ip version with the vip.
 */
//! cuckoo hash has to allocate memory before operation. No one can predict collisions and I just assume /2
void initDipPool() {
  int dipNum[VIP_NUM];
  for (int i = 0; i < VIP_NUM; ++i)
    dipNum[i] = DIP_NUM_MIN;
  
  for (int i = 0; i < DIP_NUM - VIP_NUM * DIP_NUM_MIN; ++i) {
    int index = rand() % VIP_NUM;
    int num = dipNum[index];
    
    if (num > DIP_NUM_MAX) --i;  // reselect
    else dipNum[index] += 1;
  }
  
  for (int i = 0; i < VIP_NUM; ++i) {
    Addr_Port dip, vip;
    
    getVip(&vip);
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    vector<vector<DIP>>& dips = dipPools[vipInd];
    dips.clear();
    for (uint16_t j = 0; j < dipNum[i]; ++j) {
      DIP dip = { { vip.addr, uint16_t(vip.port + j) }, 1 };
      dips.push_back( { dip });
    }
  }
}

void initControlPlaneAndDataPlane() {
  dpConnTables.clear();
  cpConnTables.clear();
  hashers.clear();
  
  cpConnTables.push_back(ControlPlaneCuckooMap<Tuple5, uint8_t>(CONN_NUM));   // cascade, initially 4 empty table
  dpConnTables.push_back(DataPlaneCuckooMap<Tuple5, uint8_t>(cpConnTables.back()));   // cascade, initially 4 empty table
  cpConnTables.back().SetAssociated(dpConnTables.back());
  hashers.push_back(cpConnTables.back().getDigestFunction());
  
  vipTable.Clear(VIP_NUM);
  dipPoolTable.Clear(VIP_NUM);
  
  for (int i = 0; i < VIP_NUM; ++i) {
    Addr_Port vip;
    getVip(&vip);
    vipTable.Insert(vip, 0);  // no update, no new version
    dipPoolTable.Insert(make_pair(vip, 0), 0);
  }
  initDipPool();
}

void init() {
  commonInit();
  initControlPlaneAndDataPlane();
}

void dynamicThroughput() {
  for (int newConnPerSec = 1024; newConnPerSec <= 256 * 1024 && newConnPerSec <= CONN_NUM; newConnPerSec *= 4) {
    struct timeval start, curr, last;
    
    int addr = 0x0a800000;
    LFSRGen<Tuple3> tuple3Gen(0xe2211, 1024 * 1024 + newConnPerSec, 0);
    
    int stupid = 0;
    
    // Clean control plane and data plane
    initControlPlaneAndDataPlane();
    simulateConnectionAdd(1024 * 1024, 0);
    
    int round = 10;
    gettimeofday(&start, NULL);
    for (int i = 0; i < round * LOG_INTERVAL; ++i) {
      // Step 1: read 5-tuple of a packet
      Tuple5 tuple;
      tuple3Gen.gen((Tuple3*) &tuple.src);
      tuple.dst.addr = addr++;
      tuple.dst.port = 0;
      if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
      
      // we do not simulate SYN, so use the number of iteration to judge.
      bool syn = false;
      
      bool hit = false;
      uint8_t version;
      
      // Step 2: lookup the ConnTable
      // note: handle SYN packets: syn packets should be directly inserted into the connTable to pypass the lookup
      hit = bigVirtualConnTable(tuple, version);
      assert(hit);
      
      if (!hit) {
        // Step 3: if not hit in the ConnTable, insert
        // lookup viptable to get current version and insert the result to the conntable,
        // and resolve any collisions in the process.
        bool findRes = vipTable.Find(tuple.dst, &version);
        assert(findRes);
        
        bool insSucc = insertToConnTable(tuple, version);
        assert(insSucc);
      }
      
#ifndef NDEBUG
      bool found = false;
      for (auto& table : cpConnTables) {
        if (table.Find(tuple, &version)) found = true;
      }
      assert(found);
#endif
      
      // Step 4: lookup the DipPoolTable
      uint8_t dipPoolIndex;
      bool findRes = dipPoolTable.Find(make_pair(tuple.dst, version), &dipPoolIndex);  // must hit
      assert(findRes);
      
      uint16_t vipInd = tuple.dst.addr & VIP_MASK;
      auto &pool = dipPools[vipInd][dipPoolIndex];
      DIP dip = pool[hashers[0](tuple) % (pool.size())];
      
      stupid += dip.addr.addr;   //prevent optimize
    }
    gettimeofday(&last, NULL);
    
    printf("%d\b \b", stupid & 7);
    
    dynamicLog << newConnPerSec << " " << LOG_INTERVAL * 1.0 * round / diff_ms(last, start) / 1000 << endl;
  }
  dynamicLog.close();
}

int main(int argc, char **argv) {
  cout << "--init" << endl;
  init();

  cout << "--simulateConnectionAdd" << endl;
  simulateConnectionAdd();
  printMemoryUsage();

  cout << "--serve" << endl;
  serve();
  
  if (CONN_NUM == 16777216) {
    cout << "--dynamicThroughput" << endl;
    dynamicThroughput();
  }
  return 0;
}
