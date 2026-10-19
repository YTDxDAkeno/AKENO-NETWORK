# AKENO Voice Alpha 0.2.0 – Installation (PS5, FW 12.20)

> **EXPERIMENTELL. 0.2.0 lief noch nie auf einer PS5.** Es ersetzt **nur eine Datei**: den
> Sprachdienst `akeno-party-voice.elf`. Alles, was bei dir funktioniert, bleibt unverändert.

## Was 0.2.0 anders macht – kurz und ehrlich

Dein Test von 0.1.0 hat gezeigt: Netzwerk und Party funktionieren, aber der Ton ist stumm und das
Mikrofon wird abgelehnt (`0x809B0081`). Die Untersuchung ergab:

- **„NOT paced" war ein Messfehler von 0.1.0** (er hat zwischen den Blöcken selbst geschlafen). 0.2.0
  misst richtig: ein kurzer, stiller Uhr-Test beim Öffnen des Ausgangs, danach taktet der Ausgang selbst.
- **Der Testton läuft jetzt durch dieselbe Strecke wie die Stimme deines Freundes** (Opus, Puffer,
  Decoder, Lautstärke, Ausgang). Jeder Block wird geprüft: Ist der 880-Hz-Ton wirklich darin?
- **Das Mikrofon wird nicht mehr blind wiederholt.** Eine Ablehnung wird erklärt und in derselben
  Situation nicht erneut versucht (erst wieder, wenn ein Spiel startet oder endet).
- **Sehr wahrscheinlicher Grund für Stille und Mikrofon-Ablehnung:** Die PS5 gibt Ton und Mikrofon nur
  der **Vordergrund-App**. AKENO Native (Vordergrund) war hörbar und bekam das Mikrofon – mit genau
  denselben Aufrufen. Der Sprachdienst ist ein **Hintergrundprozess**. 0.2.0 kann das nicht umgehen
  (das ginge nur mit verbotenen Tricks), misst es aber eindeutig. Ob du etwas hörst, entscheidet
  allein dein Ohr – der Bericht behauptet das nie.

## Was im Paket ist – und was du behältst

| Datei | Was tun |
| --- | --- |
| `voice/akeno-party-voice.elf` | **NEU (0.2.0):** ersetzt deinen Sprachdienst 0.1.0 |
| `voice/launcher/homebrew.js` | unverändert (nur falls du den Dienst über den Homebrew-Launcher startest) |
| `plugin/AKEN00001-1.20.elf` | **nichts tun:** dein installiertes Plugin 1.20, byte-gleich (SHA-256 `f3fdf510…`) – nur als Reserve |
| `rollback/akeno-party-voice-0.1.0.elf` | dein getesteter Sprachdienst 0.1.0, byte-gleich (SHA-256 `4c19e428…`) – zum Zurückwechseln |
| `rollback/AKEN00001-1.10.elf` | dein Plugin 1.10 (SHA-256 `201d2c88…`) |
| `host/`, `daemon/` | **keine neuen Dateien:** Host `OnionHEN-AKENO-direct-fixed.elf` und Daemon bleiben |
| `docs/` | diese Anleitungen, Testablauf, erwartete Log-Zeilen, Technik |
| `BUILDINFO.txt`, `SHA256SUMS`, `licenses/` | Build-Daten, Prüfsummen, Lizenzen |

**Nicht anfassen:** `OnionHEN-AKENO-direct-fixed.elf`, `/data/OnionHEN/config.ini`,
`/data/OnionHEN/plugins/AKEN00001.elf` (1.20), den AKENO-Daemon, `/data/akeno/`, kstuff, PSN,
Game Base. Am Server, VPS, an LiveKit und am Voice Gateway ändert sich nichts.

## 1. Paket am PC prüfen

```sh
sha256sum -c SHA256SUMS
```

Jede Zeile `OK`. Windows (PowerShell): `Get-FileHash voice\akeno-party-voice.elf -Algorithm SHA256`
und mit `BUILDINFO.txt` vergleichen.

## 2. Sichern

Per FTP auf den PC kopieren: deine jetzige `akeno-party-voice.elf` (0.1.0) und den Ordner
`/data/akeno-party-voice/` (darin `report.txt`, `voice.log` von deinem ersten Test – bitte aufheben
und mitschicken, siehe Testanleitung). Die 0.1.0-Datei liegt zusätzlich unter `rollback/`.

## 3. Datei ersetzen

`voice/akeno-party-voice.elf` dorthin kopieren, wo du die 0.1.0 bisher hattest (PC zum Senden, oder
z. B. `/data/akeno-party-voice/akeno-party-voice.elf`, oder der Homebrew-Launcher-Ordner). Sonst nichts.

## 4. Starten (wie bisher)

**Konsole komplett neu starten** (nicht Ruhemodus), dann:

1. Exploit → kstuff → ELF-Loader (Port 9021).
2. AKENO-Daemon senden, Benachrichtigung abwarten.
3. `OnionHEN-AKENO-direct-fixed.elf` **genau einmal** senden.
4. `akeno-party-voice.elf` (**0.2.0**) senden → *„AKENO Voice ready: AKENO NETWORK > Party > Voice chat"*.

Prüfen: L2 + R3 → AKENO NETWORK → **Diagnostics & help** zeigt *„Voice service: ready 0.2.0-alpha"*.

Der Dienst öffnet beim Start **nichts** (kein Mikrofon, kein Ton, keine Verbindung). Er startet sich
nie selbst neu. Er schreibt nur `/data/akeno-party-voice/`.

## 5. Bedienung (unverändert, Plugin 1.20)

L2 + R3 → AKENO NETWORK → **Voice chat**: Join voice chat · Turn on microphone · Mute · Deafen ·
Lautstärke · **Speaker route** → **Play the test sound**.

Neu in 0.2.0 sichtbar: Wählst du Route 3 oder 4, kommt *„Route 3 is retired …"* – diese beiden
(alte PS4-Nummern) werden nicht mehr geöffnet. Benutze **Route 1**. Drückst du nach einer
Mikrofon-Ablehnung erneut *„Try the microphone again"*, kommt *„The PS5 refused the microphone to
this background service (0x809B0081). It is tried again when a game starts or ends."* – ohne neuen
Versuch. Das ist Absicht.

Weiter mit dem Test: [HARDWARE-TEST-DE.md](HARDWARE-TEST-DE.md). Zurück: [ROLLBACK-DE.md](ROLLBACK-DE.md).
