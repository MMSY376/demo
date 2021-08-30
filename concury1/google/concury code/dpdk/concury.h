#ifndef CONCURY_COMMON_H_
#define CONCURY_COMMON_H_
#include "common.h"
#include "Othello/control_plane_othello.h"
#include "Othello/data_plane_othello.h"

// Data plane
extern MySimpleArray<DataPlaneOthello<Tuple3, uint16_t, 12, 0>> othelloForQuery;  // 3-tuple -> DIPInd  // requires initialization,
extern MySimpleArray<MySimpleArray<uint16_t>> ht;    // [VIPInd][DIPInd] -> DIP Addr_Port
extern MySimpleArray<MySimpleArray<DIP>> dipPools;
// !Data plane

// Control plane
extern MySimpleArray<ControlPlaneOthello<Tuple3, uint16_t, 12, 0, true, false, true>> conn;  // track the connections and their dipIndices
extern MySimpleArray<MySimpleArray<uint16_t>> newHt;
// !Control plane

void updateDataPlaneCallBack(int vipInd);
void configureDataPlane(int vipInd);

void simulateConnectionAdd(int count = 0, int prestart = 0);

void simulateConnectionLeave();

/**
 * Simulate an update in the weight of all dips
 */
void simulateUpdatePoolData();

/**
 * Simulate cnt of dips down
 */
void simulateDipDown(int cnt);

/**
 * update data plane to make the HT consistent with the dip weight,
 * while ensuring PCC.
 *
 * assuming dip pools and conn have been properly constructed
 */
void updateDataPlane(bool mute = false);
void updateDataPlaneStupid(bool mute = false);

void testResillience();

void initDipPool();

/**
 * read connection info from stdin, and log important cases:
 * hash conflicts of different conn: associate mem occupy (size and taken-up),
 * served 1M packets: time and average speed
 *
 * format: 1 byte: ip veprintMemoryUsagersion (server(dst)/client(src): 0x44, 0x46, 0x64, 0x66),
 * 1 byte Protocol Number (tcp 6/udp 17),
 * 4/16 byte src addr, 2 byte src port,
 * 4/16 byte dst addr, 2 byte dst port.
 */
inline DIP concury_lookup(uint16_t vipInd, uint32_t srcAddr, uint16_t srcPort, uint8_t protocol) {
  // Step 1: read 5-tuple of a packet
  // Step 2: lookup the VIPTable to get VIPInd
  Tuple3 tuple = {
    src: {srcAddr, srcPort},
    protocol: protocol
  };
  
  // Step 3: lookup corresponding Othello array
  uint16_t htInd = othelloForQuery[vipInd].query(tuple);
//  cout << "Ht index: " << htInd << endl;
  htInd &= (HT_SIZE - 1);
  
  return dipPools[vipInd][ht[vipInd][htInd]];
}
#endif /* CONCURY_COMMON_H_ */
