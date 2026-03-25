BUILD_MODE ?= elf
HOST ?= host
ASSET_DEVICE ?= host
ASSET_ROOT ?=

EE_BIN = ps2jam.elf

EE_INCS += -I$(CURDIR)/include
EE_INCS += -I$(GSKIT)/include
EE_CXXFLAGS += -std=c++17 -O2 -G0 -Wall -Wextra
EE_CXXFLAGS += -DPS2_ASSET_DEVICE=\"$(ASSET_DEVICE)\"
EE_CXXFLAGS += -DPS2_ASSET_ROOT=\"$(ASSET_ROOT)\"
EE_LDFLAGS += -L$(GSKIT)/lib

EE_OBJS = \
	src/main.o \
	src/atlas2d/AtlasPack.o \
	src/platform/asset_path.o \
	src/engine/engine.o

EE_LIBS += -lgskit -ldmakit -lc -lstdc++

ifeq ($(BUILD_MODE),erl)
$(error BUILD_MODE=erl is no longer supported by this project because the installed ps2sdk does not provide erl-loader.elf)
endif

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

run: all
	ps2client execee $(HOST):$(EE_BIN)

reset:
	ps2client reset

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
