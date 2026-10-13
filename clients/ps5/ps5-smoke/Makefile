# This isolated diagnostic uses the public ps5-payload-sdk toolchain.
# It does not compile the AKENO device client and must not be described as such.
ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK must point to a verified ps5-payload-sdk installation)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := akeno-ps5-diagnostic.elf
CFLAGS := -Wall -Wextra -Werror -g

.PHONY: all clean
all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)
