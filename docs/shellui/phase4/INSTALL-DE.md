# AKENO NETWORK Phase 4 – direkter Zugang aus dem laufenden Spiel (Installation)

> **HARDWARE-ERGEBNIS 19.10.2026 – dieser Build ist auf deiner PS5 (12.20) FEHLGESCHLAGEN.**
> Mit `toolbox_target=AKEN00001` erschien AKENO zwar kurz, danach reagierte die Konsole nicht mehr
> auf den Controller (das Spiel lief weiter). **Bitte diesen Host nicht erneut mit direktem Ziel
> testen.** Sicherer Weg bis zu einem überarbeiteten Build: **Original-onionHEN v0.9.0 + unverändertes
> Plugin, nur `toolbox=l2_r3`** (L2 + R3 halten → Toolbox → Plugins → AKENO NETWORK). Rücknahme:
> [ROLLBACK-DE.md](ROLLBACK-DE.md). Der Abschnitt „Schnellzugriff ohne neuen Host“ unten beschreibt
> genau diesen sicheren Weg.

**Experimentell, auf der PS5 noch nicht ausgeführt.** Gebaut und auf dem PC getestet. Nichts wird
automatisch auf die Konsole übertragen. Deine funktionierende v1.10-Einrichtung bleibt erhalten,
und du kannst jederzeit zurück: [ROLLBACK-DE.md](ROLLBACK-DE.md).

## Was ist neu, was bleibt gleich

| Datei | Status | Was tun |
| --- | --- | --- |
| `plugin/AKEN00001.elf` | **unverändert** v1.10, byte-gleich mit der Version, die bei dir läuft (SHA-256 beginnt mit `201d2c88`) | nichts, deine Datei behalten |
| AKENO-Daemon (`akeno-network-ps5-native-ipc.elf`) | **unverändert**, nicht im Paket | wie bisher |
| `host/OnionHEN-AKENO-direct.elf` | **neu, experimentell**: originales onionHEN v0.9.0 plus genau ein Patch (`docs/akeno-direct-entry.patch`) | statt `OnionHEN.elf` senden, nur für den Test |
| `host/config-akeno-direct.ini` | zwei neue Zeilen für `/data/OnionHEN/config.ini` | eintragen (Schritt 4) |
| `voice/akeno-voicelab.elf` | Messprogramm für Sprach-Partys im Spiel, getrennt | nur für [VOICE-TEST-DE.md](VOICE-TEST-DE.md) |

Ohne die beiden neuen Zeilen verhält sich der Host genau wie das Original-onionHEN v0.9.0.

## Was der neue Host macht

1. **Tastenkürzel direkt zu AKENO.** Das vorhandene Toolbox-Tastenkürzel von onionHEN (L2 + R3
   **1 Sekunde gedrückt halten**) öffnet die Einstellungen **direkt auf der AKENO-Hauptseite**,
   nicht mehr auf der Toolbox. Das Spiel läuft im Hintergrund weiter (pausiert, wie bei deinem
   DebugMenu-Weg). Mit **○** verlässt du AKENO und bist zurück im Spiel.
2. **„AKENO NETWORK“ im Optionsmenü eines Spiels** (OPTIONS-Taste auf der Spielkachel), neben
   „Nach Update suchen“. Auswählen öffnet dieselbe AKENO-Seite.

Es ist dieselbe Technik, mit der onionHEN schon heute im Spiel das Cheats-Menü per Tastenkürzel
öffnet. AKENO ersetzt dabei keine Sony-Funktion und schreibt keine Systemdatei.

