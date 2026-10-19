# AKENO Party-Voice Alpha – zurück zur funktionierenden Einrichtung

Diese Alpha ändert **zwei Dinge**: sie fügt den Sprachdienst `akeno-party-voice.elf` hinzu (wird bei
jedem Start neu gesendet, installiert sich nicht) und ersetzt das Plugin `AKEN00001.elf` 1.10 durch
1.20. Host, `config.ini`, Daemon, kstuff, Kopplung, Konto, Freunde, Nachrichten und Partys bleiben
unverändert.

## Wenn die Konsole oder das Spiel nicht mehr reagiert

1. 30 Sekunden warten, PS-Taste probieren.
2. Reagiert nichts: Power-Taste an der Konsole halten, bis sie ausgeht (zweiter Piepton). Beim
   nächsten Start kann die PS5 eine Speicherprüfung machen – normal.
3. Weiter mit „Schnell".

## Nur die Sprache abschalten (1 Minute)

- In AKENO: **Voice chat → Leave voice chat**, oder **Diagnostics & help → Stop the voice service**.
- Oder einfach **neu starten** und `akeno-party-voice.elf` nicht mehr senden. Das Plugin 1.20 zeigt
  dann „voice service not running"; alles andere funktioniert wie mit 1.10.

## Schnell: zurück zu Plugin 1.10 (2 Minuten)

1. Per FTP `plugin/rollback/AKEN00001-1.10.elf` (oder deine Sicherung) nach
   **`/data/OnionHEN/plugins/AKEN00001.elf`** kopieren (überschreiben). Prüfsumme muss mit
   `201d2c88` beginnen.
2. **Konsole komplett neu starten** und wie gewohnt laden: kstuff → ELF-Loader → AKENO-Daemon →
   `OnionHEN-AKENO-direct-fixed.elf` einmal. Den Sprachdienst **nicht** senden.
3. Fertig: L2 + R3 öffnet AKENO wie in deinem bestätigten Phase-4.1-Test.

## Aufräumen (optional)

- `/data/akeno-party-voice/` löschen (nur Bericht, Log, Einstellungen des Sprachdienstes).
- Nichts sonst wurde angelegt. Keine Systemdatei wurde geändert.

## Weiter zurück

Der Host ist unverändert; sein Rückweg steht in der Phase-4.1-Anleitung (`ROLLBACK-DE.md` des
Phase-4.1-Pakets): originales `OnionHEN.elf` v0.9.0 (SHA-256 `1f1f7c88…7c212`) statt des Phase-4.1-Hosts.

## Was nicht nötig ist

- Daemon neu koppeln oder `/data/akeno/` anfassen.
- `config.ini` ändern.
- Abgesicherter Modus – nur, falls die PS5 wider Erwarten nicht normal startet ("PS5 neu starten").

Der Sprachdienst startet sich **nie selbst** neu und wird von nichts automatisch neu gestartet:
ein Neustart der Konsole beendet ihn sicher.
