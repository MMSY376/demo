#pragma once

#include "../control_plane.h"
#include "control_plane_cuckoo_map.h"

template<class K, class Match = uint8_t>
class TwoLevelCuckooRouter {
public:
  DataPlaneCuckooMap<K, uint16_t, Match, 2, 4> level1;
  ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> level2;
  
  TwoLevelCuckooRouter(ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4> lv1,
                       ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> lv2) : level1(lv1), level2(lv2) {
  }
  
  // Returns true if found.  Sets *out = value.
  bool query(const K &k, uint16_t &out) const {
    if (level1.Find(k, out) && level2.Find(k, out)) {
      for (auto l : level1.locate(k)) {
        cout << l.first << endl;
      }
      cout << level2.locate(k).first << endl;
    }
    assert(!level1.Find(k, out) || !level2.Find(k, out));
    return level1.Find(k, out) || level2.Find(k, out);
  }
  
  uint64_t getMemoryCost() const {
    return level1.getMemoryCost() + level2.getMemoryCost();
  }
};

template<class K, class Match = uint8_t>
class TwoLevelCuckooGateway {
  ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> level1;
  ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> level2;

public:
  explicit TwoLevelCuckooGateway(
    const ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4> &lv1,
    const ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> &lv2 = ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>())
    : level1(lv1), level2(lv2) {
  }
  
  explicit TwoLevelCuckooGateway(
    const ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> &lv1,
    const ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> &lv2 = ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>())
    : level1(lv1), level2(lv2) {
  }
  
  // Returns true if found.  Sets *out = value.
  inline bool query(const K &k, uint16_t &out) const {
    return level1.Find(k, out) || level2.Find(k, out);
  }
  
  /// compose two maps in place
  void Compose(unordered_map<uint16_t, uint16_t> &migrate) {
    level1.Compose(migrate);
    level2.Compose(migrate);
  }
  
  uint64_t getMemoryCost() const {
    return level1.getMemoryCost() + level2.getMemoryCost();
  }
};

template<class K, bool allowGateway, class Match = uint8_t, bool gatewayTwoLevel = true>
class TwoLevelCuckooControlPlane : public ControlPlane<K, allowGateway> {
  vector<TwoLevelCuckooGateway<K, Match>> gateways;
  vector<TwoLevelCuckooRouter<K, Match>> routers;
  
  using ControlPlane<K, allowGateway>::insert;
  using ControlPlane<K, allowGateway>::remove;
  using ControlPlane<K, allowGateway>::graph;
  using ControlPlane<K, allowGateway>::capacity;

public:
  using ControlPlane<K, allowGateway>::l7;
  ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4> *level1;
  ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> *level2;
  
  explicit TwoLevelCuckooControlPlane(uint32_t capacity)
    : ControlPlane<K, allowGateway>(capacity),
      level1(new ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4>(capacity)),
      level2(new ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>(capacity / 10)) {
    gateways.reserve(capacity);
    routers.reserve(capacity);
  }
  
  virtual ~TwoLevelCuckooControlPlane() {
    if (level1) delete level1;
    if (level2) delete level2;
  }
  
  inline const char *getName() const override {
    return "Cuckoo-CP";
  }
  
  inline void insert_(const K &k, uint16_t host, bool inRebuild = false) {
    const K *result = level1->Insert(k, host);
  
    #ifndef NDEBUG
    if (!inRebuild && rand() % 10000 == 1) result = nullptr;  // intentionally set some result to be full
    #endif
    
    if (result == nullptr) { // level1 full, rebuild
      if (inRebuild) {
        throw runtime_error("rebuild in rebuild");
      }
      rebuild(k, host);
    } else if (result != &k) { // collision
      Counter::count(getName(), "digest collision");
  
      vector<const K *>collisions = level1->FindAllCollisions(k);
      for (int i = 0; i < 2; ++i) {
        if (collisions[i] == nullptr) continue;
        
        uint16_t out;
        level1->Find(*collisions[i], out);
        
        if (level2->Insert(*collisions[i], out) != result) {  // l2 full, rebuild l2
          rebuildL2(*collisions[i], out);
        }
        level1->Remove(*collisions[i], false);
      }
      
      level1->PreventCollision(k);
      
      if (level2->Insert(k, host) != &k) {  // l2 full, rebuild l2
        rebuildL2(k, host);
      }
    } else {
      Counter::count(getName(), "lv1 add");
    }
  }
  
  inline void insert(const K &k, uint16_t host) override {
    insert_(k, host, false);
  }
  
  inline void remove(const K &k) override {
    if (!level1->Remove(k)) {
      level2->Remove(k);
    }
  }
  
  inline bool query(const K &k, uint16_t &out) const override {
    return level1->Find(k, out) || level2->Find(k, out);
  }
  
  inline uint16_t queryGateway(uint16_t id, const K &key) const override {
    uint16_t port = static_cast<uint16_t>(-1);
    return gateways[id].query(key, port), port;
  }
  
  inline uint16_t queryRouter(uint16_t id, const K &key) const override {
    uint16_t port = static_cast<uint16_t>(-1);
    return routers[id - (l7 ? 0 : 3)].query(key, port), port;
  }
  
