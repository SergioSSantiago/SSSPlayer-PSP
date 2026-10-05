TARGET = SSSPlayer
OBJS = src/main.o

# PSPSDK Makefile fragment (requires pspdev on PATH)
PSPSDK ?= $(shell psp-config --pspsdk-path 2>/dev/null)

CFLAGS = -O2 -G0 -Wall -Iinclude
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LIBS = -lpspgu -lpspgum -lpspdisplay -lpspaudio -lpspctrl -lpsppower -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SSSPlayer
PSP_EBOOT_ICON =

ifeq ($(PSPSDK),)
$(warning PSPSDK not found. Install pspdev, then: export PATH=\"$$PSPDEV/bin:$$PATH\")
all:
	@echo "PSPSDK missing — cannot build yet. See README.md"
	@exit 1
else
include $(PSPSDK)/lib/build.mak
endif

.PHONY: clean-extra
clean-extra:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).prx EBOOT.PBP PARAM.SFO
