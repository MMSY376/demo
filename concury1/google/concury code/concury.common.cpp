#include "concury.common.h"

// Data plane
vector<DataPlaneOthello<Tuple3, uint16_t, 12, 0>> othelloForQuery(VIP_NUM); // 3-tuple -> DIPInd requires initialization
uint16_t **ht = 0;    // [VIPInd][DIPInd] -> DIP Addr_Port
vector<DIP> dipPools[VIP_NUM];  // vipIndex, dipindex -> dip
int dipNum[VIP_NUM];
// !Data plane

// Control plane
vector<ControlPlaneOthello<Tuple3, uint16_t, 12, 0, false, false, false>> conn(
  VIP_NUM);  // track the connections and their dipIndices
uint16_t **newHt = 0;
// !Control plane

void simulateConnectionAdd(int limit, int prestart) {
  int addr = 0x0a800000 + prestart % VIP_NUM;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, prestart);
  
  struct timeval start, end, res;
  gettimeofday(&start, NULL);

//  ofstream dipDistributionLog(NAME ".dip.distribution.data");
  limit = limit ? limit : STO_NUM;
  for (int i = 0; i < limit; i++) {
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    
    tuple3Gen.gen(&tuple);
    vip.addr = addr++;
    vip.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    // Step 3: lookup corresponding Othello array
    uint16_t htInd;
    conn[vipInd].query(tuple, htInd);
    htInd &= (HT_SIZE - 1);
    
    // Step 4: add to control plane tracking table
    conn[vipInd].insert(make_pair(tuple, htInd));
  }

//  dipDistributionLog.close();
  gettimeofday(&end, NULL);
  double diff = diff_us(end, start) / 1000.0;
  cout << "Control Plane Add " << limit << " connections " << diff << "ms" << endl;
  addLog << limit << " " << diff << endl;
}

void simulateConnectionLeave() {
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  struct timeval start, end, res;
  gettimeofday(&start, NULL);
  
  for (int i = 0; i < STO_NUM / 3; i++) {
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    
    tuple3Gen.gen(&tuple);
    vip.addr = addr++;
    vip.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    // Step 3: delete from control plane tracking table
    if (!conn[vipInd].isMember(tuple)) {   // insert to the dipIndexTable to simulate the control plane
      throw exception();
    } else {
      conn[vipInd].erase(tuple);
      assert(!conn[vipInd].isMember(tuple));
    }
  }
  
  gettimeofday(&end, NULL);
  int diff = diff_ms(end, start);
  cout << "Control Plane Leave " << STO_NUM / 3 << " connections " << diff << "ms" << endl;
}

/**
 * Simulate an update in the weight of all dips
 */
void simulateUpdatePoolData() {
  for (int i = 0; i < VIP_NUM; ++i) {
    Addr_Port dip, vip;
    
    getVip(&vip);
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    vector<DIP> &dips = dipPools[vipInd];
    dips.clear();
    for (uint16_t j = 0; j < dipNum[i]; ++j) {
      DIP dip = {{uint32_t(0x0a000000 + (i << 8) + j), uint16_t(vip.port + j)},
                 (int) log2(1 + (rand() % 64))};   // 0-49
      dips.push_back(dip);
    }
  }
}

/**
 * Simulate cnt of dips down
 */
void simulateDipDown(int cnt) {
  for (uint16_t i = 0; i < VIP_NUM; ++i) {
    Addr_Port vip;
    getVip(&vip);
    
    int *down = new int[cnt];
    int h1 = rand();
    int h2 = rand();
    int M = 127;
    
    int offset = h1 % M;
    int skip = h2 % (M - 1) + 1;
    for (int j = 0; j < cnt; ++j)
      down[j] = (offset + j * skip) % M;
    
    for (int j = 0; j < cnt; ++j) {
      dipPools[i][down[j]].weight = 0;
    }
    
    delete[] down;
  }
}

/**
 * update data plane to make the HT consistent with the dip weight,
 * while ensuring PCC.
 *
 * assuming dip pools and conn have been properly constructed
 */
