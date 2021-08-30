#!/usr/bin/env bash
make -j8 && gdb -ex=r --args ./build/dpdk -l 0-7 -n 4 --  --rx "(0,0,0),(1,0,1)" --tx "(0,0),(1,1)" --w "2,3,4,5,6,7" --Nk $1
