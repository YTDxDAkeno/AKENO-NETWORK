# Builds the AKENO Voice Lab (EXPERIMENTAL background audio diagnostics) for the PS5:
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk     # SDK commit pinned in .github/workflows/ps5-native.yml
#   make -C clients/ps5-voicelab/console            # -> build-ps5/akeno-voicelab.elf
#   clients/ps5-voicelab/console/check-voicelab.sh clients/ps5-voicelab/build-ps5
#
# It is a payload of its own: the AKENO daemon (clients/ps5) and AKENO Native are not changed.
# Nothing is sent to a console by this Makefile.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
PS5_ROOT    := $(abspath $(ROOT)/../ps5)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD       ?= $(ROOT)/build-ps5
OBJ         := $(BUILD)/obj
ELF         := $(BUILD)/akeno-voicelab.elf
DEBUG_ELF   := $(BUILD)/akeno-voicelab.debug.elf

LAB_SRC     := $(addprefix $(ROOT)/src/,main.c lab.c lab_pcm.c lab_net.c backend_ps5.c)
CLIENT_SRC  := $(addprefix $(PS5_ROOT)/src/,json.c buf.c)
LAB_OBJ     := $(patsubst $(ROOT)/src/%.c,$(OBJ)/lab/%.o,$(LAB_SRC))
CLIENT_OBJ  := $(patsubst $(PS5_ROOT)/src/%.c,$(OBJ)/client/%.o,$(CLIENT_SRC))

CFLAGS_LAB  := -O2 -g -std=c11 -DAKN_LAB_PS5 -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef \
               -Wpointer-arith -Werror -Wno-unreachable-code-generic-assoc -I$(ROOT)/src -I$(PS5_ROOT)/include \
               -I$(PS5_ROOT)/src -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
LIBS        := -lpthread -lSceSystemService -lSceUserService -lSceAudioOut

.PHONY: all clean
all: $(ELF)

$(OBJ)/lab/%.o: $(ROOT)/src/%.c $(wildcard $(ROOT)/src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LAB) -c $< -o $@

$(OBJ)/client/%.o: $(PS5_ROOT)/src/%.c $(wildcard $(PS5_ROOT)/src/*.h) $(wildcard $(PS5_ROOT)/include/akeno/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LAB) -c $< -o $@

$(DEBUG_ELF): $(LAB_OBJ) $(CLIENT_OBJ)
	$(CC) -o $@ $^ $(LIBS)

$(ELF): $(DEBUG_ELF)
	$(STRIP) -o $@ $<

clean:
	rm -rf $(BUILD)

# The websrv-format launcher folder: akeno-voicelab.elf and homebrew.js (ICON= optional).
PACKAGE := $(BUILD)/AKENOVoiceLab
.PHONY: package
package: $(ELF)
	rm -rf $(PACKAGE)
	mkdir -p $(PACKAGE)
	cp $(ELF) $(PACKAGE)/akeno-voicelab.elf
	cp $(ROOT)/package/homebrew.js $(PACKAGE)/homebrew.js
	if [ -n "$(ICON)" ]; then mkdir -p $(PACKAGE)/sce_sys && cp $(ICON) $(PACKAGE)/sce_sys/icon0.png; fi
