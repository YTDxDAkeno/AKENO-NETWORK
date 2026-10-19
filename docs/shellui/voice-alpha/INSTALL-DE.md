# AKENO Party-Voice Alpha – Installation (PS5, FW 12.20)

> **EXPERIMENTELL.** Nichts in diesem Paket lief bisher auf einer PS5. Ob die PS5 einem
> Hintergrundprozess erlaubt, während eines Spiels das Mikrofon zu nutzen und hörbar Ton
> auszugeben, ist **unbekannt** – genau das misst dein Test ([VOICE-TEST-DE.md](VOICE-TEST-DE.md)).
> **Vorher muss der AKENO-Server Konsolen-Sprache können** ([SERVER-PREREQUISITES-DE.md](SERVER-PREREQUISITES-DE.md)):
> Am 18.10.2026 antwortete dein Produktionsserver auf den Sprachpass mit **HTTP 404**. Ohne das
> Server-Update verbindet sich die PS5 nicht mit der Sprach-Party (der Dienst zeigt dann genau das an).

## Was im Paket ist – und was du behältst

| Datei | Was tun |
| --- | --- |
| `host/README-USE-EXISTING-PHASE4.1-HOST.txt` | **Kein neuer Host.** Behalte `OnionHEN-AKENO-direct-fixed.elf` (SHA-256 `560a2202…`), genau wie bei deinem funktionierenden Phase-4.1-Test. Die `config.ini` bleibt, wie sie ist |
| `daemon/README-KEEP-YOUR-DAEMON.txt` | **Kein neuer Daemon.** Behalte deinen Native-IPC-Daemon (Schnittstelle 3, z. B. `196ca13c71d1-ps5-native-ipc`) |
| `voice/akeno-party-voice.elf` | **NEU:** der Sprachdienst (Hintergrund-Payload) |
| `voice/launcher/homebrew.js` | optional: Eintrag für den websrv-Homebrew-Launcher (startet den Dienst im Hintergrund) |
| `plugin/AKEN00001-voice-alpha.elf` | **NEU:** AKENO-Plugin 1.20 mit Sprachseite. Wird zu `/data/OnionHEN/plugins/AKEN00001.elf` |
| `plugin/rollback/AKEN00001-1.10.elf` | dein getestetes Plugin 1.10 (SHA-256 `201d2c88…`), byte-gleich neu gebaut – zum Zurückwechseln |
| `docs/` | diese Anleitungen, technische Beschreibung, Prüfprotokolle |
| `BUILDINFO.txt`, `SHA256SUMS`, `licenses/` | Build-Daten, Prüfsummen, Lizenzen |

## 1. Paket am PC prüfen

```sh
sha256sum -c SHA256SUMS
```

Jede Zeile `OK`. Windows (PowerShell): `Get-FileHash voice\akeno-party-voice.elf -Algorithm SHA256`
und mit `BUILDINFO.txt` vergleichen. `plugin\rollback\AKEN00001-1.10.elf` muss mit `201d2c88` beginnen.

## 2. Sichern

Per FTP auf den PC kopieren:

1. Dein jetziges `/data/OnionHEN/plugins/AKEN00001.elf` (1.10) → z. B. `AKEN00001-1.10-sicherung.elf`
   (das Paket enthält es zusätzlich unter `plugin/rollback/`).
2. `/data/OnionHEN/config.ini` (wird nicht geändert, aber sicher ist sicher).
3. Im Spiel speichern, bevor du mit laufendem Spiel testest.

## 3. Dateien auf die PS5

Per FTP:

1. `plugin/AKEN00001-voice-alpha.elf` → **`/data/OnionHEN/plugins/AKEN00001.elf`** (ersetzt 1.10;
   der Name muss genau so bleiben, sonst findet der Phase-4.1-Host AKENO nicht).
   Den Autostart-Marker `AKEN00001.elf.auto_start` daneben **nicht** löschen.
