#ifndef CONCURY_COMMON_H_
#define CONCURY_COMMON_H_
#include "common.h"
#include "Othello/control_plane_othello.h"
#include "Othello/data_plane_othello.h"

// Data plane
extern vector<DataPlaneOthello<Tuple3, uint16_t, 12, 0>> othelloForQuery;  // 3-tuple -> DIPInd  // requires initialization,
extern uint16_t **ht;    // [VIPInd][DIPInd] -> DIP Addr_Port
extern vector<DIP> dipPools[VIP_NUM];
// !Data plane

// Control plane
extern vector<ControlPlaneOthello<Tuple3, uint16_t, 12, 0, false, false, false>> conn;  // track the connections and their dipIndices
extern uint16_t **newHt;
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

#endif /* CONCURY_COMMON_H_ */
