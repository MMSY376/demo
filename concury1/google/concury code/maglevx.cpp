#include "common.h"
//#include "libcuckoo/cuckoohash_map.hh"
#include "CuckooPresized/control_plane_cuckoo_map.h"
#include "hash.h"

static ControlPlaneCuckooMap<uint64_t, uint16_t, uint8_t, false> *connTrackingTable;    // digest of 5-tuple to version: 16 -> 6
uint16_t **ht = 0;    // [VIPInd][DIPInd] -> DIP Addr_Port
vector<DIP> dipPools[VIP_NUM];  // vipIndex, dipindex -> dip
uint16_t **newHt = 0;
int dipNum[VIP_NUM];

static Hasher32<Tuple5> hasher[2];

void printMemoryUsage() {
  uint64_t size = 0;
  
  int count = 0;
  for (int i = 0; i < VIP_NUM; ++i)
    count += connTrackingTable[i].EntryCount();
  
  cout << "ConnTrackingTable size: " << (size = count * sizeof(pair<uint64_t, uint16_t>))
       << endl;
  size *= 1.1;
  
  cout << "HT table size: " << VIP_NUM * HT_SIZE * sizeof(uint16_t) + DIP_NUM * sizeof(DIP);
  // dipTable
  size += VIP_NUM * HT_SIZE * sizeof(uint16_t) + DIP_NUM * sizeof(DIP);
  
  cout << " Total memory usage: " << size << endl;
  
  memoryLog << CONN_NUM << " " << size << endl;
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
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  struct timeval start, curr, last;
  gettimeofday(&start, NULL);
  last = start;
  uint32_t i = 0, miss = 0, round = 0;
  int stupid = 0;
  
  while (round < 5) {
    // Step 1: read 5-tuple of a packet
    Tuple5 tuple;
    tuple3Gen.gen((Tuple3 *) &tuple.src);
    tuple.dst.addr = addr++;
    tuple.dst.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = tuple.dst.addr & VIP_MASK;
    
    // Step 3: lookup connectionTracking table
    uint16_t htInd;
    uint64_t hash = hasher[0](tuple);
    hash |= uint64_t(hasher[1](tuple)) << 32;
    
    if (connTrackingTable[vipInd].Find(hash, htInd)) {
      // done with right dip
    } else {
      miss++;
      // Step 4: lookup consistent hashing table to get the dip and insert back
      htInd = ht[vipInd][hash & (HT_SIZE - 1)];
      connTrackingTable[vipInd].Insert(hash, htInd);
      uint16_t tmp;
      assert(connTrackingTable[vipInd].Find(hash, tmp) && tmp == htInd);
    }
    
    DIP &dip = dipPools[vipInd][ht[vipInd][htInd]];
    assert(((dip.addr.addr ^ (0x0a000000 + (vipInd << 8))) < dipPools[vipInd].size() &&
            dip.addr.port - tuple.dst.port >= 0 && dip.addr.port - tuple.dst.port < dipPools[vipInd].size()));
    
    stupid += dip.addr.addr;   //prevent optimize
    i++;
    if (i == LOG_INTERVAL) {
      i = 0;
      round++;
      // gettimeofday(&curr, NULL);
      // int diff = diff_ms(curr, start);
      // int mp = round * (LOG_INTERVAL / 1.0E6);
      // cout << mp << "M packets time: " << diff << "ms, Average speed: " << mp * 1000.0 / diff << "Mpps, miss cnt:" << miss << ", " << diff * 1.0 / mp << "nspp" << endl;
      
      // last = curr;
      miss = 0;
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
  gettimeofday(&start, NULL);
  last = start;
  
  limit = limit ? limit : STO_NUM;
  
  const int repeat = 1 + 8192 / STO_NUM;
  for (int j = 0; j < repeat; ++j) {
    for (int i = 0; i < VIP_NUM; ++i)
      connTrackingTable[i].Clear(CONN_NUM / VIP_NUM);
    
    unsigned addr = 0x0a800000 + prestart % VIP_NUM;
    LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, prestart);
    
    for (int i = 0; i < limit; i++) {
      // Step 1: read 5-tuple of a packet
      Tuple5 tuple;
      tuple3Gen.gen((Tuple3 *) &tuple.src);
      tuple.dst.addr = addr++;
      tuple.dst.port = 0;
      if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
      
      // Step 2: lookup the VIPTable to get VIPInd
      uint16_t vipInd = tuple.dst.addr & VIP_MASK;
      
      // Step 3: lookup connectionTracking table
      uint16_t dipInd;
      uint64_t hash = hasher[0](tuple);
      hash |= uint64_t(hasher[1](tuple)) << 32;
      
      // Step 4: lookup consistent hashing table to get the dip and insert back
      dipInd = ht[vipInd][hash & (HT_SIZE - 1)];
      connTrackingTable[vipInd].Insert(hash, dipInd);
      uint16_t tmp;
      assert(connTrackingTable[vipInd].Find(hash, tmp));
      if (tmp != dipInd) Counter::count("Maglevx", "collision");
    }
  }
  
  gettimeofday(&curr, NULL);
  double diff = diff_us(curr, start) / 1000.0;
  addLog << limit << " " << diff / repeat << endl;
}

void checkCollision() {
  int collision = 0;
  
  unsigned addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  for (int i = 0; i < STO_NUM; i++) {
    // Step 1: read 5-tuple of a packet
    Tuple5 tuple;
    tuple3Gen.gen((Tuple3 *) &tuple.src);
    tuple.dst.addr = addr++;
    tuple.dst.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = tuple.dst.addr & VIP_MASK;
    
    // Step 3: lookup connectionTracking table
    uint16_t dipInd;
    uint64_t hash = hasher[0](tuple);
    hash |= uint64_t(hasher[1](tuple)) << 32;
    
    // Step 4: check
    uint16_t tmp;
    if (connTrackingTable[vipInd].Find(hash, tmp)) {
      collision++;
    }
  }
  
  printf("Collision Count: %d out of %d connections", collision, STO_NUM);
}

/**
 * assign ip and weight to the dips, override previous weight
 */
void simulateUpdatePoolData() {
  struct timeval start, end, res;
  gettimeofday(&start, NULL);
  for (uint16_t i = 0; i < VIP_NUM; ++i) {
    Addr_Port vip;
    getVip(&vip);
    
    uint16_t vipInd = vip.addr & VIP_MASK;
    uint16_t dipCount = dipNum[vipInd];
    
    vector<DIP> &dips = dipPools[vipInd];
    dips.clear();
    for (uint16_t j = 0; j < dipCount; ++j) {
      int weight = int(log2(1 + (rand() % 64)));  // 0-49
      DIP dip = {{uint32_t(0x0a000000 + (i << 8) + j), uint16_t(vip.port + j)}, weight};   // 0-49
      dips.push_back(dip);
    }
  }
  
  gettimeofday(&end, NULL);
  timersub(&end, &start, &res);
}

/**
 * update data plane to make the HT consistent with the dip weight,
 * while ensuring PCC (by modifying the mapped decode to make the flows go to the correct dip, after HT entry reassign)
 *
 * assuming dip pools and conn have been properly constructed
 */
void updateDataPlane(bool mute = false) {
  const static int M = HT_SIZE == 4096 ? 4099 : HT_SIZE == 512 ? 521 : 0;  // a prime number // 4093
  const static Hasher32<uint32_t> hash1(0xe2211), hash2(0xe2212);
  
  int diff, migrationSum = 0;
  struct timeval start, curr, end;
  
  uint64_t controlPlaneConstructionTime = 0;
  uint64_t cpDpSynchronizationTime = 0;
  
  for (uint16_t vipInd = 0; vipInd < VIP_NUM; ++vipInd) {
    gettimeofday(&start, NULL);
    memset(newHt[vipInd], -1, sizeof(uint16_t) * HT_SIZE);
    uint16_t dipCount = dipPools[vipInd].size();
    
    // Step1: construct new HT
    // ** sum weight and cal entriesPerWeight
    uint64_t weightSum = 0;
    for (int dipInd = 0; dipInd < dipCount; ++dipInd)
      weightSum += dipPools[vipInd][dipInd].weight;
    
    // ** let dips take entries in turn with weight
    double allocatedWeight = 0;
    int allocatedEntries = 0;
    uint16_t oneHtIndOf[dipPools[vipInd].size()];
    
    for (int dipInd = 0; dipInd < dipCount; ++dipInd) {
      int w = dipPools[vipInd][dipInd].weight;
      int entries = (allocatedWeight + w) * HT_SIZE / weightSum - allocatedEntries;
      allocatedEntries += entries;
      allocatedWeight += w;
      
      uint32_t h1 = hash1(dipInd), h2 = hash2(dipInd);
      int offset = h1 % M;
      int skip = h2 % (M - 1) + 1;
      for (int j = 0; entries; ++j) {
        int htInd = (offset + j * skip) % M;
        if (htInd >= HT_SIZE || newHt[vipInd][htInd] != uint16_t(-1)) {
          continue;
        }
        
        newHt[vipInd][htInd] = dipInd;
        if (entries == 1) oneHtIndOf[dipInd] = htInd;
        --entries;
      }
    }
    assert(allocatedEntries == HT_SIZE);
    
    // Step2: compare old and new ht, remember all changed entries, and migrate connections by traversing
    unordered_map<uint16_t, uint16_t> migration;
    for (uint16_t htIndex = 0; htIndex < HT_SIZE; ++htIndex) {
      uint16_t dipIndex = ht[vipInd][htIndex];
      if (dipIndex != newHt[vipInd][htIndex]) {
        migration.insert(make_pair(htIndex, oneHtIndOf[dipIndex]));
      }
    }
    
    migrationSum += migration.size();
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    controlPlaneConstructionTime += diff;
    start = curr;
    
    // Step3: write back the new ht, and discard migrations
    memcpy(ht[vipInd], newHt[vipInd], HT_SIZE * sizeof(uint16_t));
    
    // assume no collision, do the migration via traverse
    connTrackingTable[vipInd].Compose(migration);
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    cpDpSynchronizationTime += diff;
  }
  
  if (!mute) {
    cout << "ht entries change count: " << migrationSum << ", ratio: " << migrationSum / VIP_NUM / HT_SIZE << endl;
    
    cout << "Control Plane Construction Time: " << controlPlaneConstructionTime / 1000.0 << "ms" << endl;
    cout << "Control Plane -> Data Plane Synchronization Time: " << cpDpSynchronizationTime / 1000.0 << "ms" << endl;
  }
}

/**
 * init the dip pool for all vips.
 * vips are randomly assigned different number of dips
 * then assign ip and weight to the dips
 * finally update the data plane
 */
void initDipPool() {
  for (int i = 0; i < VIP_NUM; ++i)
    dipNum[i] = DIP_NUM_MIN;
  
  for (int i = 0; i < DIP_NUM - VIP_NUM * DIP_NUM_MIN; ++i) {
    int index = rand() % VIP_NUM;
    int num = dipNum[index];
    
    if (num >= DIP_NUM_MAX) --i;  // reselect
    else dipNum[index] += 1;
  }
  
  simulateUpdatePoolData();
  updateDataPlane(true);
}

/**
 * reset ht and hash seed
 * init dip pool and update data plane
 */
void initControlPlaneAndDataPlane() {
  if (ht) {
    for (int i = 0; i < VIP_NUM; ++i) {
      delete[] ht[i];
      delete[] newHt[i];
    }
    
    delete[] ht;
    delete[] newHt;
    delete[] connTrackingTable;
  }
  
  ht = new uint16_t *[VIP_NUM];
  newHt = new uint16_t *[VIP_NUM];
  connTrackingTable = new ControlPlaneCuckooMap<uint64_t, uint16_t, uint8_t, false>[VIP_NUM];
  
  for (int i = 0; i < VIP_NUM; ++i) {
    ht[i] = new uint16_t[HT_SIZE];
    newHt[i] = new uint16_t[HT_SIZE];
    memset(ht[i], 0, sizeof(uint16_t[HT_SIZE]));
    memset(newHt[i], 0, sizeof(uint16_t[HT_SIZE]));
    
    connTrackingTable[i].Clear(CONN_NUM / VIP_NUM);
  }
  
  hasher[0].setSeed(rand());
  hasher[1].setSeed(rand());
  initDipPool();
  updateDataPlane(true);
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
    updateDataPlane(true);
    
    int round = 10;
    gettimeofday(&start, NULL);
    for (int i = 0; i < round * LOG_INTERVAL; ++i) {
      // Step 1: read 5-tuple of a packet
      Tuple5 tuple;
      tuple3Gen.gen((Tuple3 *) &tuple.src);
      tuple.dst.addr = addr++;
      tuple.dst.port = 0;
      if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
      
      // Step 2: lookup the VIPTable to get VIPInd
      uint16_t vipInd = tuple.dst.addr & VIP_MASK;
      
      // Step 3: lookup connectionTracking table
      uint16_t htInd;
      uint64_t hash = hasher[0](tuple);
      hash |= uint64_t(hasher[1](tuple)) << 32;
      
      if (connTrackingTable[vipInd].Find(hash, htInd)) {
        // done with right dip
      } else {
        // Step 4: lookup consistent hashing table to get the dip and insert back
        htInd = ht[vipInd][hash & (HT_SIZE - 1)];
        connTrackingTable[vipInd].Insert(hash, htInd);
        uint16_t tmp;
        assert(connTrackingTable[vipInd].Find(hash, tmp) && tmp == htInd);
      }
      
      DIP &dip = dipPools[vipInd][ht[vipInd][htInd]];
      assert(((dip.addr.addr ^ (0x0a000000 + (vipInd << 8))) < dipPools[vipInd].size() &&
              dip.addr.port - tuple.dst.port >= 0 && dip.addr.port - tuple.dst.port < dipPools[vipInd].size()));
      
      stupid += dip.addr.addr;   //prevent optimize
    }
    gettimeofday(&last, NULL);
    
    printf("%d\b \b", stupid & 7);
    
    dynamicLog << newConnPerSec << " " << LOG_INTERVAL * 1.0 * round / diff_ms(last, start) / 1000 << endl;
  }
  dynamicLog.close();
}

