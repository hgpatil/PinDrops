# Build this example with an SDE kit:
#   make SDE_BUILD_KIT=/path/to/sde-external-10.13.1-... tools apps

ifndef SDE_BUILD_KIT
$(error SDE_BUILD_KIT must point to an SDE kit containing pinkit and sde64)
endif
PIN_ROOT := $(SDE_BUILD_KIT)/pinkit
SDE_ROOT := $(PIN_ROOT)/sde-example
PINPLAY_ROOT := $(PIN_ROOT)/pinplay
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config

include $(CONFIG_ROOT)/makefile.config

TOOL_ROOTS := mt_address_translation
APP_ROOTS := mt_address_translation_target

TOOL_CXXFLAGS := -I$(SDE_ROOT)/include $(TOOL_CXXFLAGS)
TOOL_CXXFLAGS += -DSDE_INIT -DPINPLAY -I$(PINPLAY_ROOT)/include
TOOL_CXXFLAGS += -DPIN_RT -D_LIBCPP_HAS_MUSL_LIBC -DPIN_DISABLE_CRT_REG_DEF
TOOL_CXXFLAGS += -Wno-sign-compare
TOOL_LPATHS += -L$(SDE_ROOT)/lib/$(TARGET) -L$(PINPLAY_ROOT)/$(TARGET)
TOOL_LPATHS += -lpinplay -lsde -lpinplay -lsde -lbz2 -lzlib

APP_CXXFLAGS += -O0 -g -gdwarf-4 -fno-omit-frame-pointer -pthread
APP_LDFLAGS += -no-pie -pthread

include $(TOOLS_ROOT)/Config/makefile.default.rules