2. `voice/akeno-party-voice.elf` z. B. nach `/data/akeno-party-voice/akeno-party-voice.elf` oder auf
   den PC (du sendest es ohnehin an den ELF-Loader).

`/data/akeno/` (Kopplung) bleibt unberührt.

## 4. Starten (Reihenfolge)

**Konsole komplett neu starten** (nicht Ruhemodus). Dann wie gewohnt:

1. Exploit → **dein kstuff** → ELF-Loader (Port 9021).
2. **AKENO-Daemon** an Port 9021 senden, Benachrichtigung abwarten.
3. **`OnionHEN-AKENO-direct-fixed.elf` genau einmal** an Port 9021 senden (dein Phase-4.1-Host).
   Plugin AKEN00001 startet wie bisher (Autostart).
4. **NEU: `akeno-party-voice.elf` an Port 9021 senden** (wie den Daemon). Benachrichtigung:
   *„AKENO Voice ready: AKENO NETWORK > Party > Voice chat"*.
   Alternativ den Ordner `voice/launcher/` mit der ELF in den Homebrew-Launcher legen und dort starten
   (er startet ihn als Hintergrundprozess und schließt **kein** laufendes Spiel).

Der Sprachdienst öffnet dabei **nichts**: kein Mikrofon, keinen Audioausgang, keine Verbindung –
nur einen lokalen Port (127.0.0.1:8646), auf den das Plugin zugreift. Er läuft nur einmal; ein
zweites Senden meldet *„AKENO Voice is already running."*

**Reihenfolge wichtig:** Startet der Daemon neu, bekommt er einen neuen Schlüssel – dann den
Sprachdienst ebenfalls neu senden (das Plugin sagt das: *„voice service needs a restart"*).

## 5. Wo die Bedienung ist

L2 + R3 (im Spiel oder auf dem Startbildschirm) → AKENO NETWORK:

- Hauptseite: **„Voice chat · …"** (sobald du in einer Party bist)
- **Party → „Voice chat · …"**
- **Diagnostics & help**: Version des Dienstes, Paketzähler, *Stop the voice service*

Auf der Sprachseite: **Join voice chat** (fragt nach; Mikrofon bleibt aus) → **Turn on microphone**
(fragt nach) → **Mute/Unmute**, **Deafen/Undeafen**, **Voice volume** (lauter/leiser, 0–150 %),
**Microphone gain** (50–150 %), **Speaker route** (1–4) mit **Testton**, **Leave voice chat**.

Eine Seite zeigt neue Daten, wenn du sie neu öffnest (onionHEN zeichnet eine Seite beim Öffnen).
Das Ergebnis jedes Tastendrucks kommt sofort als Systembenachrichtigung.

## Was dabei passiert – ehrlich

- Der Sprachdienst ist ein **eigener Prozess**. Er greift nicht in das Spiel, in ShellUI, in
  Sony-Dienste oder den Kernel ein; er benutzt nur Funktionen, die AKENO auf 12.20 schon benutzt hat
  (Prüfprotokoll: `docs/gates/GATE-voice-service.txt`). Keine Audio-Fokus-, Lautstärke- oder
  Routing-Funktion.
- Stürzt er ab, endet nur er: Freunde, Nachrichten und Party im Plugin bleiben; die Sprachseite sagt
  *„voice service not running"*. Er startet sich **nie** selbst neu.
- Er schreibt nur `/data/akeno-party-voice/` (Bericht, Log, Einstellungen). Nichts wird aufgenommen.
- Unbekannt und Gegenstand des Tests: ob das Mikrofon im Hintergrund echten Ton liefert und ob der
  Ton im Spiel hörbar ist. Frühere Tests: Ton aus einem Hintergrund-Payload wurde **angenommen, aber
  nicht gehört** (Audio Lab v2, ohne Spiel). Darum gibt es die vier Lautsprecher-Routen und den Testton.
