# Builds AKENO Audio Lab 3.1 (EXPERIMENTAL, isolated audio-output experiment) for the PS5:
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk     # SDK commit pinned in the PS5 workflows
#   make -C clients/ps5-audiolab/console            # -> build-ps5/akeno-audio-lab.elf
#   clients/ps5-audiolab/console/check-audiolab.sh clients/ps5-audiolab/build-ps5
#   make -C clients/ps5-audiolab/console delivery
#
# It is a payload of its own. The AKENO daemon, AKENO Native, AKENO party voice, the OnionHEN host
# and the System Explorer are not changed, and nothing is sent to a console by this Makefile.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD       ?= $(ROOT)/build-ps5
OBJ         := $(BUILD)/obj
ELF         := $(BUILD)/akeno-audio-lab.elf
DEBUG_ELF   := $(BUILD)/akeno-audio-lab.debug.elf

BUILD_ID    ?= $(shell git -C $(REPO_ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)-ps5

SRC         := $(addprefix $(ROOT)/src/,main.c al.c al_engine.c al_report.c audio_lab_pcm.c \
               audio_lab_state.c al_backend_ps5.c)
OBJS        := $(patsubst $(ROOT)/src/%.c,$(OBJ)/%.o,$(SRC))

CFLAGS      := -O2 -g -std=c11 -DAL_PS5 -DAL_BUILD_ID='"$(BUILD_ID)"' \
               -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror \
               -I$(ROOT)/src -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
LIBS        := -lpthread

.PHONY: all clean
all: $(ELF)

$(OBJ)/%.o: $(ROOT)/src/%.c $(wildcard $(ROOT)/src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DEBUG_ELF): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(ELF): $(DEBUG_ELF)
	$(STRIP) -o $@ $<

clean:
	rm -rf $(BUILD)

# The websrv-format launcher folder.
PACKAGE := $(BUILD)/AKENOAudioLab
.PHONY: package
package: $(ELF)
	rm -rf $(PACKAGE)
	mkdir -p $(PACKAGE)
	cp $(ELF) $(PACKAGE)/akeno-audio-lab.elf
	cp $(ROOT)/package/homebrew.js $(PACKAGE)/homebrew.js

# The review/delivery package with the ELF, the German guides, SAFETY and SHA-256 checksums.
DELIVERY := $(BUILD)/AKENO-Audio-Lab-3.1
ZIP := $(BUILD)/AKENO-Audio-Lab-3.1.zip
.PHONY: delivery
delivery: package
	rm -rf $(DELIVERY)
	mkdir -p $(DELIVERY)/AKENOAudioLab
	cp $(PACKAGE)/akeno-audio-lab.elf $(DELIVERY)/akeno-audio-lab.elf
	cp $(PACKAGE)/akeno-audio-lab.elf $(DELIVERY)/AKENOAudioLab/akeno-audio-lab.elf
	cp $(ROOT)/package/homebrew.js $(DELIVERY)/AKENOAudioLab/homebrew.js
	cp $(ROOT)/package/README-DE.md $(DELIVERY)/README-DE.md
	cp $(ROOT)/package/HARDWARE-TEST-DE.md $(DELIVERY)/HARDWARE-TEST-DE.md
	cp $(ROOT)/package/SAFETY.md $(DELIVERY)/SAFETY.md
	cd $(DELIVERY) && sha256sum akeno-audio-lab.elf AKENOAudioLab/akeno-audio-lab.elf \
	  AKENOAudioLab/homebrew.js README-DE.md HARDWARE-TEST-DE.md SAFETY.md > SHA256SUMS.txt
	cd $(BUILD) && rm -f $(notdir $(ZIP)) && zip -qr $(notdir $(ZIP)) $(notdir $(DELIVERY))
	@echo "delivery package: $(ZIP)"
