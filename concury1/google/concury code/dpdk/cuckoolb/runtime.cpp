/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cinttypes>
#include <sys/types.h>
#include <cstring>
#include <sys/queue.h>
#include <cstdarg>
#include <cerrno>
#include <getopt.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_lpm.h>

#include "../main.h"
#include "../common.h"
#include "maglev.h"

#define OFFSETOF(type, field)    ((unsigned long) &(((type *) 0)->field))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#ifndef APP_LCORE_IO_FLUSH
#define APP_LCORE_IO_FLUSH           1000000
#endif

#ifndef APP_LCORE_WORKER_FLUSH
#define APP_LCORE_WORKER_FLUSH       1000000
#endif

#ifndef APP_STATS
#define APP_STATS                    100000
#endif

#define APP_IO_RX_DROP_ALL_PACKETS   0
#define APP_WORKER_DROP_ALL_PACKETS  0
#define APP_IO_TX_DROP_ALL_PACKETS   0

#ifndef APP_IO_RX_PREFETCH_ENABLE
#define APP_IO_RX_PREFETCH_ENABLE    1
#endif

#ifndef APP_WORKER_PREFETCH_ENABLE
#define APP_WORKER_PREFETCH_ENABLE   1
#endif

#ifndef APP_IO_TX_PREFETCH_ENABLE
#define APP_IO_TX_PREFETCH_ENABLE    1
#endif

#if APP_IO_RX_PREFETCH_ENABLE
#define APP_IO_RX_PREFETCH0(p)       rte_prefetch0(p)
#define APP_IO_RX_PREFETCH1(p)       rte_prefetch1(p)
#else
#define APP_IO_RX_PREFETCH0(p)
#define APP_IO_RX_PREFETCH1(p)
#endif

#if APP_WORKER_PREFETCH_ENABLE
#define APP_WORKER_PREFETCH0(p)      rte_prefetch0(p)
#define APP_WORKER_PREFETCH1(p)      rte_prefetch1(p)
#else
#define APP_WORKER_PREFETCH0(p)
#define APP_WORKER_PREFETCH1(p)
#endif

#if APP_IO_TX_PREFETCH_ENABLE
#define APP_IO_TX_PREFETCH0(p)       rte_prefetch0(p)
#define APP_IO_TX_PREFETCH1(p)       rte_prefetch1(p)
#else
#define APP_IO_TX_PREFETCH0(p)
#define APP_IO_TX_PREFETCH1(p)
#endif

/*
 *
 */
static inline void
app_lcore_io_rx_buffer_to_send(struct app_lcore_params_io *lp, uint32_t worker, struct rte_mbuf *mbuf, uint32_t bsz) {
  uint32_t pos;
  int ret;
  
  pos = lp->rx.mbuf_out[worker].n_mbufs;
  lp->rx.mbuf_out[worker].array[pos++] = mbuf;
  if (likely(pos < bsz)) {                           // only send a bulk. if not enough, skip until a flush or a bulk
    lp->rx.mbuf_out[worker].n_mbufs = pos;
    return;
  }
  
  ret = rte_ring_sp_enqueue_bulk(lp->rx.rings[worker], (void **) lp->rx.mbuf_out[worker].array, bsz, nullptr);  // only copy the pointers of the packets. The worker should prefetch the data again. The packet is currently prefetched into all level of caches in io thread. so the packet is found in the L3 cache if the prefetch finishes before the poll.
  
  if (unlikely(ret == 0)) {  // FIXME discard packets if fail
    uint32_t k;
    for (k = 0; k < bsz; k++) {
      struct rte_mbuf *m = lp->rx.mbuf_out[worker].array[k];
      rte_pktmbuf_free(m);
    }
  }
  
  lp->rx.mbuf_out[worker].n_mbufs = 0;
  lp->rx.mbuf_out_flush[worker] = 0;       // TODO meaning of this? will skip flush when it is 0

#if APP_STATS
  lp->rx.rings_iters[worker]++;
  if (likely(ret == bsz)) {
    lp->rx.rings_count[worker]++;
  }
  if (unlikely(lp->rx.rings_iters[worker] == APP_STATS)) {
    unsigned lcore = rte_lcore_id();
    
    printf("\tI/O RX %u out (worker %u): enq success rate = %.2f\n", lcore, (unsigned) worker,
           ((double) lp->rx.rings_count[worker]) / ((double) lp->rx.rings_iters[worker]));
    lp->rx.rings_iters[worker] = 0;
    lp->rx.rings_count[worker] = 0;
  }
#endif
}

