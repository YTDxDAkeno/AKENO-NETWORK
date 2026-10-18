# Rücknahme – AKENO-Seiten in der PS5-ShellUI

Das AKENO-Plugin schreibt **keine Datei** auf der Konsole und verändert keine Systemdatei. Jede
Rücknahme betrifft nur `/data/OnionHEN/…` oder deinen Startablauf.

## Zurück auf Plugin 1.00

1. Per FTP deine Sicherung `AKEN00001-1.00.elf` nach `/data/OnionHEN/plugins/AKEN00001.elf`
   kopieren (überschreibt 1.10).
2. Toolbox → Plugins → AKENO NETWORK → „Läuft“ aus und wieder an (oder Konsole neu starten).

## Plugin deaktivieren

Toolbox → Plugins → AKENO NETWORK → „Läuft“ aus, „Autostart“ aus. „Autostart aus“ entfernt die
Markierungsdatei `/data/OnionHEN/plugins/AKEN00001.elf.auto_start`, die onionHEN beim Einschalten
angelegt hat.

## Plugin entfernen

Toolbox → Plugins → AKENO NETWORK → „Plugin-ELF löschen“ – oder per FTP
`/data/OnionHEN/plugins/AKEN00001.elf` und ggf. `AKEN00001.elf.auto_start` löschen. Sonst nichts.

## Tastenkürzel zurücksetzen

Toolbox → Controller-Tastenkürzel → „OnionHEN-Toolbox öffnen“ → „Aus (kein Tastenkürzel)“, oder in
`/data/OnionHEN/config.ini` unter `[shortcuts]`: `toolbox=off`. Deine Sicherung der `config.ini`
stellt den alten Zustand ganz her.

## onionHEN nicht mehr verwenden

Konsole neu starten und `OnionHEN.elf` nicht mehr senden: seine ShellUI-Änderungen und Mounts
enden mit dem Neustart. Optional `/data/OnionHEN/` löschen (Logs, Einstellungen, Plugins).

Was onionHEN selbst (nicht AKENO) hinterlassen hat und **bleibt**:

- `/system_ex/vsh_asset/onionhen.png` – eine neue Datei, die onionHEN angelegt hat. Sie stört
  nichts. **Nicht selbst löschen**: dafür müsste die Systempartition beschreibbar gemountet
  werden; das ist ohne Not nicht empfehlenswert.
- Eine gelöschte heruntergeladene Systemaktualisierung (`/update/PS5UPDATE.PUP`) bleibt gelöscht;
  die Konsole würde sie bei erlaubten Updates erneut laden (Updates auf 12.20 weiter vermeiden).

## Den PR-#14-Host (falls je gestartet)

Konsole neu starten (entfernt seine Hooks aus der ShellUI) und alle Kopien von
`AKENO-ShellUI-Host-EXPERIMENTAL.elf` löschen. Er hat keine Dateien unter `/system` geschrieben.

## Unberührt

AKENO NETWORK Native, der AKENO-Daemon und `/data/akeno/` hängen nicht vom Plugin ab; das Plugin
liest dort nur `native.key`. Spiele, Spielstände, kstuff und dein ELF-Loader werden nicht berührt.