**Kein Kontrollzentrum-Eintrag:** Ein Eintrag in der PS-Tasten-Leiste ist nicht gebaut. Warum:
[CONTROL-CENTER-TEST-DE.md](CONTROL-CENTER-TEST-DE.md#kontrollzentrum).

## 1. Paket prüfen (am PC)

Im entpackten Ordner `AKENO-NETWORK-PS5-PHASE4`: `sha256sum -c SHA256SUMS` → jede Zeile `OK`.
Windows: `Get-FileHash host\OnionHEN-AKENO-direct.elf -Algorithm SHA256` und mit `BUILDINFO.txt`
vergleichen.

## 2. Sicherung (einmalig)

Per FTP auf den PC kopieren:

- `/data/OnionHEN/config.ini` → z. B. `config-v1.10.ini`
- dein originales `OnionHEN.elf` v0.9.0 sicher aufbewahren (SHA-256 `1f1f7c88…7c212`)

## 3. Konsole komplett neu starten

**Neustart, nicht Ruhemodus.** Pro Start darf nur **ein** onionHEN laufen. War heute schon das
Original-onionHEN aktiv, erst neu starten.

## 4. Die zwei Zeilen eintragen

`/data/OnionHEN/config.ini` per FTP öffnen und ergänzen. Die Abschnitte gibt es schon:

```ini
[game_menu]
show_onionhen_options=true
plugin_entry=AKEN00001

[shortcuts]
cheats_menu=off
toolbox=l2_r3
toolbox_target=AKEN00001
```

- `toolbox=l2_r3` schaltet das Tastenkürzel ein (L2 + R3 halten). Steht dort schon `share` oder
  `long_share`, funktioniert das genauso.
- `toolbox_target=AKEN00001` lässt das Kürzel AKENO öffnen. `toolbox_target=toolbox` = wie früher
  die Toolbox.
- `plugin_entry=AKEN00001` fügt den Eintrag ins Spiel-Optionsmenü ein. `plugin_entry=none` = aus.

Speichert die Toolbox später Einstellungen, behält sie diese Zeilen.

## 5. Starten

Wie gewohnt, nur mit dem neuen Host:

1. Exploit → **dein kstuff** → ELF-Loader (Port 9021).
2. AKENO-Daemon an Port 9021 senden, Benachrichtigung abwarten.
3. **`host/OnionHEN-AKENO-direct.elf` einmal** an Port 9021 senden (statt `OnionHEN.elf`). Auf die
   Toolbox-Benachrichtigung warten. **Nie** ein zweites onionHEN im selben Start senden.
4. Das Plugin AKEN00001 (unverändert) startet wie bisher (Autostart), sonst Einstellungen →
   OnionHEN-Toolbox → Plugins → AKENO NETWORK → „Läuft“.

## 6. Benutzen

- Im Spiel **L2 + R3 eine Sekunde halten** → AKENO NETWORK → Friends / Messages / Party →
  **○** = zurück ins Spiel.
- Oder: Spiel-Kachel → **OPTIONS** → **AKENO NETWORK**.
- Läuft das Plugin nicht, öffnet das Kürzel wie früher die Toolbox (kein leerer Bildschirm). Dann
  das Plugin starten (Schritt 5.4).
- Der bisherige Weg über die Toolbox bleibt: Einstellungen → OnionHEN-Toolbox → Plugins →
  AKENO NETWORK.

## Schnellzugriff ohne neuen Host (sicherer Weg, nach dem Hardware-Fehler empfohlen)

Mit dem **Original**-onionHEN v0.9.0 und dem unveränderten Plugin, ohne den Phase-4-Host:

1. In `/data/OnionHEN/config.ini` nur `toolbox=l2_r3` setzen (kein `toolbox_target`, kein
   `plugin_entry`).
2. Im Spiel **L2 + R3 eine Sekunde halten** → die Toolbox öffnet sich, das Spiel bleibt im
   Hintergrund → **Plugins → AKENO NETWORK → AKENO NETWORK** → mit ○ zurück ins Spiel.

Das ist ein Schritt mehr als der direkte Zugang, aber es nutzt nur bewährte onionHEN-Funktionen und
kein neues Binary. Genau das hat bei dir schon funktioniert.

## 7. Prüfen

`/data/OnionHEN/OnionHEN.log` (per FTP):

- `Toolbox online for SceShellUI pid=…` genau **einmal**
- beim Kürzel oder Menüeintrag: `plugin page URI detected: AKEN00001`, dann
  `direct entry: AKEN00001 root page as the Settings root`
- `direct entry: AKEN00001 has no page; opening the Toolbox` bedeutet: Plugin lief nicht
- `Detour failed`, `relocation failed` oder `Toolbox already active` → Konsole neu starten,
  dann [ROLLBACK-DE.md](ROLLBACK-DE.md)

Die Tests in fester Reihenfolge: [CONTROL-CENTER-TEST-DE.md](CONTROL-CENTER-TEST-DE.md).

## Risiken (ehrlich)

- Der neue Host ist **ein anderes Binary** als das, das bei dir lief. Der Code ist bis auf den
  Patch gleich, aber mit einem etwas anderen Compiler gebaut (Ubuntu clang 18.1.3 statt Homebrew
  clang 18.1.8 des offiziellen Releases). Darum steht Test H1 (Host läuft wie bisher) zuerst.
- Ein ShellUI-Absturz ist möglich: Die Konsole kehrt zum Startbildschirm zurück oder startet neu.
  Spielstände berührt AKENO nicht. Ein laufendes Spiel kann dabei beendet werden, **ungespeicherten
  Fortschritt vorher sichern**.
- onionHEN selbst (Original wie Patch) mountet `/system` und `/system_ex` neu, schreibt
  `/system_ex/vsh_asset/onionhen.png` und löscht bei jedem Start eine heruntergeladene
  Systemaktualisierung (`/update/PS5UPDATE.PUP`). Das macht der Patch nicht neu, er ändert es auch
  nicht.
- Kein PSN, keine Sony-Anmeldung, keine Lizenz- oder Store-Funktion wird berührt.
