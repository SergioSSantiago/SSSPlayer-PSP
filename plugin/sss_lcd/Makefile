TARGET = sss_lcd
OBJS = main.o exports.o

BUILD_PRX = 1
PRX_EXPORTS = exports.exp
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

PSP_FW_VERSION = 600
CFLAGS = -O2 -G0 -Wall -fno-builtin-printf
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LDFLAGS =

LIBS =

ifeq ($(shell psp-config --pspsdk-path 2>/dev/null),)
  ifneq ($(wildcard $(HOME)/pspdev/bin/psp-config),)
    PSPDEV ?= $(HOME)/pspdev
    export PSPDEV
    export PATH := $(PSPDEV)/bin:$(PATH)
  endif
endif

PSPSDK ?= $(shell psp-config --pspsdk-path 2>/dev/null)
include $(PSPSDK)/lib/build_prx.mak
