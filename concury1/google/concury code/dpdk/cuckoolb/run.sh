#!/usr/bin/env bash
make -j8 && gdb -ex=r --args ./build/dpdk -l 0-7 -n 4 -b 0000:07:00.0 -b 0000:07:00.1 -b 0000:03:00.0 --  --rx "(0,0,0),(0,1,1)" --tx "(0,0),(0,1)" --w "2,3,4,5,6,7" --ratio 256