void controlPlaneToDataPlaneUpdate() {
  ofstream updateTimeLog(NAME ".update.data");
  for (int conn = 1024 * 1024; conn <= CONN_NUM; conn *= 2) {
    struct timeval start, curr, last;
    
    // Clean control plane and data plane
    initControlPlaneAndDataPlane();
    simulateConnectionAdd(conn, 0);
    simulateUpdatePoolData();
    
    gettimeofday(&start, NULL);
    updateDataPlane(true);
    gettimeofday(&last, NULL);
    
    updateTimeLog << double(conn) / VIP_NUM << " " << diff_us(last, start) / 1000.0 / VIP_NUM << endl;
  }
  updateTimeLog.close();
}


int main(int argc, char **argv) {
  init();
//  cout << "checkCollision" << endl;
//  checkCollision();
  simulateConnectionAdd();
  printMemoryUsage();
  serve();
  
  // Clean control plane and data plane
  initControlPlaneAndDataPlane();
  
  if (CONN_NUM == 16777216) {
    cout << "--dynamicThroughput" << endl;
    dynamicThroughput();
    
    // Clean control plane and data plane
    initControlPlaneAndDataPlane();
    cout << "--controlPlaneToDataPlaneUpdate" << endl;
    controlPlaneToDataPlaneUpdate();
  }
  return 0;
}
