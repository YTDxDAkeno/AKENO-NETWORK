# AKENO Party-Voice Alpha – Diagnose

Der Sprachdienst schreibt alles nach **`/data/akeno-party-voice/`**:

| Datei | Inhalt |
| --- | --- |
| `report.txt` | der Bericht dieses Laufs (alle 10 s in einer Sprach-Party, bei jeder Änderung und beim Beenden) |
| `report-previous.txt` | der Bericht des vorigen Laufs (vor dem letzten Start des Dienstes) |
| `voice.log` | Zeitlinie: Zustände, Codes, Zähler (wird ab 512 KB zu `voice.log.1`) |
| `settings.json` | Lautstärke, Mikrofon-Verstärkung, Lautsprecher-Route (nie die Mikrofon-Erlaubnis) |

**Datenschutz:** Es werden **keine** Aufnahmen, Audiodaten, Namen, Konto-/Party-IDs, Sprachpässe,
Schlüssel oder Nachrichten gespeichert – nur Zustände, Fehlercodes, Zähler und Pegel. Du kannst die
Dateien bedenkenlos schicken.

In AKENO: **Diagnostics & help** zeigt die Version des Dienstes, seinen Zustand und die Paketzähler;
**Voice chat** zeigt *Status* und eine Zeile *Measured: network … · mic sent … · speaker output …*.

## Der Bericht, Abschnitt für Abschnitt

### [Summary] – die drei getrennten Ergebnisse

| Zeile | „yes" bedeutet | Was noch fehlt |
| --- | --- | --- |
| `NETWORK CONNECTED` | der Server hat den Sprachpass ausgestellt, der Voice-Gateway hat ihn angenommen und die Teilnehmer geschickt | nichts – dein Freund sieht dich in der Sprach-Party |
| `MICROPHONE TRANSMITTING` | Mikrofon-Blöcke **mit Inhalt** (nicht nur Nullen) wurden aufgenommen, kodiert und gesendet; darunter steht ggf., mit welchem Pegel der Gateway deinen Ton dekodiert hat | **dein Freund** muss sagen, ob er dich hört |
| `SPEAKER OUTPUT ACTIVE` | eingehender Ton wurde dekodiert, und der Audioausgang hat Blöcke mit Ton angenommen | **du** musst sagen, ob du etwas gehört hast |

`Last problem:` – der letzte Fehler in Klartext (z. B. *„voice pass: HTTP 404"* = Server-Update fehlt).

### [Network]

- `State` – `connected`, `reconnecting`, `disconnected` + Grund (z. B. *„You were removed from the
  voice chat."* = Party verlassen oder Berechtigung entzogen).
- `Daemon native interface` – muss **3** sein (sonst kann der Daemon keinen Sprachpass holen).
- `Gateway ready`, `Participants received`, `connections`, `reconnects` – wie oft verbunden/neu verbunden.
- `Last close code` – 4003 entfernt, 4004 woanders beigetreten, 4005 Raum beendet, 1006 Verbindung verloren.
- `Packets` – gesendet (davon mit Ton), empfangen, verworfen.

### [Microphone]

- `Turned on by you` – ohne dein *Turn on microphone* wird es nie geöffnet.
- `State` + `open result` – `0 (success)` oder der Systemcode, z. B. `0x8026….`
- `Library / port` – mit welcher Rate (16000 Hz) und für welchen Benutzer geöffnet.
- `Reads`, `failed` – Lesezugriffe und Fehler.
- `Captured 20 ms frames … with sound` – **wichtig:** viele Blöcke, aber *with sound 0* heißt: die PS5
  liefert dem Hintergrundprozess nur Stille (Nullen).
- `Level` – Pegel in dBFS (Sprache etwa −40 bis −10 dBFS; −100 = Stille).
- `Encoded packets … transmitted`.

### [Speakers]

- `Route` – 1 bis 4 (siehe unten), `State`, `open result`.
- `Decoded frames (with sound …)` – was vom Freund ankam und dekodiert wurde.
- `Output submissions … blocks with sound` – was an den Audioausgang ging.
- `Timing per call … -> consumed at playback speed` = eine Audio-Uhr hat die Blöcke abgenommen (gutes,
  aber **kein** Beweis für Hörbarkeit; frühere Tests waren so und trotzdem stumm).
  `NOT paced` = die Aufrufe kamen sofort zurück: der Ton wurde sehr wahrscheinlich verworfen.
- `Buffer underruns` – Nachladen des Jitter-Puffers (Netzlücken).
- `Test sounds` – der letzte Testton außerhalb der Sprache: Route, Ergebnis, Timing.

| Route | Ausgang |
| --- | --- |
| 1 | System-Benutzer, Hauptausgang (so hat AKENO Native im Vordergrund hörbar gespielt) |
| 2 | dein Benutzer, Hauptausgang |
| 3 | dein Benutzer, „Voice"-Port (Typ 2, PS4-Nummerierung, auf der PS5 unbestätigt) |
| 4 | dein Benutzer, „Personal"-Port (Typ 3, unbestätigt) |

Testton: **Route N = N Pieptöne** (−20 dBFS).

### [Runtime]

- `Heartbeats, largest gap` – wenige hundert ms sind normal; **viele Sekunden** heißt: das System
  hat den Hintergrundprozess pausiert (z. B. während des Spiels).
- `Game (running app)` – ob während der verbundenen Sprache ein Spiel lief und wie lange.
- `CPU time`.

### [Your answers]

Bitte ausfüllen: Party beigetreten? Freund gehört? Von Freund gehört? Welche Routen beim Testton
gehört? Alles in Ordnung nach Rückkehr ins Spiel? Spielton geblieben?

## Typische Bilder

| Bericht zeigt | Bedeutung |
| --- | --- |
| `State: disconnected - … HTTP 404` | Server-Update fehlt ([SERVER-PREREQUISITES-DE.md](SERVER-PREREQUISITES-DE.md)) |
| `Daemon native interface: 2` | alter Daemon – den Native-IPC-Daemon mit Schnittstelle 3 verwenden |
| Mikrofon `failed, open result 0x…` | die PS5 verweigert dem Hintergrundprozess das Mikrofon (Code notieren) |
| Mikrofon `frames 2000, with sound 0` | Mikrofon offen, liefert aber nur Stille |
| Lautsprecher `paced`, `blocks with sound` > 0, du hörst nichts | Ton angenommen, aber nicht hörbar (wie Audio Lab v2) – andere Routen testen |
| Lautsprecher `NOT paced` | Ausgang nimmt nichts ab |
| `largest gap` mehrere Sekunden | Hintergrundprozess wird im Spiel pausiert |

## Logs zum Mitschicken

- `/data/akeno-party-voice/report.txt`, `report-previous.txt`, `voice.log`
- `/data/OnionHEN/OnionHEN.log` (Plugin-Zeilen: `[akeno-ingame] voice service ready` …)
- deine Notizen aus [VOICE-TEST-DE.md](VOICE-TEST-DE.md)
