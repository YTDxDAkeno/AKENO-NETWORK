# Phase 4.1 – zurück zur funktionierenden Einrichtung

Phase 4.1 ändert **nur den Host** (onionHEN-Datei, wird bei jedem Start neu gesendet) und Zeilen in
`/data/OnionHEN/config.ini`. Plugin, Daemon, AKENO-Konto, Freunde, Nachrichten, Party und Server
bleiben unverändert. Nichts installiert sich dauerhaft.

## Wenn die Konsole nicht mehr reagiert

1. 30 Sekunden warten, PS-Taste probieren.
2. Reagiert nichts: **Power-Taste an der Konsole gedrückt halten, bis sie ausgeht** (zweiter
   Piepton). Beim nächsten Start kann die PS5 melden, dass sie nicht richtig ausgeschaltet wurde,
   und den Speicher prüfen – das ist normal.
3. Weiter mit „Schnell“.

## Schnell (2 Minuten)

1. **Konsole komplett neu starten** (nicht Ruhemodus). Damit ist der Phase-4.1-Host weg.
2. Wie bisher starten: kstuff → ELF-Loader → AKENO-Daemon → **originales `OnionHEN.elf` v0.9.0**
   (SHA-256 `1f1f7c88…7c212`) einmal senden.
3. Fertig: im Spiel L2 + R3 → Toolbox → Plugins → AKENO NETWORK, mit ○ zurück (dein bestätigter
   Weg vom 19.10.2026).

Die neuen Zeilen in `config.ini` stören das Original nicht: Es ignoriert `toolbox_target`,
`toolbox_target_mode` und `plugin_entry`. `toolbox=l2_r3` bleibt wie gewohnt wirksam.

## Vollständig (Einstellungsdatei wie vorher)

Per FTP die Sicherung zurückkopieren: `config-vor-4.1.ini` → `/data/OnionHEN/config.ini`.
Oder nur diese Zeilen setzen:

```ini
plugin_entry=none
toolbox_target=toolbox
toolbox_target_mode=link
```

## Nur den Direkt-Modus zurücknehmen

Wenn der Link-Modus (H5) funktioniert hat, der Direkt-Modus (H6–H8) aber nicht:
`toolbox_target_mode=link` setzen, Konsole neu starten, `OnionHEN-AKENO-direct-fixed.elf` laden.
L2 + R3 öffnet dann die Toolbox mit AKENO oben (✕ → AKENO).

Nach einem ShellUI-Neustart während einer Direkt-Sitzung schaltet der Host den Direkt-Modus bis zum
nächsten Konsolen-Neustart **selbst ab** (Log: `AKENO_DIRECT: root mode off until the console
restarts`). Ein Konsolen-Neustart setzt das zurück; bitte trotzdem erst melden, bevor du den
Direkt-Modus erneut versuchst.

## Danach

Logs sichern und mitschicken ([TEST-DE.md](TEST-DE.md#h10--logs-sammeln-und-melden)):
`/data/OnionHEN/OnionHEN.log` (+ `OnionHEN.log.1`), `/data/OnionHEN/OnionHEN_util_daemon.log`.

## Was nicht nötig ist

- Plugin `AKEN00001.elf` austauschen – es ist unverändert.
- Daemon neu koppeln oder `/data/akeno/` anfassen.
- Systemdateien: AKENO ändert keine. (onionHEN selbst legt wie im Original
  `/system_ex/vsh_asset/onionhen.png` an und löscht eine heruntergeladene `PS5UPDATE.PUP`.)
- Abgesicherter Modus: nur, falls die PS5 wider Erwarten nicht normal startet – dort „PS5 neu
  starten“ wählen. AKENO und onionHEN ändern nichts, was einen normalen Start verhindert.
