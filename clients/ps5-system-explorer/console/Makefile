# Builds the AKENO System Explorer (EXPERIMENTAL, read-only diagnostics) for the PS5:
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk     # SDK commit pinned in .github/workflows/ps5-native.yml
#   make -C clients/ps5-system-explorer/console     # -> build-ps5/akeno-system-explorer.elf
#   clients/ps5-system-explorer/console/check-explorer.sh clients/ps5-system-explorer/build-ps5
#
# It is a payload of its own: the AKENO daemon, AKENO Native and the OnionHEN host are not changed,
# and nothing is sent to a console by this Makefile.
#
# The optional active audio probe is NOT built here. A build WITH it (AKENO_EXPLORER_AUDIO_PROBE)
# is a separate, reviewed build with its own hardware plan; it is not part of the released ELF.

ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
REPO_ROOT   := $(abspath $(ROOT)/../..)

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD       ?= $(ROOT)/build-ps5
OBJ         := $(BUILD)/obj
ELF         := $(BUILD)/akeno-system-explorer.elf
DEBUG_ELF   := $(BUILD)/akeno-system-explorer.debug.elf

BUILD_ID    ?= $(shell git -C $(REPO_ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)-ps5

SRC         := $(addprefix $(ROOT)/src/,main.c ex.c ex_process.c ex_libraries.c ex_audio.c ex_shellui.c \
               ex_timeline.c ex_session.c ex_report.c ex_redact.c ex_backend_ps5.c)
OBJS        := $(patsubst $(ROOT)/src/%.c,$(OBJ)/%.o,$(SRC))

CFLAGS      := -O2 -g -std=c11 -DAKN_EX_PS5 -DAKN_EX_BUILD_ID='"$(BUILD_ID)"' \
               -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror \
               -I$(ROOT)/src -ffile-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(PS5_PAYLOAD_SDK)=ps5-payload-sdk
LIBS        := -lpthread -lSceSystemService -lSceUserService

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

# The websrv-format launcher folder: akeno-system-explorer.elf and homebrew.js (ICON= optional).
PACKAGE := $(BUILD)/AKENOSystemExplorer
.PHONY: package
package: $(ELF)
	rm -rf $(PACKAGE)
	mkdir -p $(PACKAGE)
	cp $(ELF) $(PACKAGE)/akeno-system-explorer.elf
	cp $(ROOT)/package/homebrew.js $(PACKAGE)/homebrew.js
	if [ -n "$(ICON)" ]; then mkdir -p $(PACKAGE)/sce_sys && cp $(ICON) $(PACKAGE)/sce_sys/icon0.png; fi

# The review/delivery package AKENO-System-Explorer-6.1/ with the ELF, the German guides, the
# safety note and SHA-256 checksums. The ELF is suitable for review before loading; nothing is
# sent to a console by this target.
DELIVERY := $(BUILD)/AKENO-System-Explorer-6.1
.PHONY: delivery
delivery: package
	rm -rf $(DELIVERY)
	mkdir -p $(DELIVERY)/AKENOSystemExplorer
	cp $(PACKAGE)/akeno-system-explorer.elf $(DELIVERY)/akeno-system-explorer.elf
	cp $(PACKAGE)/akeno-system-explorer.elf $(DELIVERY)/AKENOSystemExplorer/akeno-system-explorer.elf
	cp $(ROOT)/package/homebrew.js $(DELIVERY)/AKENOSystemExplorer/homebrew.js
	cp $(ROOT)/package/README-DE.md $(DELIVERY)/README-DE.md
	cp $(ROOT)/package/HARDWARE-TEST-DE.md $(DELIVERY)/HARDWARE-TEST-DE.md
	cp $(ROOT)/package/SAFETY.md $(DELIVERY)/SAFETY.md
	cd $(DELIVERY) && sha256sum akeno-system-explorer.elf AKENOSystemExplorer/akeno-system-explorer.elf \
	  AKENOSystemExplorer/homebrew.js README-DE.md HARDWARE-TEST-DE.md SAFETY.md > SHA256SUMS.txt
	@echo "delivery package: $(DELIVERY)"