static inline void
app_lcore_io_rx(struct app_lcore_params_io *lp, uint32_t n_workers, uint32_t bsz_rd, uint32_t bsz_wr, uint8_t pos_lb) {
  struct rte_mbuf *mbuf_1_0, *mbuf_1_1, *mbuf_2_0, *mbuf_2_1;
  uint8_t *data_1_0, *data_1_1 = nullptr;
  uint32_t i;
  
  for (i = 0; i < lp->rx.n_nic_queues; i++) {   // iterate over all (nic, queues) pairs taken over by this io thread
    uint16_t port = lp->rx.nic_queues[i].port;
    uint8_t queue = lp->rx.nic_queues[i].queue;
    uint32_t n_mbufs, j;
    
    n_mbufs = rte_eth_rx_burst(port, queue, lp->rx.mbuf_in.array, (uint16_t) bsz_rd);
    
    if (unlikely(n_mbufs == 0)) {
      continue;
    }

#if APP_STATS
    lp->rx.nic_queues_iters[i]++;
    lp->rx.nic_queues_count[i] += n_mbufs;
    if (unlikely(lp->rx.nic_queues_iters[i] == (APP_STATS * 100))) {
      struct rte_eth_stats stats{};
      unsigned lcore = rte_lcore_id();
      
      rte_eth_stats_get(port, &stats);
      
      printf("I/O RX %u in (NIC port %u): NIC drop ratio = %.2f avg burst size = %.2f\n",
             lcore, port, (double) stats.imissed / (double) (stats.imissed + stats.ipackets),
             ((double) lp->rx.nic_queues_count[i]) / ((double) lp->rx.nic_queues_iters[i]));
      lp->rx.nic_queues_iters[i] = 0;
      lp->rx.nic_queues_count[i] = 0;
      
      rte_eth_stats_reset(port);
    }
#endif

#if APP_IO_RX_DROP_ALL_PACKETS
    for (j = 0; j < n_mbufs; j ++) {
      struct rte_mbuf *pkt = lp->rx.mbuf_in.array[j];
      rte_pktmbuf_free(pkt);
    }

    continue;
#endif
    
    // pre-fill the soft pipeline
    mbuf_1_0 = lp->rx.mbuf_in.array[0];
    mbuf_1_1 = lp->rx.mbuf_in.array[1];
    data_1_0 = rte_pktmbuf_mtod(mbuf_1_0, uint8_t * );      // get the header from the mbuf
    if (likely(n_mbufs > 1)) {
      data_1_1 = rte_pktmbuf_mtod(mbuf_1_1, uint8_t * );
    }
    
    mbuf_2_0 = lp->rx.mbuf_in.array[2];
    mbuf_2_1 = lp->rx.mbuf_in.array[3];
    APP_IO_RX_PREFETCH0(mbuf_2_0);  // prefetch, regardless of the array bound
    APP_IO_RX_PREFETCH0(mbuf_2_1);
    
    // trigger the pipeline
    for (j = 0; j + 3 < n_mbufs; j += 2) {
      struct rte_mbuf *mbuf_0_0, *mbuf_0_1;
      uint8_t *data_0_0, *data_0_1;
      uint32_t worker_0, worker_1;
      
      // stage 1, data to be sent in this loop
      mbuf_0_0 = mbuf_1_0;
      mbuf_0_1 = mbuf_1_1;
      data_0_0 = data_1_0;
      data_0_1 = data_1_1;
      
      // stage 2, prefetch data for the next batch
      mbuf_1_0 = mbuf_2_0;
      mbuf_1_1 = mbuf_2_1;
      data_1_0 = rte_pktmbuf_mtod(mbuf_2_0, uint8_t * );
      data_1_1 = rte_pktmbuf_mtod(mbuf_2_1, uint8_t * );
      APP_IO_RX_PREFETCH0(data_1_0);   // prefetch the data for worker threads and for RSS here
      APP_IO_RX_PREFETCH0(data_1_1);
      
      // stage 3, prefetch mbuf for the next next batch
      mbuf_2_0 = lp->rx.mbuf_in.array[j + 4];
      mbuf_2_1 = lp->rx.mbuf_in.array[j + 5];
      APP_IO_RX_PREFETCH0(mbuf_2_0);
      APP_IO_RX_PREFETCH0(mbuf_2_1);
      
      // rss is supported by this. this simple method suffers from thread crashes. But it only affects efficiency, so it is acceptable
      worker_0 = int(data_0_0[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, src_addr) + 3]) * n_workers / 256;
      worker_1 = int(data_0_1[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, src_addr) + 3]) * n_workers / 256;

//      if (worker_0 >= 4 || worker_0 < 0) {
//        cout << "wrong 0: " << int(data_0_0[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, src_addr) + 3]) << " " << worker_0
//             << endl;
//      }

//      if (worker_1 >= 4 || worker_1 < 0) {
//        cout << "wrong 1: " << int(data_0_1[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, src_addr) + 3]) << " " << worker_1
//             << endl;
//      }
//      cout << "RSS port: " << port << " queue: " << (int) queue << " distribute: " << worker_0 << ", " << worker_1 <<
//      " because data are: " << (int) data_0_0[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, dst_addr) + 3] << ", " << (int) data_0_1[sizeof(ether_hdr) + OFFSETOF(ipv4_hdr, dst_addr) + 3] << endl;
      
      app_lcore_io_rx_buffer_to_send(lp, worker_0, mbuf_0_0, bsz_wr);
      app_lcore_io_rx_buffer_to_send(lp, worker_1, mbuf_0_1, bsz_wr);
    }
    
    /* Handle the last 1, 2 (when n_mbufs is even) or 3 (when n_mbufs is odd) packets  */
    for (; j < n_mbufs; j += 1) {
      struct rte_mbuf *mbuf;
      uint8_t *data;
      uint32_t worker;
      
      mbuf = mbuf_1_0;
      mbuf_1_0 = mbuf_1_1;
      mbuf_1_1 = mbuf_2_0;
      mbuf_2_0 = mbuf_2_1;
      
      data = rte_pktmbuf_mtod(mbuf, uint8_t * );
      
      APP_IO_RX_PREFETCH0(mbuf_1_0);
      
      worker = data[pos_lb] & (n_workers - 1);
      
      app_lcore_io_rx_buffer_to_send(lp, worker, mbuf, bsz_wr);
    }
  }
}

