AKENO NETWORK - PS5 Voice Alpha 0.2.0: NO NEW DAEMON IN THIS PACKAGE

Keep your AKENO native-IPC daemon with native interface 3 (the one that gave Voice Alpha 0.1.0
its voice pass on 2026-10-19). Nothing about the daemon, the pairing in /data/akeno/ or the
server changes.

Start order, unchanged: daemon first, then the onionHEN host, then voice/akeno-party-voice.elf.
If the daemon restarts it writes a new key; send the voice service again afterwards.
