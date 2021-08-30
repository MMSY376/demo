#!/usr/bin/env bash
apt install google-perftools libgoogle-perftools-dev tau
make -j8 && rm -f dpdk.prof && env CPUPROFILE=dpdk.prof ./build/dpdk -l 0-7 -n 4 --  --rx "(0,0,0),(1,0,1)" --tx "(0,0),(1,1)" --w "2,3,4,5,6,7" --lpm "211.0.0.64/26=>0; 211.0.0.0/26=>1;" --pos-lb 29 ; google-pprof --text ./build/dpdk dpdk.prof