static inline void app_lcore_io_rx_flush(struct app_lcore_params_io *lp, uint32_t n_workers) {
  uint32_t worker;
  
  for (worker = 0; worker < n_workers; worker++) {
    int ret;
    
    if (likely((lp->rx.mbuf_out_flush[worker] == 0) || (lp->rx.mbuf_out[worker].n_mbufs == 0))) {
      lp->rx.mbuf_out_flush[worker] = 1;
      continue;
    }
    
    ret = rte_ring_sp_enqueue_bulk(lp->rx.rings[worker], (void **) lp->rx.mbuf_out[worker].array, lp->rx.mbuf_out[worker].n_mbufs, nullptr);
    
    if (unlikely(ret == 0)) {
      uint32_t k;
      for (k = 0; k < lp->rx.mbuf_out[worker].n_mbufs; k++) {
        struct rte_mbuf *pkt_to_free = lp->rx.mbuf_out[worker].array[k];
        rte_pktmbuf_free(pkt_to_free);
      }
    }
    
    lp->rx.mbuf_out[worker].n_mbufs = 0;
    lp->rx.mbuf_out_flush[worker] = 1;
  }
}

static inline void
app_lcore_io_tx(struct app_lcore_params_io *lp, uint32_t n_workers, uint32_t bsz_rd, uint32_t bsz_wr) {
  uint32_t worker;
  
  for (worker = 0; worker < n_workers; worker++) {  // each worker
    uint32_t i;
    
    for (i = 0; i < lp->tx.n_nic_ports; i++) {  // each port
      uint16_t port = lp->tx.nic_ports[i];
      struct rte_ring *ring = lp->tx.rings[port][worker];   // each port and each worker, we have a ring from the worker to io thread
      uint32_t n_mbufs, n_pkts;
      int ret;
      
      n_mbufs = lp->tx.mbuf_out[port].n_mbufs;
      ret = rte_ring_sc_dequeue_bulk(ring, (void **) &lp->tx.mbuf_out[port].array[n_mbufs], bsz_rd, nullptr);
      
      if (unlikely(ret == 0)) {
        continue;
      }
      
      n_mbufs += bsz_rd;

#if APP_IO_TX_DROP_ALL_PACKETS
      {
        uint32_t j;
        APP_IO_TX_PREFETCH0(lp->tx.mbuf_out[port].array[0]);
        APP_IO_TX_PREFETCH0(lp->tx.mbuf_out[port].array[1]);

        for (j = 0; j < n_mbufs; j ++) {
          if (likely(j < n_mbufs - 2)) {
            APP_IO_TX_PREFETCH0(lp->tx.mbuf_out[port].array[j + 2]);
          }

          rte_pktmbuf_free(lp->tx.mbuf_out[port].array[j]);
        }

        lp->tx.mbuf_out[port].n_mbufs = 0;

        continue;
      }
#endif
      
      if (unlikely(n_mbufs < bsz_wr)) {
        lp->tx.mbuf_out[port].n_mbufs = n_mbufs;
        continue;
      }
      
      n_pkts = rte_eth_tx_burst(port, 0, lp->tx.mbuf_out[port].array, (uint16_t) n_mbufs);

#if APP_STATS
      lp->tx.nic_ports_iters[port]++;
      lp->tx.nic_ports_count[port] += n_pkts;
      if (unlikely(lp->tx.nic_ports_iters[port] == APP_STATS)) {
        unsigned lcore = rte_lcore_id();
        
        printf("\t\t\tI/O TX %u out (port %u): avg burst size = %.2f\n", lcore, port,
               ((double) lp->tx.nic_ports_count[port]) / ((double) lp->tx.nic_ports_iters[port]));
        lp->tx.nic_ports_iters[port] = 0;
        lp->tx.nic_ports_count[port] = 0;
      }
#endif
      
      if (unlikely(n_pkts < n_mbufs)) {
        uint32_t k;
        for (k = n_pkts; k < n_mbufs; k++) {
          struct rte_mbuf *pkt_to_free = lp->tx.mbuf_out[port].array[k];
          rte_pktmbuf_free(pkt_to_free);
        }
      }
      lp->tx.mbuf_out[port].n_mbufs = 0;
      lp->tx.mbuf_out_flush[port] = 0;
    }
  }
}