void updateDataPlane(bool init) {
  const static int M = HT_SIZE == 4096 ? 4099 : HT_SIZE == 512 ? 521 : 0;  // a prime number // 4093
  const static Hasher32<uint32_t> hash1(0xe2211), hash2(0xe2212);
  
  int diff, migrationSum = 0;
  struct timeval start, curr, end;
  
  uint64_t controlPlaneConstructionTime = 0;
  uint64_t cpDpSynchronizationTime = 0;
  uint64_t migrationCalculationTime = 0;
  
  for (uint16_t vipInd = 0; vipInd < VIP_NUM; ++vipInd) {
    gettimeofday(&start, NULL);
    
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
    vector<uint16_t> entries(dipCount);
    
    for (int dipInd = 0; dipInd < dipCount; ++dipInd) {
      int w = dipPools[vipInd][dipInd].weight;
      entries[dipInd] = (allocatedWeight + w) * HT_SIZE / weightSum - allocatedEntries;
      allocatedEntries += entries[dipInd];
      allocatedWeight += w;
    }
    
    int entriesToAllocate = allocatedEntries;
    assert(entriesToAllocate == HT_SIZE);
    vector<uint32_t> tryCount;
    tryCount.resize(dipCount, 0);
    
    memset(newHt[vipInd], -1, sizeof(uint16_t) * HT_SIZE);
    while (entriesToAllocate)
      for (int dipInd = 0; dipInd < dipCount; ++dipInd) {
        if (entries[dipInd]) {
          uint32_t h1 = dipInd, h2 = hash2(dipInd);
          int offset = h1 % M;
          int skip = h2 % (M - 1) + 1;
          
          while (true) {
            tryCount[dipInd]++;
            int htInd = (offset + tryCount[dipInd] * skip) % M;
            if (htInd >= HT_SIZE || newHt[vipInd][htInd] != uint16_t(-1)) {
              continue;
            }
            
            newHt[vipInd][htInd] = dipInd;
            
            if (entries[dipInd] == 1) oneHtIndOf[dipInd] = htInd;
            --entries[dipInd];
            --entriesToAllocate;
            break;
          }
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
    migrationCalculationTime += diff;
    
    // ** now that all upcoming migrations are stored in the map, do the migration
    // *** traverse all the stored connections to check if the result is to be migrated
    conn[vipInd].compose(migration);
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    controlPlaneConstructionTime += diff;
    start = curr;
    
    if (init) configureDataPlane(vipInd);
    updateDataPlaneCallBack(vipInd);
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    cpDpSynchronizationTime += diff;
  }
  
  if (!init) {
    cout << "ht entries change count: " << migrationSum << ", ratio: " << double(migrationSum) / VIP_NUM / HT_SIZE
         << endl;
    
    cout << "Control Plane Construction Time: " << controlPlaneConstructionTime / 1000.0 / VIP_NUM << "ms" << endl;
    cout << "Migration Calculation Time: " << migrationCalculationTime / 1000.0  / VIP_NUM<< "ms" << endl;
    cout << "Control Plane -> Data Plane Synchronization Time: " << cpDpSynchronizationTime / 1000.0 / VIP_NUM << "ms" << endl;
  }
}

/**
 * update data plane to make the HT consistent with the dip weight,
 * while ensuring PCC.
 *
 * assuming dip pools and conn have been properly constructed
 */
void updateDataPlaneStupid(bool init) {
  const static int M = HT_SIZE == 4096 ? 4099 : HT_SIZE == 512 ? 521 : 0;  // a prime number // 4093
  const static Hasher32<uint32_t> hash1(0xe2211), hash2(0xe2212);
  
  int diff, migrationSum = 0;
  struct timeval start, curr, end;
  
  uint64_t controlPlaneConstructionTime = 0;
  uint64_t cpDpSynchronizationTime = 0;
  uint64_t migrationCalculationTime = 0;
  
  for (uint16_t vipInd = 0; vipInd < VIP_NUM; ++vipInd) {
    gettimeofday(&start, NULL);
    
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
    vector<uint16_t> entries(dipCount);
    
    for (int dipInd = 0; dipInd < dipCount; ++dipInd) {
      int w = dipPools[vipInd][dipInd].weight;
      entries[dipInd] = (allocatedWeight + w) * HT_SIZE / weightSum - allocatedEntries;
      allocatedEntries += entries[dipInd];
      allocatedWeight += w;
    }
    
    int entriesToAllocate = allocatedEntries;
    assert(entriesToAllocate == HT_SIZE);
    vector<uint32_t> tryCount;
    tryCount.resize(dipCount, 0);
    
    memset(newHt[vipInd], -1, sizeof(uint16_t) * HT_SIZE);
    while (entriesToAllocate)
      for (int dipInd = 0; dipInd < dipCount; ++dipInd) {
        if (entries[dipInd]) {
          uint32_t h1 = dipInd, h2 = hash2(dipInd);
          int offset = h1 % M;
          int skip = h2 % (M - 1) + 1;
          
          while (true) {
            tryCount[dipInd]++;
            int htInd = (offset + tryCount[dipInd] * skip) % M;
            if (htInd >= HT_SIZE || newHt[vipInd][htInd] != uint16_t(-1)) {
              continue;
            }
            
            newHt[vipInd][htInd] = dipInd;
            
            if (entries[dipInd] == 1) oneHtIndOf[dipInd] = htInd;
            --entries[dipInd];
            --entriesToAllocate;
            break;
          }
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
    migrationCalculationTime += diff;
    
    // ** now that all upcoming migrations are stored in the map, do the migration
    // *** traverse all the stored connections to check if the result is to be migrated
    conn[vipInd].compose(migration);
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    controlPlaneConstructionTime += diff;
  
    // Step3: write back the new ht and new othelloForQuery
    ControlPlaneOthello<Tuple3, uint16_t, 12, 0, true, true, false> tmp(CONN_NUM / VIP_NUM);
    gettimeofday(&curr, NULL);
    memcpy(ht[vipInd], newHt[vipInd], HT_SIZE * sizeof(uint16_t));
    
    const auto &keys = conn[vipInd].getKeys();
    const auto &values = conn[vipInd].getValues();
    const int size = conn[vipInd].size();
    for (int i = 0; i < size; ++i) {
      tmp.insert(make_pair(keys[i], values[i]));
    }
    
    gettimeofday(&curr, NULL);
    diff = diff_us(curr, start);
    cpDpSynchronizationTime += diff;
  }
  
  if (!init) {
    cout << "ht entries change count: " << migrationSum << ", ratio: " << double(migrationSum) / VIP_NUM / HT_SIZE
         << endl;
    
    cout << "[Stupid] Control Plane Construction Time: " << controlPlaneConstructionTime / 1000.0 / VIP_NUM << "ms" << endl;
    cout << "[Stupid] Migration Calculation Time: " << migrationCalculationTime / 1000.0  / VIP_NUM<< "ms" << endl;
    cout << "[Stupid] Control Plane -> Data Plane Synchronization Time: " << cpDpSynchronizationTime / 1000.0 / VIP_NUM << "ms" << endl;
  }
}

void testResillience() {
  for (int downCnt = 1; downCnt < 15; downCnt += 1) {
    cout << "--testResillience at cnt " << downCnt << endl;
    initDipPool();
    simulateDipDown(downCnt);
    updateDataPlane();
  }
}

void initDipPool() {
#ifndef FIX_DIP_NUM
  for (int i = 0; i < VIP_NUM; ++i)
    dipNum[i] = DIP_NUM_MIN;
  
  for (int i = 0; i < DIP_NUM - VIP_NUM * DIP_NUM_MIN; ++i) {
    int index = rand() % VIP_NUM;
    int num = dipNum[index];
    
    if (num > DIP_NUM_MAX) --i;  // reselect
    else dipNum[index] += 1;
  }
#else
  for (int i = 0; i < VIP_NUM; ++i)
    dipNum[i] = DIP_NUM / VIP_NUM;
#endif
  simulateUpdatePoolData();
  updateDataPlane(true);
}
