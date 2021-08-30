#pragma once

#include "control_plane_othello.h"
#include <rte_prefetch.h>

using namespace std;

template<class K, bool allowGateway, uint8_t DL>
class OthelloFilterControlPlane;

/**
 * Describes the data structure *l-Othello*. It classifies keys of *keyType* into *2^L* classes.
 * The array are all stored in an array of uint64_t. There are actually m_a+m_b cells in this array, each of length L.
 * \note Be VERY careful!!!! valueType must be some kind of int with no more than 8 bytes' length
 */
template<class K, class V, uint8_t L = sizeof(V) * 8, uint8_t DL = 0>
class DataPlaneOthello {
  template<class K1, bool allowGateway, uint8_t DL1>
  friend
  class OthelloFilterControlPlane;

public:
  //*******builtin values
  const static int MAX_REHASH = 50; //!< Maximum number of rehash tries before report an error. If this limit is reached, Othello build fails.
  const static int VDL = L + DL;
  static_assert(VDL <= 64, "Value is too long. You should consider another solution to avoid space waste. ");
  const static uint64_t VDEMASK = ~(uint64_t(-1) << VDL);   // lower VDL bits are 1, others are 0
  const static uint64_t DEMASK = ~(uint64_t(-1) << DL);   // lower DL bits are 1, others are 0
  const static uint64_t VMASK = ~(uint64_t(-1) << L);   // lower L bits are 1, others are 0
  const static uint64_t VDMASK = (VDEMASK << 1) & VDEMASK; // [1, VDL) bits are 1
  
  //****************************************
  //*************DATA Plane
  //****************************************
public:
  MySimpleArray<uint64_t> mem{};        // memory space for array A and array B. All elements are stored compactly into consecutive uint64_t
  uint32_t ma = 0;               // number of elements of array A
  uint32_t mb = 0;               // number of elements of array B
//  Hasher64<K> hab;          // hash function Ha
  Hasher32<K> ha, hb, hd;
  
  inline uint32_t multiply_high_u32(uint32_t x, uint32_t y) const {
    return (uint32_t) (((uint64_t) x * (uint64_t) y) >> 32);
  }
  
  inline uint32_t fast_map_to_A(uint32_t x) const {
    // Map x (uniform in 2^64) to the range [0, num_buckets_ -1]
    // using Lemire's alternative to modulo reduction:
    // http://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
    // Instead of x % N, use (x * N) >> 64.
    return multiply_high_u32(x, ma);
  }
  
  inline uint32_t fast_map_to_B(uint32_t x) const {
    return multiply_high_u32(x, mb);
  }

//  /// \param k
//  /// \return ma + the index of k into array B
//  inline uint64_t getIndices(const K &k) const {
//    uint64_t hash = hab(k);
//    return (fast_map_to_A(hash) << 32) | (fast_map_to_B(hash >> 32) + ma);
//  }
  
  /// \param k
  /// \return ma + the index of k into array B
  inline uint32_t getIndexA(const K &k) const {
    return fast_map_to_A(ha(k));
  }
  
  /// \param k
  /// \return ma + the index of k into array B
  inline uint32_t getIndexB(const K &k) const {
    return (fast_map_to_B(hb(k)) + ma);
  }
  
  /// \param index in array A or array B
  /// \return the index-th element. if the index > ma, it is the (index - ma)-th element in array B
  inline uint64_t memGet(uint32_t index) const {
    uint32_t start = index * VDL / 64;
    uint8_t offset = uint8_t(index * VDL % 64);
    
    char left = char(offset + VDL - 64);
    left = char(left < 0 ? 0 : left);
    
    uint64_t mask = ~(uint64_t(-1) << (VDL - left));     // lower VDL-left bits should be 1, and others are 0
    uint64_t result = (mem[start] >> offset) & mask;
    
    if (left > 0) {
      mask = ~(uint64_t(-1) << left);     // lower left bits should be 1, and others are 0
      result |= (mem[start + 1] & mask) << (VDL - left);
    }
    
    return result;
  }
  
  inline void memPreGet(uint32_t index) const {
    uint32_t start = index * VDL / 64;
    rte_prefetch0(&mem[start]);
    rte_prefetch0(&mem[start + 1]);
  }

public:
  /// \param k
  /// \param v the lookup value for k
  /// \return the lookup is successfully passed the digest match, but it does not mean the key is really a member
  inline bool query(const K &k, V &v) const {
    uint32_t ha = getIndexA(k), hb = getIndexB(k);
    uint64_t aa = memGet(ha);
    uint64_t bb = memGet(hb);
    ////printf("%llx   [%x] %x ^ [%x] %x = %x\n", k,ha,aa&LMASK,hb,bb&LMASK,(aa^bb)&LMASK);
    uint64_t vd = aa ^bb;

//    cout << "query: " << ha << " " << hb << " -> " << aa << " " << bb << endl;
    v = vd >> DL;  // extract correct v
    
    if (DL == 0) return true;      // no filter features
    
    if ((aa & 1) == 0 || (bb & 1) == 0) return false;     // with filter features, then the last bit must be 1
    
    if (DL == 1) return true;  // shortcut for one bit digest
    
    uint32_t digest = uint32_t(vd & DEMASK);
    return (digest | 1) == ((hd(k) & DEMASK) | 1);        // ignore the last bit
  }
  
  inline V query(const K &k) const {
    V result;
    bool success = query(k, result);
    if (success) return result;
    
    throw runtime_error("No matched key! ");
  }

public:
  DataPlaneOthello() {}
  
  template<bool maintainDisjointSet, bool randomized>
  explicit DataPlaneOthello(ControlPlaneOthello<K, V, L, DL, true, maintainDisjointSet, randomized> &cpOthello) {
    fullSync(cpOthello);
  }
  
  template<bool maintainDisjointSet, bool randomized>
  void fullSync(ControlPlaneOthello<K, V, L, DL, true, maintainDisjointSet, randomized> &cpOthello) {
    this->ma = cpOthello.ma;
    this->mb = cpOthello.mb;
    this->ha = cpOthello.ha;
    this->hb = cpOthello.hb;
    this->mem = cpOthello.mem;
//    cout << "fullSync: " << mem[0] << endl;
    this->hd = cpOthello.hd;
  }
  
  template<bool maintainDisjointSet, bool randomized>
  void fullSync(ControlPlaneOthello<K, V, L, DL, false, maintainDisjointSet, randomized> &cpOthello) {
    cpOthello.prepareDP();
    
    this->ma = cpOthello.ma;
    this->mb = cpOthello.mb;
    this->ha = cpOthello.ha;
    this->hb = cpOthello.hb;
    this->hd = cpOthello.hd;
    this->mem = cpOthello.mem;
//    cout << "fullSync: " << endl;
//    for (int i = 0; i < mem.size(); ++i) {
//      if (mem[i]) {
//        cout << mem[i] << " ";
//      }
//    }
//    cout << endl;
  }
  
  virtual uint64_t getMemoryCost() const {
    return mem.capacity * sizeof(mem[0]);
  }
};

template<class K, class V, uint8_t L = sizeof(V) * 8, uint8_t DL = 6>
class OthelloWithFilter : public DataPlaneOthello<K, V, L, DL> {
};
