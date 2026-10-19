# Builds AKENO Party Voice (EXPERIMENTAL background voice service) for the PS5 with the pinned SDK:
#
#   clients/ps5-native/scripts/fetch-deps.sh          # libopus, mbedTLS, CA bundle, stb_image (checksummed)
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk       # SDK commit pinned in .github/workflows/ps5-voice.yml
#   make -C clients/ps5-partyvoice/console            # -> build-ps5/akeno-party-voice.elf (+ .debug.elf)
#   clients/ps5-partyvoice/console/check-partyvoice.sh clients/ps5-partyvoice/build-ps5
#
# A payload of its own: the AKENO daemon, AKENO Native, the plugin and the onionHEN host are not
# changed. Nothing is sent to a console by this Makefile.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
PS5_ROOT    := $(abspath $(ROOT)/../ps5)
NATIVE_ROOT := $(abspath $(ROOT)/../ps5-native)
LAB_ROOT    := $(abspath $(ROOT)/../ps5-voicelab)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

NATIVE_TP   ?= $(NATIVE_ROOT)/third_party
AKENO_TP    ?= $(PS5_ROOT)/third_party
BUILD       ?= $(ROOT)/build-ps5
OBJ         := $(BUILD)/obj
NAME        := akeno-party-voice
ELF         := $(BUILD)/$(NAME).elf
DEBUG_ELF   := $(BUILD)/$(NAME).debug.elf
GIT_COMMIT  ?= $(shell git -C $(ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo dev)

ifeq ($(wildcard $(AKENO_TP)/mbedtls/library/ssl_tls.c),)
$(error mbedTLS is missing; run clients/ps5-native/scripts/fetch-deps.sh)
endif
ifeq ($(wildcard $(NATIVE_TP)/opus/include/opus.h),)
$(error libopus is missing; run clients/ps5-native/scripts/fetch-deps.sh)
endif

OWN_SRC     := $(addprefix $(ROOT)/src/,pv_main.c pv_engine.c pv_net.c pv_audio.c pv_status.c pv_control.c pv_backend_ps5.c)
NATIVE_SRC  := $(addprefix $(NATIVE_ROOT)/src/core/,voice.c voice_codec.c voice_dsp.c voice_proto.c jitter.c account.c social.c chat.c \
               image.c text.c context.c) $(NATIVE_ROOT)/src/app/ipc.c
LAB_SRC     := $(LAB_ROOT)/src/lab_pcm.c
CLIENT_SRC  := $(addprefix $(PS5_ROOT)/src/,buf.c json.c json_write.c crypto_util.c url.c http.c ws.c net_posix.c transport_mbedtls.c)
UIHTTP_SRC  := $(PS5_ROOT)/app/ui_http.c
GAMEDET_SRC := $(PS5_ROOT)/app/game_detect.c
MBEDTLS_SRC := $(wildcard $(AKENO_TP)/mbedtls/library/*.c)
OPUS_DIR    := $(NATIVE_TP)/opus
OPUS_SRC    := $(addprefix $(OPUS_DIR)/,$(shell cat $(NATIVE_ROOT)/cmake/opus_sources.txt))

OWN_OBJ     := $(patsubst $(ROOT)/src/%.c,$(OBJ)/pv/%.o,$(OWN_SRC))
NATIVE_OBJ  := $(patsubst $(NATIVE_ROOT)/src/%.c,$(OBJ)/native/%.o,$(NATIVE_SRC))
LAB_OBJ     := $(OBJ)/lab/lab_pcm.o
CLIENT_OBJ  := $(patsubst $(PS5_ROOT)/src/%.c,$(OBJ)/client/%.o,$(CLIENT_SRC))
UIHTTP_OBJ  := $(OBJ)/client/ui_http.o
GAMEDET_OBJ := $(OBJ)/client/game_detect.o
MBEDTLS_OBJ := $(patsubst $(AKENO_TP)/mbedtls/library/%.c,$(OBJ)/mbedtls/%.o,$(MBEDTLS_SRC))
OPUS_OBJ    := $(patsubst $(OPUS_DIR)/%.c,$(OBJ)/opus/%.o,$(OPUS_SRC))
STBI_OBJ    := $(OBJ)/third_party/stb_image_impl.o

# Paths are mapped so the ELF does not depend on where the repository is checked out.
COMMON_FLAGS := -O2 -g -std=c11 -DMBEDTLS_CONFIG_FILE='"akeno_mbedtls_config.h"' -I$(PS5_ROOT)/config \
                -isystem $(AKENO_TP)/mbedtls/include -ffile-prefix-map=$(REPO_ROOT)=. \
                -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
WARNINGS     := -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror \
                -Wno-unreachable-code-generic-assoc
PV_FLAGS     := $(COMMON_FLAGS) $(WARNINGS) -DPV_PS5 -DAKENO_NATIVE_IPC -DPV_BUILD_ID='"$(GIT_COMMIT)-ps5"' -I$(ROOT)/src \
                -I$(PS5_ROOT)/include -I$(PS5_ROOT)/src -I$(PS5_ROOT)/app -I$(NATIVE_ROOT)/src/core -I$(NATIVE_ROOT)/src/app \
                -I$(LAB_ROOT)/src -isystem $(OPUS_DIR)/include
CLIENT_FLAGS := $(COMMON_FLAGS) -Wall -Wextra -Werror -Wno-unreachable-code-generic-assoc -DAKENO_NATIVE_IPC -I$(PS5_ROOT)/include \
                -I$(PS5_ROOT)/src -DAKENO_CA_BUNDLE_PATH='"$(AKENO_TP)/cacert.pem"' \
                -DAKENO_CA_BUNDLE_NAME='"Mozilla CA bundle from certifi 2026.07.22"'
OPUS_FLAGS   := -O2 -g -std=c11 -w -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk \
                -DOPUS_BUILD -DVAR_ARRAYS -DHAVE_LRINT -DHAVE_LRINTF -DPACKAGE_VERSION='"1.5.2"' -I$(OPUS_DIR)/include -I$(OPUS_DIR) \
                -I$(OPUS_DIR)/celt -I$(OPUS_DIR)/silk -I$(OPUS_DIR)/silk/float
# The voice service needs no SDL, no pad, no video: audio out, the user service and system service only.
LIBS         := -lpthread -lSceSystemService -lSceUserService -lSceAudioOut

.PHONY: all clean package
all: $(ELF)

$(OBJ)/pv/%.o: $(ROOT)/src/%.c $(wildcard $(ROOT)/src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(PV_FLAGS) -c $< -o $@

$(OBJ)/native/%.o: $(NATIVE_ROOT)/src/%.c $(wildcard $(NATIVE_ROOT)/src/core/*.h) $(wildcard $(NATIVE_ROOT)/src/app/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(PV_FLAGS) -c $< -o $@

$(OBJ)/lab/lab_pcm.o: $(LAB_SRC) $(LAB_ROOT)/src/lab_pcm.h
	@mkdir -p $(dir $@)
	$(CC) $(PV_FLAGS) -c $< -o $@

$(OBJ)/client/%.o: $(PS5_ROOT)/src/%.c $(wildcard $(PS5_ROOT)/src/*.h) $(wildcard $(PS5_ROOT)/include/akeno/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CLIENT_FLAGS) -c $< -o $@

$(OBJ)/client/transport_mbedtls.o: $(AKENO_TP)/cacert.pem

$(UIHTTP_OBJ): $(UIHTTP_SRC) $(PS5_ROOT)/app/ui_http.h
	@mkdir -p $(dir $@)
	$(CC) $(CLIENT_FLAGS) -c $< -o $@

# AKENO Native's process context check (context.c) decodes the app info with the daemon's parser.
$(GAMEDET_OBJ): $(GAMEDET_SRC) $(PS5_ROOT)/app/game_detect.h
	@mkdir -p $(dir $@)
	$(CC) $(CLIENT_FLAGS) -c $< -o $@

$(OBJ)/mbedtls/%.o: $(AKENO_TP)/mbedtls/library/%.c $(PS5_ROOT)/config/akeno_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_FLAGS) -c $< -o $@

$(OBJ)/opus/%.o: $(OPUS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OPUS_FLAGS) -c $< -o $@

$(STBI_OBJ): $(NATIVE_ROOT)/src/third/stb_image_impl.c $(NATIVE_ROOT)/src/core/stb_image_akn.h $(NATIVE_TP)/stb_image.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -g -std=c11 -w -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk \
	      -I$(NATIVE_TP) -I$(NATIVE_ROOT)/src/core -c $< -o $@

$(DEBUG_ELF): $(OWN_OBJ) $(NATIVE_OBJ) $(LAB_OBJ) $(CLIENT_OBJ) $(UIHTTP_OBJ) $(GAMEDET_OBJ) $(MBEDTLS_OBJ) $(OPUS_OBJ) $(STBI_OBJ)
	$(CC) -o $@ $^ $(LIBS)

$(ELF): $(DEBUG_ELF)
	$(STRIP) -o $@ $<

clean:
	rm -rf $(BUILD)

# The websrv-format launcher folder (a background entry, like the daemon's): the ELF and homebrew.js.
PACKAGE := $(BUILD)/AKENOPartyVoice
package: $(ELF)
	rm -rf $(PACKAGE)
	mkdir -p $(PACKAGE)
	cp $(ELF) $(PACKAGE)/akeno-party-voice.elf
	cp $(ROOT)/package/homebrew.js $(PACKAGE)/homebrew.js
