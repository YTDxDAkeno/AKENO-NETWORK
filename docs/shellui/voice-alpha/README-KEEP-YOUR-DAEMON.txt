AKENO NETWORK - PS5 party voice alpha: NO NEW DAEMON IN THIS PACKAGE

Keep your AKENO native-IPC daemon with native interface 3, for example
196ca13c71d1-ps5-native-ipc (the build you ran with AKENO Native 1.1 and the plugin).

Interface 3 already contains the console voice pass route
(POST /api/v1/parties/:id/voice/device-token) in its fixed write list, so the voice service
asks for its voice pass through your existing daemon. Nothing about the daemon changes.

Check: AKENO > Diagnostics & help shows "Native interface 3". A daemon with interface 1 or 2
(older packages) cannot ask for voice; the voice page then says so.

Start order: daemon first, then the onionHEN host, then voice/akeno-party-voice.elf. If the
daemon restarts it writes a new key; send the voice service again afterwards.