  using ControlPlane<K, allowGateway>::getGatewayIds;
  using ControlPlane<K, allowGateway>::getRouterIds;
  using ControlPlane<K, allowGateway>::getHostIds;
  
  void construct() override {
    auto gatewayIds = ControlPlane<K, allowGateway>::getGatewayIds();
    auto routerIds = ControlPlane<K, allowGateway>::getRouterIds();
    
    auto hostIds = ControlPlane<K, allowGateway>::getHostIds();
    
    if (!gatewayIds.empty()) {
      ostringstream oss;
      oss << "Gateway construction (" << gatewayIds.size() << " gateways)";
      Clocker clocker(oss.str());
      
      Clocker templ("template creation");
      TwoLevelCuckooGateway<K, Match> *gatewayTemplate;
      
      if (gatewayTwoLevel) {
        gatewayTemplate = new TwoLevelCuckooGateway<K, Match>(*level1, *level2);
      } else {
        auto *temp = new ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>(*level1);
        
        while (1) {
          if (!temp) {
            temp = new ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>(level1->entryCount + level2->entryCount);
            for (auto &bucket: level1->buckets_) {  // all buckets
              for (int slot = 0; slot < 4 && temp; ++slot) {
                if (bucket.occupiedMask & (1ULL << slot)) {
                  const K &key = bucket.keys[slot];
                  const uint16_t host = bucket.values[slot];
                  
                  if (temp->Insert(key, host) != &key) {
                    delete temp;
                    temp = 0;
                  }
                }
              }
            }
          }
          
          for (auto &bucket: level2->buckets_) {  // all buckets
            for (int slot = 0; slot < 4 && temp; ++slot) {
              if (bucket.occupiedMask & (1ULL << slot)) {
                const K &key = bucket.keys[slot];
                const uint16_t host = bucket.values[slot];
                
                if (temp->Insert(key, host) != &key) {
                  delete temp;
                  temp = 0;
                }
              }
            }
          }
          
          if (temp) break;
        }
        
        gatewayTemplate = new TwoLevelCuckooGateway<K, Match>(*temp);
      }
      templ.stop();
      
      for (int gatewayId: gatewayIds) {
        gateways.emplace_back(*gatewayTemplate);
        TwoLevelCuckooGateway<K, Match> &gateway = gateways.back();
        unordered_map<uint16_t, uint16_t> hostToPort;
        
        for (int i = 0; i < hostIds.size(); ++i) {
          int host = hostIds[i];
          vector<Graph<>::ShortestPathCell> path = graph->shortestPathTo[host];
          assert(path.size());  // asserting the host is a real host
          uint16_t nextHop = path[gatewayId].nextHop;
          
          uint16_t portNum = (uint16_t) (std::find(graph->adjacencyList[gatewayId].begin(),
                                                   graph->adjacencyList[gatewayId].end(),
                                                   Graph<>::AdjacencyMatrixCell({nextHop, 0})) -
                                         graph->adjacencyList[gatewayId].begin());
          hostToPort.insert(make_pair(host, portNum));
        }
        
        gateway.Compose(hostToPort);
        
        #ifndef NDEBUG
        unordered_map<K, uint16_t, Hasher32<K>> map;
        for (auto &bucket: level1->buckets_) {  // all buckets
          for (int slot = 0; slot < 4; ++slot) {
            if (bucket.occupiedMask & (1ULL << slot)) {
              const K &key = bucket.keys[slot];
              const uint16_t host = bucket.values[slot];
              map.insert(make_pair(key, host));
            }
          }
        }
        
        for (auto &bucket: level2->buckets_) {  // all buckets
          for (int slot = 0; slot < 4; ++slot) {
            if (bucket.occupiedMask & (1ULL << slot)) {
              const K &key = bucket.keys[slot];
              const uint16_t host = bucket.values[slot];
              map.insert(make_pair(key, host));
            }
          }
        }
        
        for (auto &pair:map) {
          const K &key = pair.first;
          const uint16_t host = pair.second;
          
          uint16_t port = uint16_t(-1);
          assert(gateway.query(key, port));
          assert(port != uint16_t(-1));
          int gatewayNextHop = graph->adjacencyList[gatewayId][port].to;
          uint16_t nextHop = graph->shortestPathTo[host][gatewayId].nextHop;
          assert(gatewayNextHop == nextHop);
        }
        #endif
      }
      
      delete gatewayTemplate;

//      if (gateways.size() >= 2) {
//        gateways.pop_back();  // just store 1 gateway
//      }
    }
    
    {
      ostringstream oss;
      oss << "Router construction (" << routerIds.size() << " routers)";
      Clocker clocker(oss.str());
      
      ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4> lv1Template(*level1);
      ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> lv2Template(*level2);
      
      for (int routerId: routerIds) {
        unordered_map<uint16_t, uint16_t> hostToPort;
        
        for (int i = 0; i < hostIds.size(); ++i) {
          int host = hostIds[i];
          vector<Graph<>::ShortestPathCell> path = graph->shortestPathTo[host];
          assert(path.size());  // asserting the host is a real host
          uint16_t nextHop = path[routerId].nextHop;
          
          uint16_t portNum = (uint16_t) (std::find(graph->adjacencyList[routerId].begin(),
                                                   graph->adjacencyList[routerId].end(),
                                                   Graph<>::AdjacencyMatrixCell({nextHop, 0})) -
                                         graph->adjacencyList[routerId].begin());
          assert(portNum != uint16_t(-1));
          hostToPort.insert(make_pair(host, portNum));
        }
        
        ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4> tmp1(lv1Template);
        ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4> tmp2(lv2Template);
        tmp1.Compose(hostToPort);
        tmp2.Compose(hostToPort);
        
        routers.emplace_back(tmp1, tmp2);
        TwoLevelCuckooRouter<K, Match> &router = routers.back();
        
        #ifndef NDEBUG
        uint32_t count = 0;
        unordered_map<K, uint16_t, Hasher32<K>> map;
        for (auto &bucket: level1->buckets_) {  // all buckets
          for (int slot = 0; slot < 4; ++slot) {
            if (bucket.occupiedMask & (1ULL << slot)) {
              const K &key = bucket.keys[slot];
              uint16_t host = bucket.values[slot];
              
              map.insert(make_pair(key, host));
            }
          }
        }
        
        for (auto &bucket: level2->buckets_) {  // all buckets
          for (int slot = 0; slot < 4; ++slot) {
            if (bucket.occupiedMask & (1ULL << slot)) {
              const K &key = bucket.keys[slot];
              uint16_t host = bucket.values[slot];
              map.insert(make_pair(key, host));
            }
          }
        }
        
        for (auto &pair:map) {
          const K &key = pair.first;
          const uint16_t host = pair.second;
          
          uint16_t port = uint16_t(-1);
          assert(router.query(key, port));
          assert(port != uint16_t(-1));
          
          int gatewayNextHop = graph->adjacencyList[routerId][port].to;
          uint16_t nextHop = graph->shortestPathTo[host][routerId].nextHop;

//          assert (gatewayNextHop == nextHop);
          if (gatewayNextHop != nextHop) {
            assert(level1->locate(key) == router.level1.locate(key)[0]);
            router.query(key, port);
            query(key, port);
          }
        }
        #endif

//        if (routers.size() >= 2) {
//          routers.pop_back();  // just store 1 router
//        }
      }
    }
  }
  
