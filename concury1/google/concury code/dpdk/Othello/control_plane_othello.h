#pragma once

#include "../common.h"

using namespace std;

template<class K, class V, uint8_t L, uint8_t DL>
class DataPlaneOthello;

template<class K, bool allowGateway, uint8_t DL>
class OthelloFilterControlPlane;

/**
 * Control plane Othello can track connections (Add [amortized], Delete, Membership Judgment) in O(1) time,
 * and can iterate on the keys in exactly n elements.
 *
 * Implementation: just add an array indMem to be maintained. always ensure that registered keys can
 * be queried to get the index of it in the keys array
 *
 * How to ensure:
 * add to tail when add, and store the value as well as the index to othello
 * when delete, move key-value and update corresponding index
 *
 * @note
 *  The valueType must be compatible with all int operations
 *
 *  If you wish to export the control plane to a data plane query structure at a fast speed and at any time, then
 *  set willExport to true. Additional computation and memory overheads will apply on insert, while lookups will be faster.
 *
 *  If you wish to maintain the disjoint set, the insertion will become faster but the deletion is slower, in the sense that
 *  memory accesses are more expensive than computation
 */
template<class K, class V, uint8_t L = sizeof(V) * 8, uint8_t DL = 0,
  bool maintainDP = false, bool maintainDisjointSet = true, bool randomized = false>
class ControlPlaneOthello {
  template<class K1, class V1, uint8_t L1, uint8_t DL1> friend
  class DataPlaneOthello;
  
  template<class K1, bool allowGateway, uint8_t DL1> friend
  class OthelloFilterControlPlane;

public:
  //*******builtin values
  const static int MAX_REHASH = 50; //!< Maximum number of rehash tries before report an error. If this limit is reached, Othello build fails.
  const static int VDL = L + DL;
  static_assert(VDL <= 64, "Value is too long. You should consider another solution to avoid space waste. ");
  static_assert(L <= sizeof(V) * 8, "Value is too long. ");
  const static uint64_t VDEMASK = ~(uint64_t(-1) << VDL);   // lower VDL bits are 1, others are 0
  const static uint64_t DEMASK = ~(uint64_t(-1) << DL);   // lower DL bits are 1, others are 0
  const static uint64_t VMASK = ~(uint64_t(-1) << L);   // lower L bits are 1, others are 0
  const static uint64_t VDMASK = (VDEMASK << 1) & VDEMASK; // [1, VDL) bits are 1
  //****************************************
  //*************DATA Plane
  //****************************************
private:
  MySimpleArray<uint64_t> mem{};        // memory space for array A and array B. All elements are stored compactly into consecutive uint64_t
  uint32_t ma = 0;               // number of elements of array A
  uint32_t mb = 0;               // number of elements of array B
//  Hasher64<K> hab = Hasher64<K>((uint64_t(rand()) << 32) + rand());          // hash function Ha
  Hasher32<K> ha = Hasher32<K>(rand());          // hash function Ha
  Hasher32<K> hb = Hasher32<K>(rand());          // hash function Ha
  Hasher32<K> hd = Hasher32<K>(uint32_t(rand()));
  
  bool maintainingDP = maintainDP;
  
  void setSeed(int seed) {
    seed = (seed != -1) ? seed : rand();
    hd.setSeed(seed);
  }
  
  void changeSeed() { setSeed(-1); }
  
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
  
  /// \return the number of uint64_t elements to hold ma + mb valueType elements
  inline void memResize() {
    if (!maintainingDP) return;
    mem.resize(((ma + mb) * VDL + 63) / 64);
  }
  
  /// Set the index-th element to be value. if the index > ma, it is the (index - ma)-th element in array B
  /// \param index in array A or array B
  /// \param value
  inline void memSet(uint32_t index, uint64_t value) {
    if (VDL == 0) return;
    
    uint64_t v = uint64_t(value) & VDEMASK;
    
    uint32_t start = index * VDL / 64;
    uint8_t offset = uint8_t(index * VDL % 64);
    char left = char(offset + VDL - 64);
    
    uint64_t mask = ~(VDEMASK << offset); // [offset, offset + VDL) should be 0, and others are 1
    
    mem[start] &= mask;
    mem[start] |= v << offset;
    
    if (left > 0) {
      mask = uint64_t(-1) << left;     // lower left bits should be 0, and others are 1
      mem[start + 1] &= mask;
      mem[start + 1] |= v >> (VDL - left);
    }
  }
  
