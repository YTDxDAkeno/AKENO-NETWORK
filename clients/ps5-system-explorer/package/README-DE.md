# AKENO System Explorer 6.0 — Installation und Nutzung (DEUTSCH)

Der AKENO System Explorer ist ein eigenständiges, **nur lesendes** PS5-Diagnose-Payload. Es sammelt
Belege über die aktuelle Prozess- und Anwendungsumgebung aus legitimen, verfügbaren
Systemschnittstellen, damit die nächste Entscheidung zu Game Base und In-Game-Voice auf gemessenen
Fakten beruht. Es verändert die Konsole nicht, öffnet **keinen Audio-Port und kein Mikrofon** und
beendet sich von selbst.

> Dieses Dokument beschreibt die Vorbereitung. Der eigentliche Konsolentest ist in
> [HARDWARE-TEST-DE.md](HARDWARE-TEST-DE.md) beschrieben und durch diesen Leitfaden **nicht**
> freigegeben. Sicherheitsregeln: [SAFETY.md](SAFETY.md).

## Voraussetzungen

| Komponente | Status |
| --- | --- |
| PS5 mit Firmware 12.20, Jailbreak (kstuff) | vorhanden beim Eigentümer |
| ELF-Loader auf Port 9021 **oder** ein Homebrew-Launcher (OnionHEN/etaHEN/Payload Manager) | einer davon genügt |
| AKENO-Daemon | **nicht nötig** — der Explorer läuft eigenständig und unabhängig |

Der Explorer ist von OnionHEN, der AKENO-Plugin-Seite und AKENO Native **unabhängig** und ändert an
keiner dieser Komponenten etwas.

## Paketinhalt

```
AKENO-System-Explorer-6.0/
  akeno-system-explorer.elf      # das Payload (für den ELF-Loader)
  AKENOSystemExplorer/           # Launcher-Ordner (für den Homebrew-Launcher)
    akeno-system-explorer.elf
    homebrew.js
  README-DE.md                   # dieses Dokument
  HARDWARE-TEST-DE.md            # der Konsolentestplan
  SAFETY.md                      # Sicherheitsregeln
  SHA256SUMS.txt                 # Prüfsummen
```

## Prüfsummen kontrollieren

```sh
cd AKENO-System-Explorer-6.0
sha256sum -c SHA256SUMS.txt
```

Nur eine ELF mit passender Prüfsumme laden. Die ELF darf vor dem Laden geprüft werden (sie ist klein
und importiert nur die wenigen, dokumentierten, schreibgeschützten Funktionen aus
[SAFETY.md](SAFETY.md)).

## Start — zwei Wege

**A) ELF-Loader (Port 9021), wie beim AKENO-Daemon.** `akeno-system-explorer.elf` an den Loader
senden. Es startet als Hintergrund-Payload.

**B) Homebrew-Launcher.** Den Ordner `AKENOSystemExplorer/` neben die anderen AKENO-Einträge in den
Homebrew-Ordner des Launchers legen und „AKENO System Explorer“ starten. `daemon: true` sorgt dafür,
dass der Launcher **kein** laufendes Spiel schließt.

## Was beim Start passiert

1. Eine Systembenachrichtigung erscheint: „AKENO System Explorer started (read-only diagnostics)…“.
2. Der Explorer läuft eine **begrenzte Sitzung** (Standard 5 Minuten) und beobachtet den Kontext
   (Vordergrund-App, Vordergrundbenutzer) einmal pro Sekunde.
3. Am Ende erscheint „AKENO System Explorer finished. Reports written.“ und das Payload beendet sich.

Es läuft nur eine Instanz gleichzeitig (eine Sperrdatei verhindert einen zweiten Start).

## Ereignismarker setzen (optional)

Damit Beobachtungen mit deinen Aktionen (Home-Bildschirm, Spielstart, AKENO-Menü, Party)
korreliert werden können, kannst du **Marker** setzen, ohne dass der Explorer fremde Oberflächen
inspiziert. Hänge dazu Zeilen an die Datei `/data/akeno-system-explorer/control` an (per FTP oder
der Dateiansicht des Launchers). Je Zeile ein Befehl:

| Befehl | Wirkung |
| --- | --- |
| `mark <Text>` | Setzt einen Marker mit deinem Text (z. B. `mark Spiel gestartet`). |
| `extend <Minuten>` | Verlängert die Sitzung um die Minuten (max. 30 Minuten gesamt). |
| `stop` | Beendet die Sitzung sofort und sauber. |

Beispiel (Inhalt der Datei `control`):

```
mark Home-Bildschirm
mark Spiel gestartet
extend 5
mark AKENO-Overlay geoeffnet
stop
```

## Ergebnisse

Nach dem Lauf liegen unter `/data/akeno-system-explorer/`:

| Datei | Inhalt |
| --- | --- |
| `report.json` | der komplette Lauf (System, Kontext, Modulzusammenfassung, Timeline, alle Proben) |
| `modules.json` | die Proben je Modul A–E |
| `audio-diagnostics.json` | Modul C im Detail (kontextabhängige Audio-Lage, frühere Hardware-Belege) |
| `events.jsonl` | die Timeline, ein JSON-Objekt pro Zeile |
| `system-summary.txt` | eine lesbare Zusammenfassung (gut zum Abfotografieren) |

Hole diese Dateien per FTP oder Dateiansicht ab und schicke sie zurück. Die Reporte enthalten keine
Passwörter, Tokens, Schlüssel, Nachrichten oder Mikrofon-Audio (automatische Redaktion).

## Wie die Statusangaben zu lesen sind

- `OK` — in diesem Lauf gemessen.
- `NOT_AVAILABLE` — die Schnittstelle oder der Wert ist in diesem Kontext nicht verfügbar.
- `UNSUPPORTED_ON_FW_12_20` — auf dieser Firmware nicht exportiert/unterstützt.
- `SKIPPED` — bewusst nicht ausgeführt (z. B. unter Quarantäne stehende Bibliothek, oder der
  deaktivierte Audio-Probe).
- `ERROR` — ein Aufruf hat einen Fehlercode zurückgegeben (der Code steht dabei).

Belegstufen: `HARDWARE_VERIFIED` (auf 12.20 gemessen), `SOURCE_VERIFIED` (in einer zitierten Quelle
oder dem SDK belegt), `HYPOTHESIS` (plausibel, nicht bewiesen), `UNKNOWN`.

**Ein erfolgreicher Aufruf oder ein auflösbares Symbol ist nie ein Beweis für hörbaren Ton,
funktionierende Sprachübertragung oder Game-Base-Zugriff.**
