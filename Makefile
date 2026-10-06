TARGET = SSSPlayer
OBJS = src/path.o src/browser.o src/player.o src/ui.o src/mp4.o src/video.o src/mpeg_nal.o src/display_bright.o src/main.o

# Prefer psp-config already on PATH. Otherwise use ~/pspdev.
ifeq ($(shell psp-config --pspsdk-path 2>/dev/null),)
  ifneq ($(wildcard $(HOME)/pspdev/bin/psp-config),)
    PSPDEV ?= $(HOME)/pspdev
    export PSPDEV
    export PATH := $(PSPDEV)/bin:$(PATH)
  endif
endif

PSPSDK ?= $(shell psp-config --pspsdk-path 2>/dev/null)
ifeq ($(PSPDEV),)
  PSPDEV := $(shell psp-config --pspdev-path 2>/dev/null)
  export PSPDEV
endif

CFLAGS = -O2 -G0 -Wall -Wextra -Iinclude
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LIBS = -lpspgu -lpspmp3 -lpspaudiocodec -lpspaudio -lpsppower -lpsputility -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SSSPlayer
PSP_EBOOT_ICON = assets/icon0.png
PSP_EBOOT_PIC1 = assets/pic1.png
PSP_FW_VERSION = 600

ifeq ($(PSPSDK),)
$(warning PSPSDK not found. Install pspdev, then: export PATH="$$PSPDEV/bin:$$PATH")
all:
	@echo "PSPSDK missing — cannot build yet. See README.md"
	@exit 1
else
include $(PSPSDK)/lib/build.mak
endif
