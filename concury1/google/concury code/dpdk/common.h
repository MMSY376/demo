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
#include <array>
#include <queue>
#include <set>
#include <list>

#include <rte_malloc.h>

extern uint VIP_NUM;                       // must be power of 2
extern uint VIP_MASK;
extern uint CONN_NUM;    // must be multiple of VIP_NUM
extern uint DIP_NUM;
extern uint DIP_NUM_MIN;
extern uint DIP_NUM_MAX;
extern uint LOG_INTERVAL;       // must be multiple of 1E6
extern uint HT_SIZE;                    // must be power of 2
extern uint STO_NUM;                // simulate control plane
//#define VIP_NUM (128)                       // must be power of 2
//
//#define VIP_MASK (VIP_NUM - 1)
//
//#ifndef CONN_NUM
//#define CONN_NUM (128 * 128 * VIP_NUM)    // must be multiple of VIP_NUM
//#endif
//
//
//#ifndef DIP_NUM
//#define DIP_NUM     (32*VIP_NUM)
//#define DIP_NUM_MIN 8
//#define DIP_NUM_MAX 64
//#endif
//
//#define LOG_INTERVAL (50 * 1000000)       // must be multiple of 1E6
//
//#define HT_SIZE (512)                    // must be power of 2
//#define STO_NUM (CONN_NUM)                // simulate control plane

using namespace std;

#pragma pack(push, 1)

struct Addr_Port {  // 6B
  uint32_t addr = 0;
  uint16_t port = 0;
  
  inline bool operator==(const Addr_Port &another) const {
    return std::tie(addr, port) == std::tie(another.addr, another.port);
  }
  
  inline bool operator<(const Addr_Port &another) const {
    return std::tie(addr, port) < std::tie(another.addr, another.port);
  }
  
};

inline ostream &operator<<(ostream &os, const Addr_Port &addr) {
  os << std::hex << addr.addr << ":" << addr.port;
  return os;
}

struct DIP {
  Addr_Port addr;
  int weight;
};

inline ostream &operator<<(ostream &os, DIP const &dip) {
  os << std::hex << dip.addr << ", " << dip.weight;
  return os;
}

struct Tuple5 {  // 13B
  Addr_Port dst, src;
  uint16_t protocol = 6;
  
  inline bool operator==(const Tuple5 &another) const {
    return std::tie(dst, src, protocol) == std::tie(another.dst, another.src, another.protocol);
  }
  
  inline bool operator<(const Tuple5 &another) const {
    return std::tie(dst, src, protocol) < std::tie(another.dst, another.src, another.protocol);
  }
};

struct Tuple3 {  // 8B
  Addr_Port src;
  uint16_t protocol = 6;  // intentionally padded. will be much faster
  
  inline bool operator==(const Tuple3 &another) const {
    return std::tie(src, protocol) == std::tie(another.src, another.protocol);
  }
  
  inline bool operator<(const Tuple3 &another) const {
    return std::tie(src, protocol) < std::tie(another.src, another.protocol);
  }
  
};

inline ostream &operator<<(ostream &os, Tuple3 const &tuple) {
  os << std::hex << tuple.src << ", " << tuple.protocol;
  return os;
}

#pragma pack(pop)

inline int diff_ms(timeval t1, timeval t2) {
  return (((t1.tv_sec - t2.tv_sec) * 1000000) + (t1.tv_usec - t2.tv_usec)) / 1000;
}

inline int diff_us(timeval t1, timeval t2) {
  return ((t1.tv_sec - t2.tv_sec) * 1000000 + (t1.tv_usec - t2.tv_usec));
}

std::string human(uint64_t word);

void doNotOptimize();

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <sched.h>
#include <unistd.h>

int stick_this_thread_to_core(int core_id);

void sync_printf(const char *format, ...);

void commonInit();

int concury_init(int argc, char **argv);

template<typename InType, template<typename U, typename alloc = allocator <U>> class InContainer,
  typename OutType = InType,
  template<typename V, typename alloc = allocator <V>> class OutContainer = InContainer>
OutContainer<OutType> mapf(const InContainer<InType> &input, function<OutType(const InType &)> func) {
  OutContainer<OutType> output;
  output.resize(input.size());
  transform(input.begin(), input.end(), output.begin(), func);
  return output;
}

/// for runtime statistics collection
class Counter {
public:
  unordered_map <string, unordered_map<string, double>> mem;
  static list <Counter> counters;
  
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
    if (!stopped) {
      stop();
    }
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

template<class T>
class MySimpleArray {
public:
  T *m = 0;
  uint capacity;
  
  inline MySimpleArray(uint size = 1) {
    resize(size);
  }
  
  inline ~MySimpleArray() {
    free();
  }
  
  inline void free() const {
    if (m) {
      for (int i = 0; i < capacity; ++i) {
        (m + i)->~T();
      }
      
      rte_free(m);
    }
  }
  
  inline MySimpleArray(MySimpleArray &&other) {
    m = other.m;
    other.m = 0;
  }
  
  inline MySimpleArray(const MySimpleArray &other) {
    free();
    
    capacity = other.capacity;
    m = (T *) rte_zmalloc(NULL, capacity * sizeof(T), 0);
    
    if (m) {
      for (int i = 0; i < capacity; ++i) {
        new(m + i) T(other.m[i]);
      }
    } else { capacity = 0; }
  }
  
  inline MySimpleArray &operator=(MySimpleArray other) {
    swap(m, other.m);
    capacity = other.capacity;
    return *this;
  }
  
  inline T &operator[](uint index) {
    return m[index];
  }
  
  inline const T &operator[](uint index) const {
    return m[index];
  }
  
  inline void resize(uint size) {
    free();
    
    m = (T *) rte_zmalloc(NULL, size * sizeof(T), 0);
    
    if (m) {
      this->capacity = size;
      for (int i = 0; i < capacity; ++i) {
        new(m + i) T();
      }
    } else { capacity = 0; }
  }
  
  inline void resize(uint size, const T &v) {
    resize(size);
    fill(v);
  }
  
  inline void fill(const T &v) {
    for (int i = 0; i < capacity; ++i) m[i] = v;
  }
};

#include "disjointset.h"
#include "hash.h"
#include "lfsr64.h"

inline void getVip(Addr_Port *vip) {
  static uint addr = (211U << 24) + 127;
  static uint16_t port = 0;
  vip->addr = addr++;
  vip->port = port++ & VIP_MASK & 0;
  if (addr >= (211U << 24) + VIP_NUM) addr = 211U << 24;
}

inline void getTuple3(Tuple3 *tuple3) {
//  static LFSRGen <Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
//  tuple3Gen.gen(tuple3);
  static uint addr = 0;
  static uint port = 0;
  
  tuple3->src.addr = addr++;
  tuple3->src.port = port = (port + 1) & 0;
  
  if (addr >= CONN_NUM) addr = 0;
  tuple3->protocol = 6;
}

inline void get(Tuple3 *tuple, Addr_Port *vip) {
  getTuple3(tuple); // this is why we assume CONN_NUM is multiple of VIP_NUM
  getVip(vip);
//  if (tuple->protocol & 1) { tuple->protocol = 17; }
//  else { tuple->protocol = 6; }
}