static inline void app_lcore_io_tx_flush(struct app_lcore_params_io *lp) {
  uint16_t port;
  uint32_t i;
  
  for (i = 0; i < lp->tx.n_nic_ports; i++) {
    uint32_t n_pkts;
    
    port = lp->tx.nic_ports[i];
    if (likely((lp->tx.mbuf_out_flush[port] == 0) || (lp->tx.mbuf_out[port].n_mbufs == 0))) {
      lp->tx.mbuf_out_flush[port] = 1;
      continue;
    }
    
    n_pkts = rte_eth_tx_burst(port, 0, lp->tx.mbuf_out[port].array, (uint16_t) lp->tx.mbuf_out[port].n_mbufs);
    
    if (unlikely(n_pkts < lp->tx.mbuf_out[port].n_mbufs)) {
      uint32_t k;
      for (k = n_pkts; k < lp->tx.mbuf_out[port].n_mbufs; k++) {
        struct rte_mbuf *pkt_to_free = lp->tx.mbuf_out[port].array[k];
        rte_pktmbuf_free(pkt_to_free);
      }
    }
    
    lp->tx.mbuf_out[port].n_mbufs = 0;
    lp->tx.mbuf_out_flush[port] = 1;
  }
}

Hasher32<Tuple3> hasher[2] = {{(uint) rand()},
                              {(uint) rand()}};

