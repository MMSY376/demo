#pragma once

#include "../control_plane.h"
#include "control_plane_othello.h"
#include "data_plane_othello.h"

template<class K, bool allowGateway, uint8_t DL>
class OthelloFilterControlPlane;

template<class K>
class OthelloGateWay : public DataPlaneOthello<K, uint32_t, 32, 0> {
  template<class K1, bool allowGateway, uint8_t DL> friend
  class OthelloFilterControlPlane;

private:
  vector<K> keys{};
  vector<uint16_t> ports{};

public:
  inline bool query(const K &k, uint16_t &out) const {
    uint32_t index = DataPlaneOthello<K, uint32_t, 32, 0>::query(k);
    if (index >= keys.size() || keys[index] != k) return false;
    
    out = ports[index];
    return true;
  }
  
  uint64_t getMemoryCost() const {
    return keys.size() * sizeof(keys[0]) + ports.size() * sizeof(ports[0]);
  }
};

template<class K, bool allowGateway, uint8_t DL = 0>
class OthelloFilterControlPlane : public ControlPlane<K, allowGateway> {
  static_assert(DL == 0 || !allowGateway, "You don't need digests while allowing gateways");
  ControlPlaneOthello<K, uint16_t, 16, DL, true> keyToHost;   // a map with set and iteration support, and exportable to data plane
  vector<DataPlaneOthello<K, uint16_t, 16, DL>> routers;
  vector<OthelloGateWay<K>> gateways;
  
  using ControlPlane<K, allowGateway>::insert;
  using ControlPlane<K, allowGateway>::remove;
  using ControlPlane<K, allowGateway>::graph;
  using ControlPlane<K, allowGateway>::capacity;

public:
  using ControlPlane<K, allowGateway>::l7;
  
  explicit OthelloFilterControlPlane(uint32_t capacity)
    : ControlPlane<K, allowGateway>(capacity), keyToHost(capacity) {
    gateways.reserve(capacity);
    routers.reserve(capacity);
  }
  
  inline const char *getName() const override {
    return "Othello-CP";
  }
  
  inline void insert(const K &k, uint16_t host) override {
    keyToHost.insert(make_pair(k, host));
  }
  
  void remove(const K &k) override {
    keyToHost.erase(k);
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
      vector<uint64_t> mem;
      mem.reserve((keyToHost.ma + keyToHost.mb + 1) / 2);
      
      bool lower = true;
      for (uint32_t element: keyToHost.getIndexMemory()) {
        if (lower) {
          mem.push_back(uint64_t(element));
        } else {
          mem.back() |= uint64_t(element) << 32;
        }
        
        lower = !lower;
      }
      
      auto hosts = keyToHost.getValues();
      templ.stop();
      
      for (int gatewayId: gatewayIds) {
        gateways.emplace_back();
        OthelloGateWay<K> &gateway = gateways.back();
        gateway.H = keyToHost.H;
        gateway.ma = keyToHost.ma;
        gateway.mb = keyToHost.mb;
        gateway.keys = keyToHost.keys;  // copy keys, and construct port array later
        gateway.mem = mem;  // the query structure gives the index of the key array and value array
        
        // construct port array
        for (int i = 0; i < keyToHost.size(); ++i) {
          int host = hosts[i];    // the destination of the i-th key
          vector<Graph<>::ShortestPathCell> path = graph->shortestPathTo[host];
          assert(path.size());  // asserting the host is a real host
          uint16_t nextHop = path[gatewayId].nextHop;
          
          uint16_t portNum = (uint16_t) (std::find(graph->adjacencyList[gatewayId].begin(),
                                                   graph->adjacencyList[gatewayId].end(),
                                                   Graph<>::AdjacencyMatrixCell({nextHop, 0})) -
                                         graph->adjacencyList[gatewayId].begin());
          assert(portNum != uint16_t(-1));
          gateway.ports.push_back(portNum);
        }
        
        #ifndef NDEBUG
        for (int i = 0; i < keyToHost.size(); ++i) {
          const K &key = keyToHost.keys[i];
          const uint16_t host = keyToHost.values[i];
          uint16_t port;
          gateway.query(key, port);
          
          assert(port != uint16_t(-1));
          
          int gatewayNextHop = graph->adjacencyList[gatewayId][port].to;
          uint16_t nextHop = graph->shortestPathTo[host][gatewayId].nextHop;
          assert(gatewayNextHop == nextHop);
        }
        #endif
        
//        if (gateways.size() >= 2) {
//          gateways.pop_back();  // just store 1 gateway
//        }
      }
    }
    