  void scenario(int topo) override {
    ControlPlane<K, allowGateway>::scenario(topo);
  }
  
  uint64_t getGatewayMemoryCost() const override {
    uint64_t result = 0;
    
    for (const TwoLevelCuckooGateway<K, Match> &gateway: gateways) {
      result += gateway.getMemoryCost();
    }
    
    return result;
  }
  
  uint64_t getRouterMemoryCost() const override {
    uint64_t result = 0;
    
    for (const TwoLevelCuckooRouter<K, Match> &router: routers) {
      result += router.getMemoryCost();
    }
    
    return result;
  }
  
  uint64_t getControlPlaneMemoryCost() const override {
    return level1->getMemoryCost() + level2->getMemoryCost();
  }

private:
  void rebuild(const K &k, uint16_t host) {
    Counter::count(getName(), "level1 rebuild");
    Clocker rebuild("Cuckoo level1 rebuild");
    
    unordered_map<K, uint16_t, Hasher32<K>> map = level1->toMap();
    unordered_map<K, uint16_t, Hasher32<K>> map2 = level2->toMap();
    map.insert(map2.begin(), map2.end());
    
    map.insert(make_pair(k, host));
    
    delete level1;
    delete level2;
    level1 = nullptr;
    level2 = nullptr;
    
    uint32_t capacity = ControlPlane<K, allowGateway>::capacity;
    while (!level1) {
      Counter::count(getName(), "level1 re-rebuild");
      
      level1 = new ControlPlaneCuckooMap<K, uint16_t, Match, true, 2, 4>(capacity);
      level2 = new ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>(capacity / 10);
      
      for (auto it = map.begin(); it != map.end(); ++it) {
        try {
          insert_(it->first, it->second, true);
        } catch (exception &e) {
          delete level1;
          delete level2;
          level1 = nullptr;
          level2 = nullptr;
          break;
        }
      }
    }
  }
  
  void rebuildL2(const K &k, uint16_t host) {
    Counter::count(getName(), "level2 full, only rebuild lv2");
    unordered_map<K, uint16_t, Hasher32<K>> map = level2->toMap();
    map.insert(make_pair(k, host));
    
    delete level2;
    level2 = 0;
    
    while (!level2) {
      level2 = new ControlPlaneCuckooMap<K, uint16_t, Match, false, 2, 4>(
        ControlPlane<K, allowGateway>::capacity / 10);
      
      for (auto it = map.begin(); it != map.end(); ++it) {
        const K *result = level2->Insert(it->first, it->second);
        
        if (result != &it->first) {
          delete level2;
          level2 = 0;
          break;
        }
      }
    }
  }
};
