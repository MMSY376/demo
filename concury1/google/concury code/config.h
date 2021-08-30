#ifndef VIP_NUM
#define VIP_NUM (128)                       // must be power of 2
#endif

#define VIP_MASK (VIP_NUM - 1)

#ifndef CONN_NUM
#define CONN_NUM (128 * 128 * VIP_NUM)    // must be multiple of VIP_NUM
#endif


#ifndef DIP_NUM
#define DIP_NUM     (32*VIP_NUM)
#define DIP_NUM_MIN 8
#define DIP_NUM_MAX 64
#endif

#define LOG_INTERVAL (50 * 1000000)       // must be multiple of 1E6

#define HT_SIZE (512)                    // must be power of 2
#define STO_NUM (CONN_NUM)                // simulate control plane
