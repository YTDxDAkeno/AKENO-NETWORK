#!/usr/bin/env python3
"""Check the actual PS5 ELF descriptor against onionHEN v0.9.0's host rules.

The SDK's descriptor inspector accepts AUTO_START, but onionHEN host commit
aaa4264 rejects it: kKnownFlags = LONG_RUNNING | STOP_SUPPORTED.
This independent check reads .onion_plugin from the final stripped ELF.
It performs no console or network operations.
"""
import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError("host descriptor: " + message)


def check(elf: bytes) -> None:
    if len(elf) < 64 or elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 1:
        fail("expected little-endian ELF64")
    section_offset = struct.unpack_from("<Q", elf, 40)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", elf, 58)
    if entry_size < 64 or count == 0 or names_index >= count:
        fail("unsupported section table")
    if section_offset > len(elf) or count > (len(elf) - section_offset) // entry_size:
        fail("section table out of bounds")

    section_format = "<IIQQQQIIQQ"
    entries = [struct.unpack_from(section_format, elf, section_offset + i * entry_size)
               for i in range(count)]

    def data(entry: tuple) -> bytes:
        offset, length = entry[4], entry[5]
        if offset > len(elf) or length > len(elf) - offset:
            fail("section out of bounds")
        return elf[offset:offset + length]

    names = data(entries[names_index])
    sections = []
    for entry in entries:
        name_offset = entry[0]
        if name_offset >= len(names):
            fail("section name out of bounds")
        end = names.find(b"\0", name_offset)
        if end < 0:
            fail("unterminated section name")
        if names[name_offset:end] == b".onion_plugin":
            sections.append(data(entry))
    if len(sections) != 1:
        fail("expected exactly one .onion_plugin section")

    descriptor = sections[0]
    if len(descriptor) < 128:
        fail("descriptor truncated")
    size, abi, capabilities, flags = struct.unpack_from("<IIII", descriptor)
    if not 128 <= size <= len(descriptor) or abi != 1:
        fail("invalid descriptor size or ABI")
    # NOTIFY | IPC | UI; the host knows bits 0-5 (kKnownCapabilities = (1 << 6) - 1).
    if capabilities != (1 << 0) | (1 << 1) | (1 << 5):
        fail("unexpected plugin capabilities")
    if flags != (1 << 1) | (1 << 2):
        fail("host v0.9.0 only permits LONG_RUNNING and STOP_SUPPORTED")
    identifier = descriptor[16:48].split(b"\0", 1)[0]
    version = descriptor[48:64].split(b"\0", 1)[0]
    if identifier != b"AKEN00001" or version != b"1.20":  # 1.10 (the rollback) is rebuilt from its own commit
        fail("unexpected plugin ID or version")
    print("host descriptor: OK (onionHEN v0.9.0, ABI 1, NOTIFY+IPC+UI, flags 0x06)")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            fail("usage: check-host-descriptor.py AKEN00001.elf")
        check(Path(sys.argv[1]).read_bytes())
    except (OSError, ValueError, struct.error) as exc:
        sys.exit(str(exc))
