# Builds the AKENO NETWORK console app for the PS5 with the ps5-payload-sdk:
#
#   clients/ps5/scripts/fetch-deps.sh                 # pinned mbedTLS + CA bundle (checksummed)
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk       # pinned SDK, see .github/workflows/ps5.yml
#   make -C clients/ps5/console
#
# Output: clients/ps5/build-ps5/akeno-network-ps5.elf (stripped) and .debug.elf (with symbols).
# Nothing is sent to a console by this Makefile.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

THIRD_PARTY ?= $(ROOT)/third_party
BUILD       := $(ROOT)/build-ps5
OBJ         := $(BUILD)/obj
ELF         := $(BUILD)/akeno-network-ps5.elf
DEBUG_ELF   := $(BUILD)/akeno-network-ps5.debug.elf
GIT_COMMIT  ?= $(shell git -C $(ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo dev)

ifeq ($(wildcard $(THIRD_PARTY)/mbedtls/library/ssl_tls.c),)
$(error mbedTLS is missing; run clients/ps5/scripts/fetch-deps.sh)
endif

MBEDTLS_SRC := $(wildcard $(THIRD_PARTY)/mbedtls/library/*.c)
CLIENT_SRC  := $(addprefix $(ROOT)/src/,buf.c json.c json_write.c crypto_util.c url.c backoff.c caps.c http.c ws.c \
               client.c realtime.c net_posix.c transport_mbedtls.c)
APP_SRC     := $(addprefix $(ROOT)/app/,app.c app_routes.c ui_server.c ui_http.c ui_assets.c platform_ps5.c main.c)

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
                -DAKENO_BUILD_ID='"$(GIT_COMMIT)-ps5"'
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
