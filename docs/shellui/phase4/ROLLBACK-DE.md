# Phase 4 – zurück zur funktionierenden v1.10-Einrichtung

Die Phase 4 ändert **nur den Host** (onionHEN-Datei) und **zwei Zeilen** in
`/data/OnionHEN/config.ini`. Plugin, Daemon, AKENO-Konto und Server bleiben unverändert.

## Schnell (2 Minuten)

1. **Konsole komplett neu starten** (nicht Ruhemodus). Das entfernt den Phase-4-Host aus der
   ShellUI. Er verändert keine Datei auf der Konsole dauerhaft.
2. Wie früher starten: kstuff → ELF-Loader → AKENO-Daemon → **originales `OnionHEN.elf` v0.9.0**
   einmal senden.
3. Fertig. AKENO v1.10 läuft wie vorher (Einstellungen → OnionHEN-Toolbox → … → AKENO NETWORK, oder
   dein DebugMenu-Weg).

Die neuen Zeilen in `config.ini` stören das Original nicht: Es ignoriert `toolbox_target` und
`plugin_entry`. Mit `toolbox=l2_r3` öffnet L2 + R3 beim Original die Toolbox.

## Vollständig (Einstellungsdatei wie vorher)

Per FTP deine Sicherung zurückkopieren: `config-v1.10.ini` → `/data/OnionHEN/config.ini`.
Oder nur diese Zeilen löschen bzw. zurücksetzen:

```ini
plugin_entry=none
toolbox_target=toolbox
```

## Wenn die Konsole nach dem Phase-4-Host nicht sauber startet

- Normaler Neustart genügt: Der Host wird bei jedem Start neu gesendet, nichts installiert sich
  dauerhaft. Ohne Senden des Hosts läuft die PS5 ganz normal.
- Schlimmstenfalls PS5 im abgesicherten Modus starten → „PS5 neu starten“. AKENO und onionHEN
  ändern keine Systemdatei, die das verhindern könnte. (onionHEN selbst legt nur
  `/system_ex/vsh_asset/onionhen.png` an, wie das Original.)

Danach bitte `/data/OnionHEN/OnionHEN.log` (und, falls vorhanden, `OnionHEN.log.1`) sichern und
mitschicken.

## Was nicht nötig ist

- Plugin `AKEN00001.elf` austauschen: es ist unverändert.
- Daemon neu koppeln oder `/data/akeno/` anfassen.
- Systemaktualisierungsdateien: AKENO löscht oder ändert nichts davon (onionHEN löscht wie bisher
  eine heruntergeladene `PS5UPDATE.PUP`, das ist unverändertes Original-Verhalten).
