cmake_minimum_required(VERSION 3.5)
project(OthelloLB)

set(CMAKE_CXX_STANDARD 17)
add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_DEBUG -mavx -maes")
find_package(Threads)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        /mnt/d/projects/dpdk-18.11/lib/librte_acl/
        /mnt/d/projects/dpdk-18.11/lib/librte_bbdev/
        /mnt/d/projects/dpdk-18.11/lib/librte_bitratestats/
        /mnt/d/projects/dpdk-18.11/lib/librte_bpf/
        /mnt/d/projects/dpdk-18.11/lib/librte_cfgfile/
        /mnt/d/projects/dpdk-18.11/lib/librte_cmdline/
        /mnt/d/projects/dpdk-18.11/lib/librte_compat/
        /mnt/d/projects/dpdk-18.11/lib/librte_compressdev/
        /mnt/d/projects/dpdk-18.11/lib/librte_cryptodev/
        /mnt/d/projects/dpdk-18.11/lib/librte_distributor/
        /mnt/d/projects/dpdk-18.11/lib/librte_eal/common/include/
        /mnt/d/projects/dpdk-18.11/lib/librte_eal/common/include/arch/x86
        /mnt/d/projects/dpdk-18.11/lib/librte_efd/
        /mnt/d/projects/dpdk-18.11/lib/librte_ethdev/
        /mnt/d/projects/dpdk-18.11/lib/librte_eventdev/
        /mnt/d/projects/dpdk-18.11/lib/librte_flow_classify/
        /mnt/d/projects/dpdk-18.11/lib/librte_gro/
        /mnt/d/projects/dpdk-18.11/lib/librte_gso/
        /mnt/d/projects/dpdk-18.11/lib/librte_hash/
        /mnt/d/projects/dpdk-18.11/lib/librte_ip_frag/
        /mnt/d/projects/dpdk-18.11/lib/librte_jobstats/
        /mnt/d/projects/dpdk-18.11/lib/librte_kni/
        /mnt/d/projects/dpdk-18.11/lib/librte_kvargs/
        /mnt/d/projects/dpdk-18.11/lib/librte_latencystats/
        /mnt/d/projects/dpdk-18.11/lib/librte_lpm/
        /mnt/d/projects/dpdk-18.11/lib/librte_mbuf/
        /mnt/d/projects/dpdk-18.11/lib/librte_member/
        /mnt/d/projects/dpdk-18.11/lib/librte_mempool/
        /mnt/d/projects/dpdk-18.11/lib/librte_meter/
        /mnt/d/projects/dpdk-18.11/lib/librte_metrics/
        /mnt/d/projects/dpdk-18.11/lib/librte_net/
        /mnt/d/projects/dpdk-18.11/lib/librte_pci/
        /mnt/d/projects/dpdk-18.11/lib/librte_pdump/
        /mnt/d/projects/dpdk-18.11/lib/librte_pipeline/
        /mnt/d/projects/dpdk-18.11/lib/librte_port/
        /mnt/d/projects/dpdk-18.11/lib/librte_power/
        /mnt/d/projects/dpdk-18.11/lib/librte_rawdev/
        /mnt/d/projects/dpdk-18.11/lib/librte_reorder/
        /mnt/d/projects/dpdk-18.11/lib/librte_ring/
        /mnt/d/projects/dpdk-18.11/lib/librte_sched/
        /mnt/d/projects/dpdk-18.11/lib/librte_security/
        /mnt/d/projects/dpdk-18.11/lib/librte_table/
        /mnt/d/projects/dpdk-18.11/lib/librte_timer/
        /mnt/d/projects/dpdk-18.11/lib/librte_vhost/
        
        /mnt/d/projects/dpdk-18.11/x86_64-native-linuxapp-gcc/include
)

#set(COMMON_SOURCE_FILES
#        farmhash/farmhash.cc
#        md5/md5.cpp
#        common.cpp)

#set(HEADER_FILES
#        common.h
#        hash.h
#        farmhash/farmhash.h
#        lfsr64.h
#        disjointset.h
#        Othello/control_plane_othello.h
#        Othello/data_plane_othello.h
#        CuckooPresized/macros.h
#        CuckooPresized/types.h
#        CuckooPresized/control_plane_cuckoo_map.h)

#add_executable(Concury
#        ${HEADER_FILES}
#        ${COMMON_SOURCE_FILES}
#        concury.common.cpp
#        concury.cpp)

#add_executable(Maglevx
#        ${HEADER_FILES}
#        ${COMMON_SOURCE_FILES}
#        maglevx.cpp)

#add_executable(SilkRoad
#        ${HEADER_FILES}
#        ${COMMON_SOURCE_FILES}
#        silkroad.cpp)

add_executable(DPDK
        dpdk/farmhash.cpp
        dpdk/farmhash.h
        dpdk/concury.cpp
        dpdk/common.cpp
        dpdk/common.h
        dpdk/Othello/control_plane_othello.h
        dpdk/Othello/data_plane_othello.h
        dpdk/main.h
        dpdk/main.cpp
        dpdk/config.cpp
        dpdk/init.cpp
        dpdk/runtime.cpp
        dpdk/cuckoolb/maglevx.cpp
        dpdk/cuckoolb/maglev.h
        dpdk/cuckoolb/runtime.cpp
        )

#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)
#target_link_libraries(Concury Threads::Threads)
#target_link_libraries(Maglevx Threads::Threads)
#target_link_libraries(SilkRoad Threads::Threads)

#set(Gperftools_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake/")
#find_package(Gperftools REQUIRED)

#target_link_libraries(Maglevx ${GPERFTOOLS_PROFILER})
#target_link_libraries(Concury ${GPERFTOOLS_PROFILER})
#target_link_libraries(SilkRoad ${GPERFTOOLS_PROFILER})