  /// \param index in array A or array B
  /// \return the index-th element. if the index > ma, it is the (index - ma)-th element in array B
  template<bool onlyValue = false>
  inline uint64_t memGet(uint32_t index) const {
    if (VDL == 0) return 0;
    
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
  
  inline void memValueSet(uint32_t index, uint64_t value) {
    if (L == 0) return;
    
    uint64_t v = uint64_t(value) & VMASK;
    
    uint32_t start = (index * VDL + DL) / 64;
    uint8_t offset = uint8_t((index * VDL + DL) % 64);
    char left = char(offset + L - 64);
    
    uint64_t mask = ~(VMASK << offset); // [offset, offset + L) should be 0, and others are 1
    
    mem[start] &= mask;
    mem[start] |= v << offset;
    
    if (left > 0) {
      mask = uint64_t(-1) << left;     // lower left bits should be 0, and others are 1
      mem[start + 1] &= mask;
      mem[start + 1] |= v >> (L - left);
    }
  }
  
  inline uint64_t memValueGet(uint32_t index) const {
    if (L == 0) return 0;
    
    uint32_t start = (index * VDL + DL) / 64;
    uint8_t offset = uint8_t((index * VDL + DL) % 64);
    char left = char(offset + L - 64);
    left = char(left < 0 ? 0 : left);
    
    uint64_t mask = ~(uint64_t(-1) << (L - left));     // lower L-left bits should be 1, and others are 0
    uint64_t result = (mem[start] >> offset) & mask;
    
    if (left > 0) {
      mask = ~(uint64_t(-1) << left);     // lower left bits should be 1, and others are 0
      result |= (mem[start + 1] & mask) << (L - left);
    }
    
    return result;
  }

public:
  /// \param k
  /// \param v the lookup value for k
  /// \return the lookup action is successful, but it does not mean the key is really a member
  /// \note No membership is checked. Use isMember to check the membership
  inline bool query(const K &k, V &out) const {
    if (maintainDP) {
//      uint64_t hash = getIndices(k);
//      uint32_t ha = hash, hb = hash >> 32;
      uint32_t ha = getIndexA(k), hb = getIndexB(k);
      V aa = memGet(ha);
      V bb = memGet(hb);
      ////printf("%llx   [%x] %x ^ [%x] %x = %x\n", k,ha,aa&LMASK,hb,bb&LMASK,(aa^bb)&LMASK);
      uint64_t vd = aa ^bb;
      out = vd >> DL;
    } else {
      uint32_t index = queryIndex(k);
      if (index >= values.capacity) return false;// throw runtime_error("Index out of bound. Maybe not a member");
      out = values[index];
    }
    return true;
  }

public:
  explicit ControlPlaneOthello(uint32_t keyCapacity = 256) {
    for (minimalKeyCapacity = 256; minimalKeyCapacity < keyCapacity; minimalKeyCapacity <<= 1);
    
    resizeKey(0);
    
    resetBuildState();
    
    build();
  }
  
  /// Resize key and value related memory for the Othello to be able to hold keyCount keys
  /// \param keyCount the target capacity
  /// \note Side effect: will change keyCnt, and if hash size is changed, a rebuild is performed
  void resizeKey(uint32_t keyCount, bool compact = false) {
    keyCount = max(keyCount, minimalKeyCapacity);
    
    if (keyCount < this->size()) {
      throw runtime_error("The specified capacity is less than current key size! ");
    }
    
    uint32_t nextMb;
    
    if (compact) {
      nextMb = max(minimalKeyCapacity, keyCount);
    } else {
      nextMb = minimalKeyCapacity;
      while (nextMb < keyCount)
        nextMb <<= 1;
    }
    uint32_t nextMa = static_cast<uint32_t>(1.33334 * nextMb);
    
    if (keyCount > keys.capacity) {
      uint32_t keyCntReserve = max(256U, keyCount * 2U);
      keys.resize(keyCntReserve);
      values.resize(keyCntReserve);
      nextAtA.resize(keyCntReserve);
      nextAtB.resize(keyCntReserve);
    }
    
    if (nextMa > ma || nextMa < 0.8 * ma) {
      ma = nextMa;
      mb = nextMb;
      
      memResize();
      
      indMem.resize(ma + mb);
      head.resize(ma + mb);
      connectivityForest.resize(ma + mb);
      
      build();
    }
//    cout << human(keyCnt) << " Keys, ma/mb = " << human(ma) << "/" << human(mb) << endl;
  }
  
  //****************************************
  //*************CONTROL plane
  //****************************************
private:
  uint32_t keyCnt = 0, minimalKeyCapacity = 0;
public:
  void setMinimalKeyCapacity(uint32_t minimalKeyCapacity) {
    this->minimalKeyCapacity = minimalKeyCapacity;
    resizeKey(0);
  }
  
  void compose(const unordered_map<V, V> &migration) {
    for (int i = 0; i < size(); ++i) {
      uint16_t &val = values[i];
      
      auto it = migration.find(val);
      if (it != migration.end()) {
        uint16_t dst = it->second;
        if (dst == (uint16_t) -1) {
          eraseAt(i);
          --i;
        } else {
          val = dst;
        }
      }
    }
    
    if (maintainingDP) {
      fillValue<true>();
    }
  }
  
  void prepareDP() {
    if (maintainDP) return;
    maintainingDP = true;
    
    memResize();
    fillValue();
    
    maintainingDP = false;
  }

private:
  // ******input of control plane
  MySimpleArray<K> keys{};
  MySimpleArray<V> values{};
  MySimpleArray<uint32_t> indMem{};       // memory space for indices
  
  inline V randVal(int i = 0) const {
    V v = rand();
    
    if (sizeof(V) > 4) {
      *(((int *) &v) + 1) = rand();
    }
    return v;
  }
  
  /// Forget all previous build states and get prepared for a new build
  void resetBuildState() {
//    cout << "reset. m size: " << ma + mb << endl;
    for (uint32_t i = 0; i < ma + mb; ++i) {
      if (maintainingDP) memSet(i, randomized ? (randVal(i) & VDMASK) : 0);
    }
    head.fill(-1);
    nextAtA.fill(-1);
    nextAtB.fill(-1);
    
    connectivityForest.reset();
  }
  
  uint32_t tryCount = 0; //!< number of rehash before a valid hash pair is found.
  /*! multiple keys may share a same end (hash value)
   first and next1, next2 maintain linked lists,
   each containing all keys with the same hash in either of their ends
   */
  MySimpleArray<int32_t> head{};         //!< subscript: hashValue, value: keyIndex
  MySimpleArray<int32_t> nextAtA{};         //!< subscript: keyIndex, value: keyIndex
  MySimpleArray<int32_t> nextAtB{};         //! h2(keys[i]) = h2(keys[next2[i]]);
  
  DisjointSet connectivityForest;                     //!< store the hash values that are connected by key edges
  
  /// gen new hash seed pair, cnt ++
  inline void newHash() {
//    hab.setSeed((uint64_t(rand()) << 32) | rand());
    ha.setSeed(rand());
    hb.setSeed(rand());
    tryCount++;
    if (tryCount > 1) {
      //printf("NewHash for the %d time\n", tryCount);
    }
  }
  
  /// update the disjoint set and the connected forest so that
  /// include all the old keys and the newly inserted key
  /// \note this method won't change the node value
  inline void addEdge(int key, uint32_t ha, uint32_t hb) {
    nextAtA[key] = head[ha];
    head[ha] = key;
    nextAtB[key] = head[hb];
    head[hb] = key;
    connectivityForest.merge(ha, hb);
  }
  
  /// test if this hash pair is acyclic, and build:
  /// the connected forest and the disjoint set of connected relation
  /// the disjoint set will be only useful to determine the root of a connected component
  ///
  /// Assume: all build related memory are cleared before
  /// Side effect: the disjoint set and the connected forest are changed
  bool testHash() {
    // cout << "********\ntesting hash" << endl;
    for (int i = 0; i < keyCnt; i++) {
      const K &k = keys[i];
//      uint64_t hash = getIndices(k);
//      uint32_t ha = hash, hb = hash >> 32;
      
      uint32_t ha = getIndexA(k);
      uint32_t hb = getIndexB(k);
      
      // cout << i << "th key: " << keys[i] << ", ha: " << ha << ", hb: " << hb << endl;
      
      // two indices are in the same disjoint set, which means the current key will incur a circle.
      if (connectivityForest.sameSet(ha, hb)) {
        //printf("Conflict key %d: %llx\n", i, *(unsigned long long*) &(keys[i]));
        return false;
      }
      addEdge(i, ha, hb);
    }
    return true;
  }
  
  /// Fill the values of a connected tree starting at the root node and avoid searching keyId
  /// Assume:
  /// 1. the value of root is not properly set before the function call
  /// 2. the values are in the value array
  /// 3. the root is always from array A
  /// Side effect: all node in this tree is set and if updateToFilled
  template<bool fillValue, bool fillIndex, bool keepDigest = false>
  void fillTreeDFS(uint32_t root) {
    assert(root < ma);
    
    stack<pair<uint32_t, uint32_t>> stack;  // previous key id, this node
    stack.push(make_pair(uint32_t(-1), root));
    
    while (!stack.empty()) {
      Counter::count("Othello", "fillTreeDFS step");
      uint32_t prev = stack.top().first;
      uint32_t nid = stack.top().second;
      stack.pop();
      
      bool isAtoB = nid < ma;
      
      // // find all the opposite side node to be filled
      // search all the edges of this node, to fill and enqueue the opposite side, and record the fill
      MySimpleArray<int32_t> &nextKeyOfThisKey = isAtoB ? nextAtA : nextAtB;
      
      for (int keyId = head[nid]; keyId >= 0; keyId = nextKeyOfThisKey[keyId]) {
        // now the opposite side node needs to be filled
        // fill and enqueue all next element of it
        if (keyId == prev) continue;
        
        const K &k = keys[keyId];
//        uint64_t hash = getIndices(k);
//        uint32_t ha = hash, hb = hash >> 32;
        uint32_t ha = getIndexA(k);
        uint32_t hb = getIndexB(k);
        uint32_t nextNode = isAtoB ? hb : ha;
        
        fillSingle<fillValue, fillIndex, keepDigest>(keyId, nextNode, nid);
        
        stack.push(make_pair(uint32_t(keyId), nextNode));
      }
    }
  }
  
  template<bool fillValue, bool fillIndex, bool keepDigest = false>
  inline void fillSingle(uint32_t keyId, uint32_t nodeToFill, uint32_t oppositeNode) {
    if (fillValue && maintainingDP) {
      uint64_t valueToFill;
      if (keepDigest) {
        uint64_t v = values[keyId];
        valueToFill = v ^ memValueGet(oppositeNode);
        memValueSet(nodeToFill, valueToFill);
      } else {
        if (DL) {
          uint64_t digest = hd(keys[keyId]) & DEMASK;
          uint64_t vd = (values[keyId] << DL) | digest;
          valueToFill = (vd ^ memGet(oppositeNode)) | 1ULL;
        } else {
          uint64_t v = values[keyId];
          valueToFill = v ^ memGet(oppositeNode);
        }
        
        memSet(nodeToFill, valueToFill);
      }
    }
    
    if (fillIndex) {
      uint32_t indexToFill = keyId ^indMem[oppositeNode];
      indMem[nodeToFill] = indexToFill;
    }
  }
  
  template<bool fillValue, bool fillIndex, bool keepDigest = false>
  /// fix the value and index at single node by xoring x
  /// \param x the xor'ed number
  inline void fixSingle(uint32_t nodeToFix, uint64_t x, uint32_t ix) {
    if (fillValue && maintainDP) {
      uint64_t valueToFill = x ^memValueGet(nodeToFix);
      memValueSet(nodeToFix, valueToFill);
    }
    
    if (fillIndex) {
      uint32_t indexToFill = ix ^indMem[nodeToFix];
      indMem[nodeToFix] = indexToFill;
    }
  }
  
  /// Fix the values of a connected tree starting at the root node and avoid searching keyId
  /// Assume:
  /// 1. the value of root is not properly set before the function call
  /// 2. the values are in the value array
  /// 3. the root is always from array A
  /// Side effect: all node in this tree is set and if updateToFilled
  template<bool fillValue, bool fillIndex, bool keepDigest = false>
  void fixHalfTreeDFS(uint32_t keyId, uint32_t root, uint32_t hb) {
    assert(root < ma && keyId != uint32_t(-1));
    
    uint64_t x = fillValue ? (keepDigest ? memValueGet(root) : memGet(root)) : 0;
    uint32_t ix = fillIndex ? indMem[root] : 0;
    
    fillSingle<fillValue, fillIndex, keepDigest>(keyId, root, hb);
    
    x = fillValue ? (x ^ (keepDigest ? memValueGet(root) : memGet(root))) : 0;
    ix = fillIndex ? ix ^ indMem[root] : 0;
    
    stack<pair<uint32_t, uint32_t>> stack;  // previous key id, this node
    stack.push(make_pair(keyId, root));
    
    while (!stack.empty()) {
      Counter::count("Othello", "fixHalfTreeDFS step");
      uint32_t prev = stack.top().first;
      uint32_t nid = stack.top().second;
      stack.pop();
      
      bool isAtoB = nid < ma;
      
      // // find all the opposite side node to be filled
      // search all the edges of this node, to fill and enqueue the opposite side, and record the fill
      MySimpleArray<int32_t> &nextKeyOfThisKey = isAtoB ? nextAtA : nextAtB;
      
      for (int keyId = head[nid]; keyId >= 0; keyId = nextKeyOfThisKey[keyId]) {
        // now the opposite side node needs to be filled
        // fill and enqueue all next element of it
        if ((uint) keyId == prev) continue;
        
        const K &k = keys[keyId];
//        uint64_t hash = getIndices(k);
//        uint32_t ha = hash, hb = hash >> 32;
        uint32_t ha = getIndexA(k);
        uint32_t hb = getIndexB(k);
        uint32_t nextNode = isAtoB ? hb : ha;
        
        fixSingle<fillValue, fillIndex, keepDigest>(nextNode, x, ix);
        
        stack.push(make_pair(uint32_t(keyId), nextNode));
      }
    }
  }
  
  /// test the two nodes are connected or not
  /// Assume the Othello is properly built
  /// \note cannot use disjoint set if because disjoint set cannot maintain valid after key deletion. So a traverse is performed
  /// \param ha0
  /// \param hb0
  /// \return true if connected
  bool isConnectedDFS(uint32_t ha0, uint32_t hb0) {
    if (maintainDisjointSet) return connectivityForest.representative(ha0) == connectivityForest.representative(hb0);
    
    if (ha0 == hb0) return true;
    
    stack<pair<uint32_t, uint32_t>> stack;  // previous key id, this node
    stack.push(make_pair(uint32_t(-1), ha0));
    
    while (!stack.empty()) {
      uint32_t prev = stack.top().first;
      uint32_t nid = stack.top().second;
      stack.pop();
      
      bool isAtoB = nid < ma;
      const MySimpleArray<int32_t> &nextKeyOfThisKey = isAtoB ? nextAtA : nextAtB;
      
      for (int keyId = head[nid]; keyId >= 0; keyId = nextKeyOfThisKey[keyId]) {
        if (keyId == prev) continue;
        
        const K &k = keys[keyId];
//        uint64_t hash = getIndices(k);
//        uint32_t ha = hash, hb = hash >> 32;
        uint32_t ha = getIndexA(k);
        uint32_t hb = getIndexB(k);
        uint32_t nextNode = isAtoB ? hb : ha;
        
        if (nextNode == hb0) {
          return true;
        }
        
        stack.push(make_pair(uint32_t(keyId), nextNode));
      }
    }
    return false;
  }
  
  
  /// Ensure the disjoint set is properly maintained after the construction.
  /// the workflow is: mark the representatives of all connected nodes as root
  /// \param node
  void connectBFS(uint32_t root) {
    stack<pair<uint32_t, uint32_t>> stack;  // previous key id, this node
    stack.push(make_pair(uint32_t(-1), root));
    connectivityForest.__set(root, root);
    
    if (head[root] < 0 && maintainingDP) {
      memSet(root, randomized ? (randVal(root) & VDMASK) : 0);
      return;
    }
    
    while (!stack.empty()) {
      uint32_t prev = stack.top().first;
      uint32_t nid = stack.top().second;
      stack.pop();
      
      bool isAtoB = nid < ma;
      const MySimpleArray<int32_t> &nextKeyOfThisKey = isAtoB ? nextAtA : nextAtB;
      
      for (int keyId = head[nid]; keyId >= 0; keyId = nextKeyOfThisKey[keyId]) {
        if (keyId == prev) continue;
        
        const K &k = keys[keyId];
//        uint64_t hash = getIndices(k);
//        uint32_t ha = hash, hb = hash >> 32;
        uint32_t ha = getIndexA(k);
        uint32_t hb = getIndexB(k);
        uint32_t nextNode = isAtoB ? hb : ha;
        
        connectivityForest.__set(nextNode, root);
        
        stack.push(make_pair(uint32_t(keyId), nextNode));
      }
    }
  }
  
  /// Fill *Othello* so that the query returns values as defined
  ///
  /// Assume: edges and disjoint set are properly set up.
  /// Side effect: all values are properly set
  template<bool keepDigest = false>
  void fillValue() {
    for (uint32_t i = 0; i < ma + mb; i++) {
      if (connectivityForest.isRoot(i)) {  // we can only fix one end's value in a cc of keys, then fix the roots'
        if ((DL || randomized) && maintainingDP) {
          memSet(i, randomized ? randVal() | 1 : 1);
        }
        
        fillTreeDFS<true, true, keepDigest>(i);
      }
    }
  }
  
  inline void fillOnlyValue() {
    fillValue<true>();
  }
  
  /// Begin a new build
  /// Side effect: 1) discard all memory except keys and values. 2) build fail, or
  /// all the values and disjoint set are properly set
  bool tryBuild() {
    resetBuildState();
    
    if (keyCnt == 0) {
      return true;
    }
    
    #ifndef NDEBUG
    Clocker rebuild("rebuild");
    #else
    cout << "rebuild" << endl;
    #endif
    bool succ;
    if ((succ = testHash())) {
      fillValue<false>();
    }
    
    return succ;
  }
  
  /// try really hard to build, until success or tryCount >= MAX_REHASH
  ///
  /// Side effect: 1) discard all memory except keys and values. 2) build fail, or
  /// all the values and disjoint set are properly set
  bool build() {
    tryCount = 0;
    
    bool built = false;
    do {
      newHash();
      if (tryCount > 20 && !(tryCount & (tryCount - 1))) {
        cout << "Another try: " << tryCount << " " << human(keyCnt) << " Keys, ma/mb = " << human(ma) << "/"
             << human(mb)    //
             << " keyT" << sizeof(K) * 8 << "b  valueT" << sizeof(V) * 8 << "b"     //
             << " Lvd=" << (int) VDL << endl;
      }
      built = tryBuild();
    } while ((!built) && (tryCount < MAX_REHASH));
    
    //printf("%08x %08x\n", Ha.s, Hb.s);
    if (built) {
      if (tryCount > 20) {
        cout << "Succ " << human(keyCnt) << " Keys, ma/mb = " << human(ma) << "/" << human(mb)    //
             << " keyT" << sizeof(K) * 8 << "b  valueT" << sizeof(V) * 8 << "b"     //
             << " Lvd=" << (int) VDL << " After " << tryCount << "tries" << endl;
      }
    } else {
      cout << "rebuild fail! " << endl;
      throw exception();
    }
    
    #ifdef FULL_DEBUG
    assert(checkIntegrity());
    #endif
    
    return built;
  }

public:
  /// \param k
  /// \return the index of k in the array of keys
  inline uint32_t queryIndex(const K &k) const {
//    uint64_t hash = getIndices(k);
//    uint32_t ha = hash, hb = hash >> 32;
    uint32_t ha = getIndexA(k);
    uint32_t hb = getIndexB(k);
    uint32_t aa = indMem[ha];
    uint32_t bb = indMem[hb];
    return aa ^ bb;
  }
  
  /// Insert a key-value pair
  /// \param kv
  /// \return succeeded or not
  inline bool insert(pair<K, V> &&kv) {
    assert(!isMember(kv.first));
    int lastIndex = keyCnt;
    
    if (keyCnt >= keys.capacity) {
      resizeKey(keyCnt + 1);
    }
    keyCnt++;
    
    this->keys[lastIndex] = kv.first;
    this->values[lastIndex] = kv.second;

//    uint64_t hash = getIndices(kv.first);
//    uint32_t ha = hash, hb = hash >> 32;
    uint32_t ha = getIndexA(kv.first);
    uint32_t hb = getIndexB(kv.first);
    
    if (isConnectedDFS(ha, hb)) {
      #ifndef NDEBUG
      Clocker rebuild("Othello cyclic add");
      #endif
      if (!build()) {
        keyCnt -= 1;
        throw exception();
      }
    } else {  // acyclic, just add
      addEdge(lastIndex, ha, hb);
      fixHalfTreeDFS<maintainDP, true>(keyCnt - 1, ha, hb);
    }
    
    #ifdef FULL_DEBUG
    assert(checkIntegrity());
    #endif
    return true;
  }
  
  /// remove one key with the particular index keyId.
  /// \param uint32_t keyId.
  /// \note after this option, the number of keys, keyCnt decrease by 1.
  /// The key currently stored in keys[keyId] will be replaced by the last key of keys.
  /// \note remember to adjust the values array if necessary.
  inline void eraseAt(uint32_t keyId) {
    if (keyId >= keyCnt) throw exception();
    const K &k = keys[keyId];
    erase(k, keyId);
  }
  
  inline void updateMapping(const K &k, V &val) {
    updateValueAt(queryIndex(k), val);
  }
  
  inline void updateMapping(const K &&k, V &&val) {
    updateValueAt(queryIndex(k), val);
  }
  
  inline void updateValueAt(uint32_t keyId, V val) {
    if (keyId >= keyCnt) throw exception();
    
    values[keyId] = val;
    
    if (maintainDP) {
      const K &k = keys[keyId];
      uint32_t ha = getIndexA(k);
      uint32_t hb = getIndexB(k);
      fixHalfTreeDFS<true, false, true>(keyId, ha, hb);
    }
  }
  
  //****************************************
  //*********AS A SET
  //****************************************
public:
  inline const MySimpleArray<K> &getKeys() const {
    return keys;
  }
  
  inline const MySimpleArray<V> &getValues() const {
    return values;
  }
  
  inline MySimpleArray<V> &getValues() {
    return values;
  }
  
  inline const MySimpleArray<uint32_t> &getIndexMemory() const {
    return indMem;
  }
  
  inline uint32_t size() const {
    return keyCnt;
  }
  
  inline bool isMember(const K &x) const {
    uint32_t index = queryIndex(x);
    return (index < keyCnt && keys[index] == x);
  }
  
  inline void erase(const K &k, int32_t keyId = -1) {
    if (keyId == -1) {
      keyId = queryIndex(k);
      if (keyId >= keyCnt || !(keys[keyId] == k)) return;
    }

//    uint64_t hash = getIndices(k);
//    uint32_t ha = hash, hb = hash >> 32;
    uint32_t ha = getIndexA(k);
    uint32_t hb = getIndexB(k);
    keyCnt--;
    
    // Delete the edge of keyId. By maintaining the linked lists on nodes ha and hb.
    int32_t headA = head[ha];
    if (headA == keyId) {
      head[ha] = nextAtA[keyId];
    } else {
      int t = headA;
      while (nextAtA[t] != keyId)
        t = nextAtA[t];
      nextAtA[t] = nextAtA[nextAtA[t]];
    }
    int32_t headB = head[hb];
    if (headB == keyId) {
      head[hb] = nextAtB[keyId];
    } else {
      int t = headB;
      while (nextAtB[t] != keyId)
        t = nextAtB[t];
      nextAtB[t] = nextAtB[nextAtB[t]];
    }
    
    // move the last to override current key-value
    if (keyId == keyCnt) return;
    const K &key = keys[keyCnt];
    keys[keyId] = key;
    values[keyId] = values[keyCnt];

//    uint64_t hashl = getIndices(key);
//    uint32_t hal = hashl, hbl = hashl >> 32;
    uint32_t hal = getIndexA(key);
    uint32_t hbl = getIndexB(key);
    
    // repair the broken linked list because of key movement
    nextAtA[keyId] = nextAtA[keyCnt];
    if (head[hal] == keyCnt) {
      head[hal] = keyId;
    } else {
      int t = head[hal];
      while (nextAtA[t] != keyCnt)
        t = nextAtA[t];
      nextAtA[t] = keyId;
    }
    nextAtB[keyId] = nextAtB[keyCnt];
    if (head[hbl] == keyCnt) {
      head[hbl] = keyId;
    } else {
      int t = head[hbl];
      while (nextAtB[t] != keyCnt)
        t = nextAtB[t];
      nextAtB[t] = keyId;
    }
    
    if (maintainDisjointSet) {
      // repair the disjoint set
      connectBFS(ha);
      connectBFS(hb);
    }
    
    // update the mapped index
    fixHalfTreeDFS<false, true, true>(keyId, hal, hbl);
    
    #ifdef FULL_DEBUG
    assert(checkIntegrity());
    #endif
  }
  
  bool checkIntegrity() const {
    for (int i = 0; i < size(); ++i) {
      V q;
      assert(query(keys[i], q));
      q &= VMASK;
      V e = values[i] & VMASK;
      assert(q == e);
      assert(queryIndex(keys[i]) == i);
    }
    
    return true;
  }
  
  //****************************************
  //*********As a randomizer
  //****************************************
public:
  uint64_t reportDataPlaneMemUsage() const {
    uint64_t size = mem.capacity * sizeof(V);
    
    cout << "Ma: " << ma * sizeof(V) << ", Mb: " << mb * sizeof(V) << endl;
    
    return size;
  }
  
  // return the mapped count of a value
  MySimpleArray<uint32_t> getCnt() const {
    MySimpleArray<uint32_t> cnt(1ULL << L);
    
    for (int i = 0; i < ma; i++) {
      for (int j = ma; j < ma + mb; j++) {
        cnt[memGet(i) ^ memGet(j)]++;
      }
    }
    return cnt;
  }
  
  void outputMappedValues(ofstream &fout) const {
    bool partial = (uint64_t) ma * (uint64_t) mb > (1UL << 22);
    
    if (partial) {
      for (int i = 0; i < (1 << 22); i++) {
        fout << uint32_t(memGet(rand() % (ma - 1)) ^ memGet(ma + rand() % (mb - 1))) << endl;
      }
    } else {
      for (int i = 0; i < ma; i++) {
        for (int j = ma; j < ma + mb; ++j) {
          fout << uint32_t(memGet(ma) ^ memGet(j)) << endl;
        }
      }
    }
  }
  
  int getStaticCnt() {
    return ma * mb;
  }
  
  uint64_t getMemoryCost() const {
    return mem.capacity * sizeof(mem[0]) + keys.capacity * sizeof(keys[0]) + values.capacity * sizeof(values[0]) +
           indMem.capacity * sizeof(indMem[0]);
  }
};


template<class K, class V, uint8_t L = sizeof(V) * 8>
class OthelloMap : public ControlPlaneOthello<K, V, L, false, true> {
public:
  explicit OthelloMap(uint32_t keyCapacity = 256) : ControlPlaneOthello<K, V, L, false, true>(keyCapacity) {}
};

template<class K>
class OthelloSet : public ControlPlaneOthello<K, bool, 0, false, true> {
public:
  explicit OthelloSet(uint32_t keyCapacity = 256) : ControlPlaneOthello<K, bool, 0, false, true>(keyCapacity) {}
  
  inline bool insert(const K &k) {
    return ControlPlaneOthello<K, bool, 0, false, true>::insert(make_pair(k, true));
  }
};
