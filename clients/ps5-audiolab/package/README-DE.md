# AKENO Audio Lab 3.1 — Installation und Nutzung (DEUTSCH)

Ein **eigenständiges, isoliertes** PS5-Experiment, das genau eine Frage schärfer beantwortet als die
bisherigen Tests: *Was meldet das System über den Audio-Port, den ein Hintergrund-Payload öffnet?*

Es ist **nicht** die AKENO-NETWORK-Konsolen-App und nicht Audio Lab v3 darin. Es übernimmt den
bereits geprüften PCM-, Ausgabe- und Port-State-Code unverändert und verpackt ihn in ein winziges,
datei-gesteuertes Payload nach dem Muster des System Explorers.

> **Wichtig vorab:** Ein Ton aus einem Hintergrund-Payload wurde in früheren Tests **akzeptiert und
> getaktet, aber nicht gehört**. Dieses Lab wiederholt das nicht als Selbstzweck. Der **neue**
> Erkenntnisgewinn kommt allein aus der **Port-State-Messung** (Opt-in, siehe unten). Ohne sie ist
> der Tonlauf nur eine Bestätigung des bekannten Befunds in sauberer Isolation.

## Isolation (fest eingebaut, per CI geprüft)

- Nutzt **ausschließlich** `/data/akeno-audiolab/`. Kein Zugriff auf `/data/akeno`.
- **Keine** Netzwerkverbindung: kein VPS, kein LiveKit, keine Weboberfläche, Port 8642 wird nicht belegt.
- **Kein** Mikrofon. Keine Fokus-/Attach-/Routing-/Lautstärke-/HDMI-Funktion.
- Spielt einen Ton **nur** nach deinem ausdrücklichen `tone`-Befehl. Nichts startet von selbst.
- Läuft nur **einmal gleichzeitig** (Dateisperre). Ein zweiter Start wird abgewiesen — es kann
  keine zweite Audio-Sitzung parallel entstehen.
- Ändert nichts am OnionHEN-Host, an AKENO NETWORK, an der Party-Voice oder an Spielen.

## Voraussetzungen

| Komponente | Status |
| --- | --- |
| PS5 mit FW 12.20, Jailbreak, ELF-Loader (Port 9021) oder Homebrew-Launcher | einer davon genügt |
| AKENO-Daemon | **nicht nötig** — das Lab ist eigenständig und unabhängig |

## Paketinhalt

```
AKENO-Audio-Lab-3.1/
  akeno-audio-lab.elf          # das Payload (für den ELF-Loader)
  AKENOAudioLab/               # Launcher-Ordner (für den Homebrew-Launcher)
    akeno-audio-lab.elf
    homebrew.js
  README-DE.md                 # dieses Dokument
  HARDWARE-TEST-DE.md          # der Konsolentestplan
  SAFETY.md                    # Sicherheit, Abbruch, Rückbau
  SHA256SUMS.txt               # Prüfsummen
```

## Prüfsummen kontrollieren

```sh
cd AKENO-Audio-Lab-3.1
sha256sum -c SHA256SUMS.txt
```

## Start — zwei Wege

**A) ELF-Loader (Port 9021), wie beim Explorer.** `akeno-audio-lab.elf` an den Loader senden.
**B) Homebrew-Launcher.** Den Ordner `AKENOAudioLab/` in den Homebrew-Ordner legen und starten.
`daemon: true` sorgt dafür, dass **kein** laufendes Spiel geschlossen wird.

Beim Start erscheint eine Benachrichtigung; das Lab wartet dann auf Befehle und beendet sich nach
spätestens 30 Minuten von selbst (oder sofort auf `exit`).

## Steuerung über die Datei `control`

Hänge Befehle **zeilenweise** (jede Zeile mit Zeilenumbruch) an
`/data/akeno-audiolab/control` an (per FTP oder Dateiansicht des Launchers):

| Befehl | Wirkung |
| --- | --- |
| `status` | Schreibt die Berichte sofort neu. |
| `tone` | Spielt **einen** Ton mit den Vorgaben (S16 Stereo, 1024, −20 dBFS, 440 Hz, ~1,5 s). |
| `tone <preset> <level>` | Ton mit gewähltem Preset/Pegel (siehe unten). |
| `stop` | Bricht einen laufenden oder wartenden Ton ab. |
| `exit` | Beendet die Sitzung sauber. |

**Presets:** `stereo16-1024` (Standard), `mono16-256`, `stereofloat-1024`, `stereo16-2048`.
**Pegel:** `quiet` (−26 dBFS), `moderate` (−20 dBFS, Standard), `clear` (−14 dBFS).

Es läuft immer nur ein Ton; zwischen Tönen gilt eine kurze Sperre, höchstens 30 Töne pro Start.

### Die Port-State-Messung aktivieren (Opt-in — der eigentliche Nutzen)

Standardmäßig **deaktiviert**. Zum Aktivieren **vor** dem `tone`-Befehl eine leere Datei anlegen:

```
/data/akeno-audiolab/enable-portstate
```

Dann liest das Lab bei jedem Ton drei Mal **read-only** den Zustand des **eigenen** Ports
(`sceAudioOutGetPortState`). Warum das vertretbar ist und was es messen kann, steht in
[SAFETY.md](SAFETY.md) und [HARDWARE-TEST-DE.md](HARDWARE-TEST-DE.md). Wenn du das Risiko nicht
eingehen willst, lass die Datei weg — der Tonlauf funktioniert auch ohne.

## Ergebnisse

Nach jedem Befehl liegen unter `/data/akeno-audiolab/`:

| Datei | Inhalt |
| --- | --- |
| `report.json` | alle Läufe maschinenlesbar, inkl. Port-State (roh + tentatives PS4-Layout) |
| `report.txt` | deutsche Zusammenfassung, Ergebnismatrix und Details je Lauf |
| `status.json` | Lebenszeichen (`RUNNING`/`COMPLETED`), Heartbeat |

Nach jedem Ton **trägst du selbst ein**, ob du ihn gehört hast — das steht nirgends automatisch.
Schick `report.json` und `report.txt` zurück. Die Berichte enthalten keine Tokens, Zugangsdaten,
Nachrichten oder Audioaufnahmen (das Payload hat kein Netzwerk und nimmt nichts auf).

## Ergebnisklassen (report)

- `port-open-failed` — der Port ließ sich nicht öffnen.
- `sample-output-failed` — Samples wurden abgelehnt.
- `accepted-but-not-paced` — angenommen, aber nicht im Playback-Takt.
- `accepted+paced-but-inaudible` — angenommen, getaktet, von dir **nicht gehört** (der bekannte Fall).
- `AUDIBLE (owner-confirmed)` — von dir **gehört** (wäre neu und würde alles ändern).

Ein erfolgreicher Aufruf ist **nie** ein Beweis für hörbaren Ton. „Gehört“ ist immer deine Angabe.
