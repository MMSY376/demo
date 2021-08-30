#pragma once

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cassert>
#include <cinttypes>
#include <sys/time.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <functional>
#include <algorithm>
#include <iterator>
#include <utility>
#include <random>

#include <queue>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <tuple>
#include <vector>
#include <array>
#include <queue>
#include <set>
#include <list>

#include "disjointset.h"
#include "hash.h"
#include "lfsr64.h"
#include "sha2/sha2.h"
#include "md5/md5.h"
#include "config.h"   // when work with p4

//int VIP_NUM = 128;                       // must be power of 2
//int VIP_MASK = (VIP_NUM - 1);
//int CONN_NUM = (16 * 1024 * VIP_NUM);    // must be multiple of VIP_NUM
//int DIP_NUM = (VIP_NUM * 128);
//int LOG_INTERVAL = (50 * 1000000);       // must be multiple of 1E6
//int HT_SIZE = 4096;                    // must be power of 2
//int STO_NUM = (CONN_NUM);                // simulate control plane

using namespace std;

#pragma pack(push, 1)
struct Addr_Port {  // 6B
  uint32_t addr = 0;
  uint16_t port = 0;

  inline bool operator ==(const Addr_Port& another) const {
    return std::tie(addr, port) == std::tie(another.addr, another.port);
  }
  
  inline bool operator <(const Addr_Port& another) const {
    return std::tie(addr, port) < std::tie(another.addr, another.port);
  }
  
};

inline ostream& operator <<(ostream & os, const Addr_Port& addr) {
  os << std::hex << addr.addr << ":" << addr.port;
  return os;
}

struct DIP {
  Addr_Port addr;
  int weight;
};

struct Tuple5 {  // 13B
  Addr_Port dst, src;
  uint16_t protocol = 6;

  inline bool operator ==(const Tuple5& another) const {
    return std::tie(dst, src, protocol) == std::tie(another.dst, another.src, another.protocol);
  }
  
  inline bool operator <(const Tuple5& another) const {
    return std::tie(dst, src, protocol) < std::tie(another.dst, another.src, another.protocol);
  }
};

struct Tuple3 {  // 8B
  Addr_Port src;
  uint16_t protocol = 6;  // intentionally padded. will be much faster
  
  inline bool operator ==(const Tuple3& another) const {
    return std::tie(src, protocol) == std::tie(another.src, another.protocol);
  }
  
  inline bool operator <(const Tuple3& another) const {
    return std::tie(src, protocol) < std::tie(another.src, another.protocol);
  }
  
};
inline ostream& operator <<(ostream & os, Tuple3 const & tuple) {
  os << std::hex << tuple.src << ", " << tuple.protocol;
  return os;
}
#pragma pack(pop)

template<int coreId = 0>
inline void getVip(Addr_Port *vip) {
  static int addr = 0x0a800000 + coreId * 10;
  vip->addr = addr++;
  vip->port = 0;
  if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
}

template<int coreId = 0>
inline void getTuple3(Tuple3* tuple3) {
  static LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, coreId * 10);
  tuple3Gen.gen(tuple3);
}

template<int coreId = 0>
inline void get(Tuple3* tuple, Addr_Port* vip) {
  getTuple3<coreId>(tuple); // this is why we assume CONN_NUM is multiple of VIP_NUM
  getVip<coreId>(vip);
  if(tuple->protocol&1) tuple->protocol = 17;
  else tuple->protocol = 6;
}

inline int diff_ms(timeval t1, timeval t2) {
  return (((t1.tv_sec - t2.tv_sec) * 1000000) + (t1.tv_usec - t2.tv_usec)) / 1000;
}

inline int diff_us(timeval t1, timeval t2) {
  return ((t1.tv_sec - t2.tv_sec) * 1000000 + (t1.tv_usec - t2.tv_usec));
}

std::string human(uint64_t word);

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <sched.h>
#include <unistd.h>

int stick_this_thread_to_core(int core_id);

void sync_printf(const char *format, ...);

void commonInit();

template<typename InType,
  template<typename U, typename alloc = allocator<U>> class InContainer,
  typename OutType = InType,
  template<typename V, typename alloc = allocator<V>> class OutContainer = InContainer>
OutContainer<OutType> mapf(const InContainer<InType> &input, function<OutType(const InType &)> func) {
  OutContainer<OutType> output;
  output.resize(input.size());
  transform(input.begin(), input.end(), output.begin(), func);
  return output;
}

#ifndef NAME
#define NAME "concury"
#endif

/// for runtime statistics collection
class Counter {
public:
  unordered_map<string, unordered_map<string, double>> mem;
  static list<Counter> counters;
  
  static inline void count(const string &solution, const string &type, double acc = 1) {
    #ifdef PROFILE
    auto it = counters.back().mem.find(solution);
    if (it == counters.back().mem.end()) {
      counters.back().mem.insert(make_pair(solution, unordered_map<string, double>()));
      it = counters.back().mem.find(solution);
    }
    
    unordered_map<string, double> &typeToCount = it->second;
    
    if (typeToCount.find(type) == typeToCount.end())
      typeToCount.insert(make_pair(type, uint64_t(0)));
    
    counters.back().mem[solution][type] += acc;
    #endif
  }
  
  ~Counter() {
    lap();
  }
  
  string pad() const;
  
  void lap() {
    if (mem.empty()) return;
    
    #ifdef PROFILE
//    cout << pad() << "**************** Hit counts: ****************" << endl;
    
    for (auto it = mem.begin(); it != mem.end(); ++it) {
      const string &solution = it->first;
      
      for (auto iit = it->second.begin(); iit != it->second.end(); ++iit) {
        const string type = iit->first;
        cout << pad() << "->" <<  "[" << solution << "] [" << type << "] " << iit->second << endl;
      }
    }
    
//    cout << pad() << "*********************************************" << endl;
    mem.clear();
    #endif
  }
};

class Clocker {
  int level;
  struct timeval start;
  string name;
  bool stopped = false;
  
  int laps = 0;
  int us = 0;

public:
  explicit Clocker(const string &name) : name(name), level(currentLevel++) {
    for (int i = 0; i < level; ++i) cout << "| ";
    cout << "++";
    
    gettimeofday(&start, nullptr);
    cout << " [" << name << "]" << endl;
    
    Counter::counters.push_back({});
  }
  
  void lap() {
    timeval end;
    gettimeofday(&end, nullptr);
    us += diff_us(end, start);
    
    output();
    
    laps++;
  }
  
  void resume() {
    gettimeofday(&start, nullptr);
  }
  
  void stop() {
    Counter::counters.back().lap();
    Counter::counters.pop_back();
    
    lap();
    stopped = true;
    currentLevel--;
  }
  
  ~Clocker() {
    if (!stopped)
      stop();
  }
  
  void output() const {
    for (int i = 0; i < level; ++i) cout << "| ";
    cout << "--";
    cout << " [" << name << "]" << (laps ? "@" + to_string(laps) : "") << ": "
         << us / 1000 << "ms or " << us << "us"
         << endl;
  }
  
  static int currentLevel;
};

extern ofstream queryLog, addLog, memoryLog, dynamicLog;