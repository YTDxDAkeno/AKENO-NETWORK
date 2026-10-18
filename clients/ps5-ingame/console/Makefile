# Builds the EXPERIMENTAL AKENO In-Game Companion OnionHEN plugin (AKEN00001.elf) with the pinned
# ps5-payload-sdk and the pinned OnionHEN plugin SDK (docs/IN_GAME_ACCESS.md):
#
#   clients/ps5-ingame/scripts/fetch-deps.sh
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
#   make -C clients/ps5-ingame/console      # -> build-ps5/AKEN00001.elf (+ .debug.elf), descriptor checked
#
# Nothing is sent to a console. The plugin links the OnionHEN SDK runtime (GPLv3) and the PS5 SDK
# runtime (GPLv3+): the ELF is a separate GPLv3 component, shipped only in the experimental package.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
NATIVE_ROOT := $(abspath $(ROOT)/../ps5-native)
PS5_ROOT    := $(abspath $(ROOT)/../ps5)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ONION_SDK  ?= $(ROOT)/third_party/onion-sdk
NATIVE_TP  ?= $(NATIVE_ROOT)/third_party
BUILD      ?= $(ROOT)/build-ps5
OBJ        := $(BUILD)/obj
PLUGIN_ID  := AKEN00001
ELF        := $(BUILD)/$(PLUGIN_ID).elf
DEBUG_ELF  := $(BUILD)/$(PLUGIN_ID).debug.elf

ifeq ($(wildcard $(ONION_SDK)/runtime/ui.c),)
$(error The OnionHEN plugin SDK is missing; run clients/ps5-ingame/scripts/fetch-deps.sh)
endif

ONION_SRC  := $(wildcard $(ONION_SDK)/runtime/*.c)
SHARED_SRC := $(addprefix $(NATIVE_ROOT)/src/core/,social.c chat.c party.c account.c image.c text.c) $(NATIVE_ROOT)/src/app/ipc.c
CLIENT_SRC := $(addprefix $(PS5_ROOT)/src/,json.c buf.c)
OWN_SRC    := $(addprefix $(ROOT)/src/,companion.c fetch.c ui_doc.c plugin_main.c)

ONION_OBJ  := $(patsubst $(ONION_SDK)/runtime/%.c,$(OBJ)/onion/%.o,$(ONION_SRC))
SHARED_OBJ := $(patsubst $(NATIVE_ROOT)/src/%.c,$(OBJ)/native/%.o,$(SHARED_SRC))
CLIENT_OBJ := $(patsubst $(PS5_ROOT)/src/%.c,$(OBJ)/client/%.o,$(CLIENT_SRC))
OWN_OBJ    := $(patsubst $(ROOT)/src/%.c,$(OBJ)/own/%.o,$(OWN_SRC))
STBI_OBJ   := $(OBJ)/third_party/stb_image_impl.o

COMMON  := -O2 -g -std=c11 -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
WARN    := -Wall -Wextra -Werror -Wno-unreachable-code-generic-assoc
INCS    := -I$(ROOT)/src -I$(NATIVE_ROOT)/src/core -I$(NATIVE_ROOT)/src/app -I$(PS5_ROOT)/include -I$(PS5_ROOT)/src -I$(ONION_SDK)/include
LIBS    := -lpthread

.PHONY: all clean
all: $(ELF)

$(OBJ)/onion/%.o: $(ONION_SDK)/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -w -D_DEFAULT_SOURCE -I$(ONION_SDK)/include -I$(ONION_SDK)/runtime -c $< -o $@

$(OBJ)/native/%.o: $(NATIVE_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(WARN) $(INCS) -c $< -o $@

$(OBJ)/client/%.o: $(PS5_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(WARN) -I$(PS5_ROOT)/include -I$(PS5_ROOT)/src -c $< -o $@

$(OBJ)/own/%.o: $(ROOT)/src/%.c $(wildcard $(ROOT)/src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(WARN) -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef $(INCS) -c $< -o $@

$(STBI_OBJ): $(NATIVE_ROOT)/src/third/stb_image_impl.c $(NATIVE_TP)/stb_image.h
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -w -I$(NATIVE_TP) -I$(NATIVE_ROOT)/src/core -c $< -o $@

$(DEBUG_ELF): $(OWN_OBJ) $(SHARED_OBJ) $(CLIENT_OBJ) $(ONION_OBJ) $(STBI_OBJ)
	$(CC) -o $@ $^ $(LIBS)

# Keep the OnionHEN descriptor section when stripping, then check it as OnionHEN will.
$(ELF): $(DEBUG_ELF)
	$(STRIP) --keep-section=.onion_plugin -o $@ $<
	python3 -I $(ONION_SDK)/tools/inspect_plugin.py $@ --expect-id $(PLUGIN_ID) --expect-version 1.00

clean:
	rm -rf $(BUILD)