static void app_lcore_main_loop_io() {
  uint32_t lcore = rte_lcore_id();
  struct app_lcore_params_io *lp = &app.lcore_params[lcore].io;
  uint32_t n_workers = app_get_lcores_worker();
  uint64_t i = 0;
  
  uint32_t bsz_rx_rd = app.burst_size_io_rx_read;
  uint32_t bsz_rx_wr = app.burst_size_io_rx_write;
  uint32_t bsz_tx_rd = app.burst_size_io_tx_read;
  uint32_t bsz_tx_wr = app.burst_size_io_tx_write;
  
  uint8_t pos_lb = app.pos_lb;
  
  for (;;) {
    if (APP_LCORE_IO_FLUSH && (unlikely(i == APP_LCORE_IO_FLUSH))) {
      if (likely(lp->rx.n_nic_queues > 0)) {
        app_lcore_io_rx_flush(lp, n_workers);
      }
      
      if (likely(lp->tx.n_nic_ports > 0)) {
        app_lcore_io_tx_flush(lp);
      }
      
      i = 0;
    }
    
    if (likely(lp->rx.n_nic_queues > 0)) {
      app_lcore_io_rx(lp, n_workers, bsz_rx_rd, bsz_rx_wr, pos_lb);
    }
    
    if (likely(lp->tx.n_nic_ports > 0)) {
      app_lcore_io_tx(lp, n_workers, bsz_tx_rd, bsz_tx_wr);
    }
    
    i++;
  }
}

static uint64_t cnt = 0;
static uint64_t inserted = 0;

