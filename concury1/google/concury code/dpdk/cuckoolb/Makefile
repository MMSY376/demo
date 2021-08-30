# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation


RTE_SDK ?= /opt/dpdk-18.11

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = dpdk

#LDLIBS += -L/root/libnids-1.24/src/ /root/libnet-1.1.6/src/.libs/libnet.so.1 -lnids -lglib-2.0 -lgthread-2.0 /root/libpcap/release/libpcap.a

# all source are stored in SRCS-y
SRCS-y := ../main.cpp ../config.cpp ../init.cpp ../common.cpp ../farmhash.cpp maglevx.cpp runtime.cpp

COMMON_FLAGS := -Ofast -fmax-errors=1 -ggdb -w -DNDEBUG -march=native -mavx -maes
CFLAGS += $(COMMON_FLAGS)
CPPFLAGS += -std=gnu++17 $(COMMON_FLAGS)
#CFLAGS += $(WERROR_FLAGS) -Wno-error -Wno-unused-function
LDFLAGS += -lstdc++ -lprofiler
include $(RTE_SDK)/mk/rte.extapp.mk
