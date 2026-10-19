AKENO NETWORK - PS5 party voice alpha: NO NEW HOST IN THIS PACKAGE

Keep using the Phase 4.1 host you tested on your PS5 (FW 12.20, 2026-10-19):

  OnionHEN-AKENO-direct-fixed.elf
  SHA-256 560a2202ae97a832cf5635d07fce41062999e972ac7683e9adad475eb9828858
  (package AKENO-NETWORK-PS5-PHASE4-FIX, draft PR #17)

Send it exactly once per boot, as before. Your /data/OnionHEN/config.ini stays as it is
(toolbox=l2_r3, toolbox_target=AKEN00001, toolbox_target_mode=link or root).

This voice alpha does not change the host, its controller hooks or its Settings handling.
Voice runs in a separate background payload (voice/akeno-party-voice.elf); the plugin
(plugin/AKEN00001-voice-alpha.elf) only shows the voice page and sends your presses to it.

Rollback of the host: the original OnionHEN.elf v0.9.0 (SHA-256 1f1f7c88...7c212), see the
Phase 4.1 package's ROLLBACK-DE.md.
