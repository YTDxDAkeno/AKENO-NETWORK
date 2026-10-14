#   AKENO NETWORK PS5 payload.
#
#   Builds a userland homebrew ELF for an already-jailbroken PS5 using the ps5-payload SDK.
#   Set PS5_PAYLOAD_SDK to the installed SDK (see docs/INSTALLATION.md), then run `make`.
#
#   The payload reuses the platform-neutral client core (the same code tested on Linux) and adds
#   PS5-specific files: REST over libSceHttp2, native toasts, and controller input. It does NOT
#   build http.c / ws.c / realtime.c (the WebSocket path has no verified console TLS binding yet)
#   or transport_openssl.c (host only).

ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined. Install the SDK and `export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk`)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ROOT    := ..
ELF     := akeno-network-ps5.elf

# Server baked into the payload; override per build with `make AKENO_SERVER=https://...`,
# or at runtime via /data/akeno/config.txt on the console.
AKENO_SERVER ?= https://85.190.112.10.sslip.io

CFLAGS  := -std=c11 -Wall -Wextra -Werror -O2 -g \
           -Wno-unreachable-code-generic-assoc \
           -I$(ROOT)/include -I$(ROOT)/src \
           -DAKENO_DEFAULT_SERVER='"$(AKENO_SERVER)"'
LDLIBS  := -lSceNet -lSceSsl -lSceHttp2 -lScePad -lSceUserService

CORE    := $(ROOT)/src/buf.c $(ROOT)/src/json.c $(ROOT)/src/crypto_util.c \
           $(ROOT)/src/url.c $(ROOT)/src/backoff.c $(ROOT)/src/caps.c $(ROOT)/src/events.c
CLIENT  := $(ROOT)/src/client.c
PS5     := $(ROOT)/src/ps5/httpclient_ps5.c $(ROOT)/src/ps5/transport_ps5.c \
           $(ROOT)/src/ps5/notify.c $(ROOT)/src/ps5/pad.c
SRCS    := $(CORE) $(CLIENT) $(PS5) akeno_ps5.c
OBJS    := $(SRCS:.c=.o)

all: $(ELF)

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(ELF) $(ELF).sha256

# Reproducible checksum for distribution.
checksum: $(ELF)
	cd $(dir $(ELF)) && sha256sum $(notdir $(ELF)) > $(notdir $(ELF)).sha256

# Manual, operator-initiated deploy to a console in test mode (never automatic). Requires
# PS5_HOST/PS5_PORT and an ELF loader listening on the console; see docs/INSTALLATION.md.
deploy: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)

.PHONY: all clean checksum deploy
