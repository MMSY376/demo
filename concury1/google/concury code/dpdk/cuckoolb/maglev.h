#pragma once

#include "../common.h"
//#include "libcuckoo/cuckoohash_map.hh"
#include "CuckooPresized/control_plane_cuckoo_map.h"
#include "../hash.h"

// Data & Control plane
extern MySimpleArray<ControlPlaneCuckooMap<uint64_t, uint16_t, uint8_t, false>> connTrackingTables; // 3-tuple -> DIPInd requires initialization
extern MySimpleArray<MySimpleArray<uint16_t>> ht;    // [VIPInd][DIPInd] -> DIP Addr_Port
extern MySimpleArray<MySimpleArray<DIP>> dipPools;  // vipIndex, dipindex -> dip
extern MySimpleArray<int> dipNum;

extern MySimpleArray<MySimpleArray<uint16_t>> newHt;
// !Control plane