    {
      ostringstream oss;
      oss << "Router construction (" << routerIds.size() << " routers)";
      Clocker clocker(oss.str());
      
      ControlPlaneOthello<K, uint16_t, 16, DL, true> keyToPort;
      // copy the graph from the control plane
      keyToPort.connectivityForest = keyToHost.connectivityForest;
      keyToPort.indMem = keyToHost.indMem;
      keyToPort.keys = keyToHost.keys;
      keyToPort.keyCnt = keyToHost.keyCnt;
      keyToPort.H = keyToHost.H;
      keyToPort.Hd = keyToHost.Hd;
      keyToPort.head = keyToHost.head;
      keyToPort.nextAtA = keyToHost.nextAtA;
      keyToPort.nextAtB = keyToHost.nextAtB;
      keyToPort.ma = keyToHost.ma;
      keyToPort.mb = keyToHost.mb;
      keyToPort.minimalKeyCapacity = keyToHost.minimalKeyCapacity;
      keyToPort.memResize();
      keyToPort.values.resize(keyToPort.keyCnt);
      
      auto hosts = keyToHost.getValues();
      
      for (int routerId: routerIds) {
        // different routers have different key-port mappings
        // construct port array
        for (int i = 0; i < keyToHost.size(); ++i) {
          int host = hosts[i];    // the destination of the i-th key
          vector<Graph<>::ShortestPathCell> path = graph->shortestPathTo[host];
          assert(path.size());  // asserting the host is a real host
          uint16_t nextHop = path[routerId].nextHop;
          
          uint16_t portNum = (uint16_t) (std::find(graph->adjacencyList[routerId].begin(),
                                                   graph->adjacencyList[routerId].end(),
                                                   Graph<>::AdjacencyMatrixCell({nextHop, 0})) -
                                         graph->adjacencyList[routerId].begin());
          keyToPort.values[i] = portNum;
        }
        
        // update the query structure
        keyToPort.fillOnlyValue();
        
        routers.emplace_back(keyToPort);
        
        #ifndef NDEBUG
        for (int i = 0; i < keyToHost.size(); ++i) {
          const K &key = keyToHost.keys[i];
          const uint16_t host = keyToHost.values[i];
          uint16_t port;
          assert(keyToPort.query(key, port) && port != uint16_t(-1));
          
          int gatewayNextHop = graph->adjacencyList[routerId][port].to;
          uint16_t nextHop = graph->shortestPathTo[host][routerId].nextHop;
          assert(gatewayNextHop == nextHop);
        }
        #endif
  
//        if (routers.size() >= 2) {
//          routers.pop_back();  // just store 1 router
//        }
      }
    }
  }
  
  inline bool query(const K &k, uint16_t &out) const override {
    return keyToHost.query(k, out);
  }
  
  
  inline uint16_t queryGateway(uint16_t id, const K &key) const override {
    uint16_t port = static_cast<uint16_t>(-1);
    gateways[id].query(key, port);
    return port;
  }
  
  inline uint16_t queryRouter(uint16_t id, const K &key) const override {
    uint16_t port = static_cast<uint16_t>(-1);
    routers[id - (l7 ? 0 : 3)].query(key, port);
    return port;
  }
  
  void scenario(int topo) override {
    ControlPlane<K, allowGateway>::scenario(topo);
  }
  
  uint64_t getGatewayMemoryCost() const override {
    uint64_t result = 0;
    
    for (const OthelloGateWay<K> &gateway: gateways) {
      result += gateway.getMemoryCost();
    }
    
    return result;
  }
  
  uint64_t getRouterMemoryCost() const override {
    uint64_t result = 0;
    
    for (const DataPlaneOthello<K, uint16_t, 16, DL> &router: routers) {
      result += router.getMemoryCost();
    }
    
    return result;
  }
  
  uint64_t getControlPlaneMemoryCost() const override {
    return keyToHost.getMemoryCost();
  }
};
