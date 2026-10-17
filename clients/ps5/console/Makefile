# Builds the AKENO NETWORK console app for the PS5 with the ps5-payload-sdk:
#
#   clients/ps5/scripts/fetch-deps.sh                 # pinned mbedTLS + CA bundle (checksummed)
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk       # pinned SDK, see .github/workflows/ps5.yml
#   make -C clients/ps5/console
#
# Output: clients/ps5/build-ps5/akeno-network-ps5.elf (stripped) and .debug.elf (with symbols).
# Nothing is sent to a console by this Makefile.
#
#   make -C clients/ps5/console EXPERIMENTAL_AUDIO=1
#
# builds the separate, experimental audio lab instead (PS5 voice research, see
# docs/PS5_AUDIO_RESEARCH.md): clients/ps5/build-ps5-audiolab/akeno-network-ps5-audiolab-v3.elf.
# The release ELF never contains the audio lab.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

THIRD_PARTY ?= $(ROOT)/third_party
EXPERIMENTAL_AUDIO ?= 0
ifeq ($(EXPERIMENTAL_AUDIO),1)
BUILD       := $(ROOT)/build-ps5-audiolab
NAME        := akeno-network-ps5-audiolab-v3
LAB_SRC     := audio_lab.c audio_lab_pcm.c audio_lab_state.c
LAB_FLAGS   := -DAKENO_EXPERIMENTAL_AUDIO
BUILD_KIND  := ps5-audiolab-v3
else
BUILD       := $(ROOT)/build-ps5
NAME        := akeno-network-ps5
LAB_SRC     :=
LAB_FLAGS   :=
BUILD_KIND  := ps5
endif
OBJ         := $(BUILD)/obj
ELF         := $(BUILD)/$(NAME).elf
DEBUG_ELF   := $(BUILD)/$(NAME).debug.elf
GIT_COMMIT  ?= $(shell git -C $(ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo dev)

ifeq ($(wildcard $(THIRD_PARTY)/mbedtls/library/ssl_tls.c),)
$(error mbedTLS is missing; run clients/ps5/scripts/fetch-deps.sh)
endif

MBEDTLS_SRC := $(wildcard $(THIRD_PARTY)/mbedtls/library/*.c)
CLIENT_SRC  := $(addprefix $(ROOT)/src/,buf.c json.c json_write.c crypto_util.c url.c backoff.c caps.c http.c ws.c \
               client.c realtime.c net_posix.c transport_mbedtls.c)
APP_SRC     := $(addprefix $(ROOT)/app/,app.c app_routes.c activity.c game_detect.c quick_access.c shortcut.c ui_server.c ui_http.c \
               ui_assets.c platform_ps5.c main.c $(LAB_SRC))

MBEDTLS_OBJ := $(patsubst $(THIRD_PARTY)/mbedtls/library/%.c,$(OBJ)/mbedtls/%.o,$(MBEDTLS_SRC))
CLIENT_OBJ  := $(patsubst $(ROOT)/src/%.c,$(OBJ)/client/%.o,$(CLIENT_SRC))
APP_OBJ     := $(patsubst $(ROOT)/app/%.c,$(OBJ)/app/%.o,$(APP_SRC))

COMMON_FLAGS := -O2 -g -std=c11 -DMBEDTLS_CONFIG_FILE='"akeno_mbedtls_config.h"' \
                -I$(ROOT)/config -isystem $(THIRD_PARTY)/mbedtls/include
WARNINGS     := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Werror \
                -Wno-unreachable-code-generic-assoc
AKENO_FLAGS  := $(COMMON_FLAGS) $(WARNINGS) -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/app \
                -DAKENO_CA_BUNDLE_PATH='"$(THIRD_PARTY)/cacert.pem"' \
                -DAKENO_CA_BUNDLE_NAME='"Mozilla CA bundle from certifi 2026.07.22"' \
                -DAKENO_UI_DIR='"$(ROOT)/app/web"' \
                -DAKENO_BUILD_ID='"$(GIT_COMMIT)-$(BUILD_KIND)"' $(LAB_FLAGS)
LIBS         := -lpthread -lSceSystemService -lSceUserService

.PHONY: all clean
all: $(ELF)

$(OBJ)/mbedtls/%.o: $(THIRD_PARTY)/mbedtls/library/%.c $(ROOT)/config/akeno_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(OBJ)/client/%.o: $(ROOT)/src/%.c $(wildcard $(ROOT)/src/*.h) $(wildcard $(ROOT)/include/akeno/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(AKENO_FLAGS) -c $< -o $@

$(OBJ)/client/transport_mbedtls.o: $(THIRD_PARTY)/cacert.pem

$(OBJ)/app/%.o: $(ROOT)/app/%.c $(wildcard $(ROOT)/app/*.h) $(wildcard $(ROOT)/src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(AKENO_FLAGS) -c $< -o $@

$(OBJ)/app/ui_assets.o: $(wildcard $(ROOT)/app/web/*)

$(DEBUG_ELF): $(APP_OBJ) $(CLIENT_OBJ) $(MBEDTLS_OBJ)
	$(CC) -o $@ $^ $(LIBS)

$(ELF): $(DEBUG_ELF)
	$(STRIP) -o $@ $<

clean:
	rm -rf $(BUILD)
