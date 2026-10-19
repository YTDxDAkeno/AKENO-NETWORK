# AKENO Voice Alpha 0.2.0 – sofort zurück

0.2.0 ersetzt **nur** `akeno-party-voice.elf`. Der Dienst wird bei jedem Start neu gesendet und
installiert sich nicht. Host, `config.ini`, Plugin 1.20, Daemon, kstuff, Kopplung, Konto, Freunde,
Nachrichten und Partys bleiben unverändert.

## Wenn Konsole oder Spiel nicht mehr reagieren

1. 30 Sekunden warten, PS-Taste probieren.
2. Reagiert nichts: Power-Taste an der Konsole halten, bis sie ausgeht (zweiter Piepton).
3. Beim nächsten Start den Sprachdienst **nicht** senden.

## Nur die Sprache abschalten (1 Minute)

L2 + R3 → AKENO NETWORK → **Voice chat → Leave voice chat**, oder **Diagnostics & help → Stop the
voice service**. Oder Konsole neu starten und `akeno-party-voice.elf` nicht mehr senden.

## Zurück zu Voice Alpha 0.1.0 (2 Minuten)

1. `rollback/akeno-party-voice-0.1.0.elf` statt der 0.2.0 verwenden (SHA-256 beginnt mit `4c19e428`,
   genau deine getestete Datei, Build `ce06ba04c048-ps5`).
2. Konsole neu starten, wie gewohnt: kstuff → ELF-Loader → Daemon → `OnionHEN-AKENO-direct-fixed.elf`
   einmal → `akeno-party-voice-0.1.0.elf`.
3. Diagnostics & help zeigt dann *„Voice service: ready 0.1.0-alpha"*.

## Zurück zum Plugin 1.10 (ohne Sprachseite)

1. `rollback/AKEN00001-1.10.elf` per FTP nach **`/data/OnionHEN/plugins/AKEN00001.elf`** kopieren
   (SHA-256 beginnt mit `201d2c88`). Den Marker `AKEN00001.elf.auto_start` nicht löschen.
2. Konsole neu starten, den Sprachdienst nicht senden.

## Aufräumen (optional)

`/data/akeno-party-voice/` löschen (nur Bericht, Log, Einstellungen). Sonst wurde nichts angelegt;
keine Systemdatei wurde geändert. Für den Host gilt weiter der Rückweg aus dem Phase-4.1-Paket
(originales `OnionHEN.elf` v0.9.0).
