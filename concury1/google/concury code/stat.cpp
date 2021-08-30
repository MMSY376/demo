#include "othello.h"
#include "othello_set.h"
#include "common.h"
#include "libcuckoo/cuckoohash_map.hh"
// Data plane
Othello<Tuple3, uint16_t, 12> *othello[VIP_NUM];  // 3-tuple -> DIPInd  // requires initialization, 
uint16_t ht[VIP_NUM][HT_SIZE];    // [VIPInd][DIPInd] -> DIP Addr_Port
DIP dipPools[VIP_NUM][DIP_NUM / VIP_NUM];

// Control plane
OthelloSet<Tuple3> conn[VIP_NUM];
// how to track connections?

void printStat() {
  const static char* randNames[] = { "md5", "sha256", "crand" };
  
  int cnt = othello[0]->getStaticCnt();
  
  for (int round = 0; round < 3; round++) {
    ostringstream name;
    name << "bin/stat." << randNames[round] << "." << cnt << ".txt";
    ofstream fout(name.str());
    
    for (int i = 0; i < cnt; ++i) {
      if (round == 0) {
        fout << (MD5(i).toInt() & (HT_SIZE - 1)) << endl;
      } else if (round == 1) {
        uint64_t const x[] = { (uint64_t) i };
        auto res = sha2::sha512(sha2::bit_sequence(x));
        fout << (res[0] & (HT_SIZE - 1)) << endl;
      } else {
        fout << (rand() & (HT_SIZE - 1)) << endl;
      }
    }
    fout.close();
  }
  
  for (uint16_t i = 0; i < 1; ++i) {
    ostringstream name;
    name << "bin/stat." << CONN_NUM << "." << VIP_NUM << "." << STO_NUM << "." << i << ".txt";
    
    othello[i]->outputMappedValue(name.str());
    
    cout << i << "done" << endl;
  }
}

void simulateConnectionAdd() {
  int i = 0;
  bool found = false;
  
  while (1) {
    if (i >= STO_NUM) break;
    
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    get(&tuple, &vip);
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    // Step 3: lookup corresponding Othello array
    uint16_t dipInd = othello[vipInd]->query(tuple);
    dipInd &= (HT_SIZE - 1);
    
    volatile DIP* dip = &dipPools[vipInd][ht[vipInd][dipInd]];
    
    assert(dip->addr.addr == vip.addr && dip->addr.port - vip.port >= 0 && dip->addr.port - vip.port < DIP_NUM / VIP_NUM);
    
    if (!conn[vipInd].isMember(tuple)) {   // insert to the dipIndexTable to simulate the control plane
      // TODO remove when control plane exits!
      othello[vipInd]->insert(tuple, dipInd);
      conn[vipInd].insert(tuple);
    }
    
    assert((othello[vipInd]->query(tuple) & (HT_SIZE - 1)) == dipInd);
    
    i++;
  }
}

/**
 * Update dip pool info from generated format:
 * 1 byte: ip version (server: 6/4),
 * 4 byte dip addr start, 2 byte dip port,
 * 4 byte vip addr, 2 byte dst port.
 * 
 * I just assume dips of a vip are sequential,
 * and are of the same ip version with the vip.
 */
//! cuckoo hash has to allocate memory before operation. No one can predict collisions and I just assume /2
void updateDataPlane() {
  struct timeval start, end, res;
  gettimeofday(&start, NULL);
  
  for (uint16_t i = 0; i < VIP_NUM; ++i) {
    ostringstream name;
    name << "bin/dip." << i << ".txt";
    ofstream fout(name.str());
    
    Addr_Port vip;
    getVip(&vip);
    
    uint64_t weightSum[DIP_NUM / VIP_NUM + 1];
    
    for (int j = 0; j < DIP_NUM / VIP_NUM; ++j) {
      dipPools[i][j].addr.addr = vip.addr;
      dipPools[i][j].addr.port = j;
      double r = (double) rand() / RAND_MAX;
      
      int weight = r * 50;
      
      weightSum[j + 1] = weightSum[j] + weight;
      
      dipPools[i][j].weight = weight;   // 0-49
          
      fout << j << " " << weight << endl;
    }
    
    int curr = 0;
    for (int k = 0; k < HT_SIZE; ++k) {
      while (double(weightSum[curr + 1]) / weightSum[DIP_NUM / VIP_NUM] <= double(k) / HT_SIZE)
        curr++;
      ht[i][k] = curr;
    }
    
    fout.close();
  }
  
  gettimeofday(&end, NULL);
  timersub(&end, &start, &res);
  cout << "Construction Time: " << res.tv_usec << endl;
}

void init() {
  for (int i = 0; i < VIP_NUM; ++i) {
    othello[i] = new Othello<Tuple3, uint16_t, 12>(0, 0, 0);
  }
  
  commonInit();
  
  updateDataPlane();
  simulateConnectionAdd();
}

int main(int argc, char **argv) {
  init();
  printStat();
  return 0;
}