static inline void app_lcore_worker(struct app_lcore_params_worker *lp, uint32_t bsz_rd, uint32_t bsz_wr) {
  for (uint32_t i = 0; i < lp->n_rings_in; i++) {
    struct rte_ring *ring_in = lp->rings_in[i];
    
    // copy pointers from ring to mbuf_in. always start at the beginning of the mbuf_in array. no need to reset the array after reading from it.
    int ret = rte_ring_sc_dequeue_bulk(ring_in, (void **) lp->mbuf_in.array, bsz_rd, nullptr);
    
    if (unlikely(ret == 0)) {
      continue;
    }

#if APP_WORKER_DROP_ALL_PACKETS
    for (j = 0; j < bsz_rd; j ++) {
      struct rte_mbuf *pkt = lp->mbuf_in.array[j];
      rte_pktmbuf_free(pkt);
    }

    continue;
#endif
    
    const int batch_size = 24;  // should be divided by 144
    MySimpleArray<uint8_t *> packets(batch_size);
    MySimpleArray<uint32_t> outPorts(batch_size);
    
    for (uint32_t base = 0; base < bsz_rd; base += batch_size) {
      for (uint32_t j = 0; j < batch_size; ++j) {
        APP_WORKER_PREFETCH1(packets[j] = (uint8_t *) lp->mbuf_in.array[base + j]);   // buffer
      }
      for (uint32_t j = 0; j < batch_size; ++j) {
        APP_WORKER_PREFETCH1(packets[j] = rte_pktmbuf_mtod((rte_mbuf *) packets[j], uint8_t * ));
      }
      for (uint32_t j = 0; j < batch_size; ++j) {
        struct ipv4_hdr *ipv4_hdr;
        struct tcp_hdr *tcp_hdr;
        
        uint32_t ipv4_dst, pos, ipv4_src;
        uint16_t tcp_port_dst, tcp_port_src;
        
        ipv4_hdr = (struct ipv4_hdr *) (packets[j] + sizeof(struct ether_hdr));
        ipv4_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);   // important !! switch ending
        ipv4_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
        
        tcp_hdr = (struct tcp_hdr *) (packets[j] + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));
        
        tcp_port_src = rte_be_to_cpu_16(tcp_hdr->src_port);
        tcp_port_dst = rte_be_to_cpu_16(tcp_hdr->dst_port);
        
        uint vipInd = ipv4_dst & VIP_MASK;
        Tuple3 tuple = {
          src: {ipv4_src, tcp_port_src},
          protocol: 6
        };
        
        if (app.ratio && cnt % app.ratio == 0) {
          vipInd = rand() & VIP_MASK;
          tuple = {
            src: {(uint) rand(), (uint16_t) rand()},
            protocol: 6
          };
        }
        
        cnt++;
        
        // Step 3: lookup connectionTracking table
        uint16_t htInd;
        uint64_t hash = hasher[0](tuple);
        hash |= uint64_t(hasher[1](tuple)) << 32;
        
        if (connTrackingTables[vipInd].Find(hash, htInd)) {
          // done with right dip
        } else {
          // Step 4: lookup consistent hashing table to get the dip and insert back
          inserted++;
//          if (inserted % (1024 * 1024) == 0) {
//          cout << "inserting: #" << human(inserted) << ", cnt: " << human(cnt) << endl;
//          cout << "ip parse: " << ipv4_src << "->" << ipv4_dst << " desired: 0 ~ unbounded -> 3539992576" << endl;
//          cout << "tcp parse: " << tcp_port_src << "->" << tcp_port_dst << " desired: 0/32767 -> 0/127" << endl;

//          if (inserted > 100 ) exit(0);
//          }
          
          htInd = ht[vipInd][hash & (HT_SIZE - 1)];
          const uint64_t *tmp = connTrackingTables[vipInd].Insert(hash, htInd);
          if (tmp != &hash) {
            cout << "full - rebuild: " << hash << " " << (tmp ? *tmp : 0) << endl;
            unordered_map<uint64_t, uint16_t, Hasher32<uint64_t>> map = connTrackingTables[vipInd].toMap();
            map.insert(make_pair(hash, htInd));
            
            bool succ = false;
            
            while (!succ) {
              succ = true;
              
              connTrackingTables[vipInd] = ControlPlaneCuckooMap<uint64_t, uint16_t, uint8_t, false>(
                connTrackingTables[vipInd].EntryCount() * 2);
              
              for (auto it = map.begin(); it != map.end(); ++it) {
                auto *result = connTrackingTables[vipInd].Insert(it->first, it->second);
                
                if (result != &it->first) {
                  succ = false;
                  break;
                }
              }
            }
          }
        }
        
        DIP &dip = dipPools[vipInd][ht[vipInd][htInd]];

//        cout << "Ht index: " << htInd << " DIP: " << dip << endl;
        
        outPorts[j] = (dip.addr.addr ^ tcp_port_src) & 1;
        
        uint32_t port = outPorts[j];
//        cout << ipv4_src << "@" << tcp_port_src << "-> #" << (ipv4_dst & VIP_MASK) << " lookup result: "
//             << dip.addr.addr << "@" << port << endl;
      }
      for (uint32_t j = 0; j < batch_size; ++j) {
      
      }
      for (uint32_t j = 0; j < batch_size; ++j) {
        uint32_t port = lp->mbuf_in.array[base +
                                          j]->port; //((base + j) / 1) & 1; // outPorts[j] // lp->mbuf_in.array[base + j]->port;
        uint pos = lp->mbuf_out[port].n_mbufs;
        lp->mbuf_out[port].array[pos++] = lp->mbuf_in.array[base + j];
        
        if (likely(pos < bsz_wr)) {
          lp->mbuf_out[port].n_mbufs = pos;
          continue;
        }
        // once when we have one bulk, enqueue them right away. if we don't, just buffer until a flush
        ret = rte_ring_sp_enqueue_bulk(lp->rings_out[port], (void **) lp->mbuf_out[port].array, bsz_wr, nullptr);

#if APP_STATS
        lp->rings_out_iters[port]++;
        if (likely(ret == bsz_wr)) {
          lp->rings_out_count[port]++;
        }
        if (lp->rings_out_iters[port] == APP_STATS) {
          printf("\t\tWorker %u out (NIC port %u): enq success rate = %.2f\n", (unsigned) lp->worker_id, port,
                 ((double) lp->rings_out_count[port]) / ((double) lp->rings_out_iters[port]));
          lp->rings_out_iters[port] = 0;
          lp->rings_out_count[port] = 0;
        }
#endif
        
        // FIXME current solution: if enqueue failed (The number of objects enqueued is 0), just delete the packets
        if (unlikely(ret == 0)) {
          uint32_t k;
          for (k = 0; k < bsz_wr; k++) {
            struct rte_mbuf *pkt_to_free = lp->mbuf_out[port].array[k];
            rte_pktmbuf_free(pkt_to_free);
          }
        }
        
        lp->mbuf_out[port].n_mbufs = 0;  // always clear the array after a bulk. no partial commit in poll mode
        lp->mbuf_out_flush[port] = outPorts[j];
      }
    }
