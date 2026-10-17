# Builds AKENO NETWORK Native for the PS5 with the pinned ps5-payload-sdk and SDL2:
#
#   clients/ps5-native/scripts/fetch-deps.sh             # stb_truetype, Inter, mbedTLS, CA bundle (checksummed)
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk          # SDK commit pinned in .github/workflows/ps5-native.yml
#   clients/ps5-native/scripts/build-sdl-ps5.sh          # SDL2 ee4c47d, software renderer only
#   make -C clients/ps5-native/console                   # -> build-ps5/akeno-native.elf (+ .debug.elf)
#   make -C clients/ps5-native/console package ICON=icon0.png   # -> build-ps5/AKENONative/ (launcher folder)
#
# Nothing is sent to a console by this Makefile. The existing daemon (clients/ps5) is not touched:
# its sources are compiled here unchanged, into this separate ELF.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
PS5_ROOT    := $(abspath $(ROOT)/../ps5)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

THIRD_PARTY  ?= $(ROOT)/third_party
AKENO_TP     ?= $(PS5_ROOT)/third_party
SDL_PREFIX   ?= $(THIRD_PARTY)/sdl-ps5
BUILD        ?= $(ROOT)/build-ps5
OBJ          := $(BUILD)/obj
NAME         := akeno-native
ELF          := $(BUILD)/$(NAME).elf
DEBUG_ELF    := $(BUILD)/$(NAME).debug.elf
PACKAGE      := $(BUILD)/AKENONative
GIT_COMMIT   ?= $(shell git -C $(ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo dev)

ifeq ($(wildcard $(AKENO_TP)/mbedtls/library/ssl_tls.c),)
$(error mbedTLS is missing; run clients/ps5-native/scripts/fetch-deps.sh)
endif
ifeq ($(wildcard $(THIRD_PARTY)/stb_truetype.h),)
$(error stb_truetype is missing; run clients/ps5-native/scripts/fetch-deps.sh)
endif
ifeq ($(wildcard $(SDL_PREFIX)/lib/libSDL2.a),)
$(error SDL2 for the PS5 is missing; run clients/ps5-native/scripts/build-sdl-ps5.sh)
endif

MBEDTLS_SRC := $(wildcard $(AKENO_TP)/mbedtls/library/*.c)
CLIENT_SRC  := $(addprefix $(PS5_ROOT)/src/,buf.c json.c json_write.c crypto_util.c url.c backoff.c caps.c http.c ws.c \
               client.c realtime.c net_posix.c transport_mbedtls.c)
SHARED_SRC  := $(addprefix $(PS5_ROOT)/app/,audio_lab_pcm.c game_detect.c)
CORE_SRC    := $(addprefix $(ROOT)/src/core/,nav.c input.c audio_test.c redact.c log.c context.c logo.c png.c text.c ui.c \
               script.c account.c social.c image.c vector.c badges.c artwork.c)
APP_SRC     := $(addprefix $(ROOT)/src/app/,main.c app.c screens.c gfx.c net.c report.c ipc.c social.c images.c)
PLAT_SRC    := $(ROOT)/src/platform/platform_ps5.c
STB_SRC     := $(ROOT)/src/app/font_stb.c

MBEDTLS_OBJ := $(patsubst $(AKENO_TP)/mbedtls/library/%.c,$(OBJ)/mbedtls/%.o,$(MBEDTLS_SRC))
CLIENT_OBJ  := $(patsubst $(PS5_ROOT)/src/%.c,$(OBJ)/client/%.o,$(CLIENT_SRC))
SHARED_OBJ  := $(patsubst $(PS5_ROOT)/app/%.c,$(OBJ)/shared/%.o,$(SHARED_SRC))
CORE_OBJ    := $(patsubst $(ROOT)/src/core/%.c,$(OBJ)/core/%.o,$(CORE_SRC))
APP_OBJ     := $(patsubst $(ROOT)/src/app/%.c,$(OBJ)/app/%.o,$(APP_SRC))
PLAT_OBJ    := $(OBJ)/platform/platform_ps5.o
STB_OBJ     := $(OBJ)/third_party/font_stb.o
STBI_OBJ    := $(OBJ)/third_party/stb_image_impl.o

# Paths are mapped so the ELF (and the debug ELF) do not depend on where the repository is checked out.
COMMON_FLAGS := -O2 -g -std=c11 -DMBEDTLS_CONFIG_FILE='"akeno_mbedtls_config.h"' -I$(PS5_ROOT)/config \
                -isystem $(AKENO_TP)/mbedtls/include -ffile-prefix-map=$(REPO_ROOT)=. \
                -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
WARNINGS     := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror \
                -Wno-unreachable-code-generic-assoc
SDL_FLAGS    := -isystem $(SDL_PREFIX)/include/SDL2 -D_REENTRANT
AKN_FLAGS    := $(COMMON_FLAGS) $(WARNINGS) -DAKN_PLATFORM_PS5 -I$(PS5_ROOT)/include -I$(PS5_ROOT)/src -I$(PS5_ROOT)/app \
                -I$(ROOT)/src/core -I$(ROOT)/src/app -I$(ROOT)/src/platform -I$(THIRD_PARTY)
CLIENT_FLAGS := $(COMMON_FLAGS) -Wall -Wextra -Werror -Wno-unreachable-code-generic-assoc -I$(PS5_ROOT)/include \
                -I$(PS5_ROOT)/src \
                -DAKENO_CA_BUNDLE_PATH='"$(AKENO_TP)/cacert.pem"' \
                -DAKENO_CA_BUNDLE_NAME='"Mozilla CA bundle from certifi 2026.07.22"'
APP_DEFS     := -DAKN_BUILD_ID='"$(GIT_COMMIT)-ps5"' \
                -DAKN_FONT_REGULAR_PATH='"$(THIRD_PARTY)/inter/Inter-Regular.ttf"' \
                -DAKN_FONT_BOLD_PATH='"$(THIRD_PARTY)/inter/Inter-Bold.ttf"'
# SDL2's PS5 drivers need these system libraries; nothing else is linked.
LIBS         := $(SDL_PREFIX)/lib/libSDL2.a -lpthread -lSceSystemService -lSceUserService -lScePad -lSceAudioOut \
                -lSceVideoOut -lSceKeyboard -lSceImeDialog

.PHONY: all clean package
all: $(ELF)

$(OBJ)/mbedtls/%.o: $(AKENO_TP)/mbedtls/library/%.c $(PS5_ROOT)/config/akeno_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(OBJ)/client/%.o: $(PS5_ROOT)/src/%.c $(wildcard $(PS5_ROOT)/src/*.h) $(wildcard $(PS5_ROOT)/include/akeno/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CLIENT_FLAGS) -c $< -o $@

$(OBJ)/client/transport_mbedtls.o: $(AKENO_TP)/cacert.pem

$(OBJ)/shared/%.o: $(PS5_ROOT)/app/%.c $(wildcard $(PS5_ROOT)/app/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(AKN_FLAGS) -c $< -o $@

$(OBJ)/core/%.o: $(ROOT)/src/core/%.c $(wildcard $(ROOT)/src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(AKN_FLAGS) -c $< -o $@

$(OBJ)/app/%.o: $(ROOT)/src/app/%.c $(wildcard $(ROOT)/src/app/*.h) $(wildcard $(ROOT)/src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(AKN_FLAGS) $(SDL_FLAGS) $(APP_DEFS) -c $< -o $@

$(OBJ)/app/app.o: $(THIRD_PARTY)/inter/Inter-Regular.ttf $(THIRD_PARTY)/inter/Inter-Bold.ttf

$(PLAT_OBJ): $(PLAT_SRC) $(ROOT)/src/platform/akn_platform.h
	@mkdir -p $(dir $@)
	$(CC) $(AKN_FLAGS) -c $< -o $@

$(STB_OBJ): $(STB_SRC) $(THIRD_PARTY)/stb_truetype.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -g -std=c11 -w -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk \
	      $(SDL_FLAGS) -I$(THIRD_PARTY) -c $< -o $@

$(STBI_OBJ): $(ROOT)/src/third/stb_image_impl.c $(ROOT)/src/core/stb_image_akn.h $(THIRD_PARTY)/stb_image.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -g -std=c11 -w -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk \
	      -I$(THIRD_PARTY) -I$(ROOT)/src/core -c $< -o $@

$(DEBUG_ELF): $(APP_OBJ) $(PLAT_OBJ) $(CORE_OBJ) $(SHARED_OBJ) $(STB_OBJ) $(STBI_OBJ) $(CLIENT_OBJ) $(MBEDTLS_OBJ)
	$(CC) -o $@ $^ $(LIBS)

$(ELF): $(DEBUG_ELF)
	$(STRIP) -o $@ $<

# The websrv-format launcher folder: eboot.elf, homebrew.js and the AKENO icon.
package: $(ELF)
	@test -n "$(ICON)" -a -f "$(ICON)" || { echo "ICON=<icon0.png> is required (built by the PC tool make-icon)"; exit 1; }
	rm -rf $(PACKAGE)
	mkdir -p $(PACKAGE)/sce_sys
	cp $(ELF) $(PACKAGE)/eboot.elf
	cp $(ROOT)/package/homebrew.js $(PACKAGE)/homebrew.js
	cp $(ICON) $(PACKAGE)/sce_sys/icon0.png

clean:
	rm -rf $(BUILD)