//
//    // pre-fill the pipeline
//    APP_WORKER_PREFETCH1(rte_pktmbuf_mtod(lp->mbuf_in.array[0], unsigned char * ));  // fetch packet data to L1
//    APP_WORKER_PREFETCH0(lp->mbuf_in.array[1]); // fetch next mbuf meta data to L0
//
//    for (uint32_t j = 0; j < bsz_rd; j++) {
//      struct rte_mbuf *pkt;
//      struct ipv4_hdr *ipv4_hdr;
//      struct tcp_hdr *tcp_hdr;
//
//      uint32_t ipv4_dst, pos, ipv4_src;
//      uint16_t tcp_port_dst, tcp_port_src;
//      uint32_t port;
//
//      if (likely(j < bsz_rd - 1)) {
//        APP_WORKER_PREFETCH1(rte_pktmbuf_mtod(lp->mbuf_in.array[j + 1], unsigned char * ));
//      }
//      if (likely(j < bsz_rd - 2)) {
//        APP_WORKER_PREFETCH0(lp->mbuf_in.array[j + 2]);
//      }
//
//      pkt = lp->mbuf_in.array[j];
//
//      // from the packet start in the buffer, skip length of ether header, convert the address to ipv4 header. This address is prefetched.
//      // meta data is used to determine the offset
//      ipv4_hdr = rte_pktmbuf_mtod_offset(pkt, struct ipv4_hdr *, sizeof(struct ether_hdr));
//      ipv4_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);   // important !! switch ending
//      ipv4_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
//
////      cout << "ip parse: " << ipv4_src << "->" << ipv4_dst << " desired: 0 ~ unbounded -> 3539992576" << endl;
//
//      tcp_hdr = rte_pktmbuf_mtod_offset(pkt, struct tcp_hdr *, sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));
//
//      tcp_port_src = rte_be_to_cpu_16(tcp_hdr->src_port);
//      tcp_port_dst = rte_be_to_cpu_16(tcp_hdr->dst_port);
//
////      cout << "tcp parse: " << tcp_port_src << "->" << tcp_port_dst << " desired: 0/32767 -> 0/127" << endl;
//
//      // most cases we have a hit. send back if miss FIXME
////      if (unlikely(rte_lpm_lookup(lp->lpm_table, ipv4_dst, &port) != 0)) {
////        port = pkt->port;
////      }
//
//      DIP dip = concury_lookup(ipv4_dst & VIP_MASK, ipv4_src, tcp_port_src, 6);
//      port = (dip.addr.addr ^ tcp_port_src) & 1;
//
////      cout << ipv4_src << "@" << tcp_port_src << "-> #" << (ipv4_dst & VIP_MASK) << " lookup result: " << dip.addr.addr
////           << "@" << port << endl;
//
////      cout << port << " ";
//
//      pos = lp->mbuf_out[port].n_mbufs;  // just put the pointer of the packet to the
//
//      lp->mbuf_out[port].array[pos++] = pkt;
//      if (likely(pos < bsz_wr)) {
//        lp->mbuf_out[port].n_mbufs = pos;
//        continue;
//      }
//      // once when we have one bulk, enqueue them right away. if we don't, just buffer until a flush
//      ret = rte_ring_sp_enqueue_bulk(lp->rings_out[port], (void **) lp->mbuf_out[port].array, bsz_wr, nullptr);
//
//#if APP_STATS
//      lp->rings_out_iters[port]++;
//      if (likely(ret == bsz_wr)) {
//        lp->rings_out_count[port]++;
//      }
//      if (lp->rings_out_iters[port] == APP_STATS) {
//        printf("\t\tWorker %u out (NIC port %u): enq success rate = %.2f\n", (unsigned) lp->worker_id, port,
//               ((double) lp->rings_out_count[port]) / ((double) lp->rings_out_iters[port]));
//        lp->rings_out_iters[port] = 0;
//        lp->rings_out_count[port] = 0;
//      }
//#endif
//
//      // FIXME current solution: if enqueue failed (The number of objects enqueued is 0), just delete the packets
//      if (unlikely(ret == 0)) {
//        uint32_t k;
//        for (k = 0; k < bsz_wr; k++) {
//          struct rte_mbuf *pkt_to_free = lp->mbuf_out[port].array[k];
//          rte_pktmbuf_free(pkt_to_free);
//        }
//      }
//
//      lp->mbuf_out[port].n_mbufs = 0;  // always clear the array after a bulk. no partial commit in poll mode
//      lp->mbuf_out_flush[port] = 0;
//    }
  }
}

// in case some packets remained in the buffer for a long time
static inline void app_lcore_worker_flush(struct app_lcore_params_worker *lp) {
  uint32_t port;
  
  for (port = 0; port < APP_MAX_NIC_PORTS; port++) {
    int ret;
    
    if (unlikely(lp->rings_out[port] == nullptr)) {
      continue;
    }
    
    if (likely((lp->mbuf_out_flush[port] == 0) || (lp->mbuf_out[port].n_mbufs == 0))) {
      lp->mbuf_out_flush[port] = 1;
      continue;
    }
    
    ret = rte_ring_sp_enqueue_bulk(lp->rings_out[port], (void **) lp->mbuf_out[port].array, lp->mbuf_out[port].n_mbufs, nullptr);
    
    if (unlikely(ret == 0)) {
      uint32_t k;
      for (k = 0; k < lp->mbuf_out[port].n_mbufs; k++) {
        struct rte_mbuf *pkt_to_free = lp->mbuf_out[port].array[k];
        rte_pktmbuf_free(pkt_to_free);
      }
    }
    
    lp->mbuf_out[port].n_mbufs = 0;
    lp->mbuf_out_flush[port] = 1;
  }
}

static void app_lcore_main_loop_worker() {
  uint32_t lcore = rte_lcore_id();
  struct app_lcore_params_worker *lp = &app.lcore_params[lcore].worker;
  uint64_t i = 0;
  
  uint32_t bsz_rd = app.burst_size_worker_read;
  uint32_t bsz_wr = app.burst_size_worker_write;
  
  for (;;) {
    if (APP_LCORE_WORKER_FLUSH && (unlikely(i == APP_LCORE_WORKER_FLUSH))) {
      app_lcore_worker_flush(lp);
      i = 0;
    }
    
    app_lcore_worker(lp, bsz_rd, bsz_wr);
    
    i++;
  }
}

int app_lcore_main_loop(__attribute__((unused)) void *arg) {
  struct app_lcore_params *lp;
  unsigned lcore;
  
  lcore = rte_lcore_id();
  lp = &app.lcore_params[lcore];
  
  if (lp->type == e_APP_LCORE_IO) {
    printf("Logical core %u (I/O) main loop.\n", lcore);
    app_lcore_main_loop_io();
  }
  
  if (lp->type == e_APP_LCORE_WORKER) {
    printf("Logical core %u (worker %u) main loop.\n", lcore, (unsigned) lp->worker.worker_id);
    app_lcore_main_loop_worker();
  }
  
  return 0;
}

#pragma clang diagnostic pop
