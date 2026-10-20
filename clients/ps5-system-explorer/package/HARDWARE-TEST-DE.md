# AKENO System Explorer 6.0 — Hardwaretestplan (DEUTSCH)

Dieser Plan ist für einen **später, separat freizugebenden** Konsolentest gedacht. Er wird hier
**nicht** ausgeführt. Der Test ist nur lesend: der Explorer öffnet keinen Audio-Port und kein
Mikrofon, ändert nichts an Account, PSN, DNS oder Firmware und schließt kein Spiel.

**Firmware:** 12.20 · **Umgebung:** kstuff, ELF-Loader oder Homebrew-Launcher · **Dauer:** ca. 6–8
Minuten.

## Vorbereitung

1. Prüfsummen kontrollieren (`sha256sum -c SHA256SUMS.txt`), siehe [README-DE.md](README-DE.md).
2. Eine Möglichkeit bereitlegen, die Datei `/data/akeno-system-explorer/control` zu beschreiben und
   `/data/akeno-system-explorer/` auszulesen (FTP oder die Dateiansicht des Launchers).
3. Ein Spiel bereithalten, das später gestartet wird.

## Testablauf

| Schritt | Aktion | Erwartete Beobachtung | Mögliche Fehlzustände |
| --- | --- | --- | --- |
| 1 | **Vom Home-Bildschirm aus** `akeno-system-explorer.elf` laden (ELF-Loader) oder im Launcher starten | Benachrichtigung „AKENO System Explorer started …“; eine `explorer.lock` entsteht | Keine Benachrichtigung → ELF nicht gestartet (Loader/Launcher prüfen) |
| 2 | Kurz warten und `control` anlegen mit einer Zeile `mark Home-Bildschirm` | nichts sichtbar; der Marker landet später in der Timeline | — |
| 3 | **Spiel starten** und `control` um `mark Spiel gestartet` ergänzen | der Explorer läuft weiter; Spiel startet normal | Spiel ruckelt → Spiel schließen beendet alles; nichts bleibt zurück |
| 4 | Prüfen, ob der Explorer noch reagiert: `control` um `mark im Spiel` ergänzen | der Marker wird aufgenommen (die Sitzung läuft im Hintergrund weiter) | Lange Scheduling-Lücke → wird als Beobachtung notiert, **nicht** als Suspendierung gewertet |
| 5 | **Vorhandenes AKENO-Overlay** (L2+R3) öffnen und wieder schließen; `control` um `mark AKENO-Overlay auf` bzw. `mark AKENO-Overlay zu` ergänzen | Overlay verhält sich wie gewohnt; Marker landen in der Timeline | — |
| 6 | Optional Game-Base-/Party-UI öffnen und mit `mark …` kennzeichnen | nur dein Marker wird gespeichert; keine fremde Oberfläche wird inspiziert | — |
| 7 | Mit `stop` in `control` die Aufnahme sauber beenden (oder die 5 Minuten ablaufen lassen) | Benachrichtigung „AKENO System Explorer finished. Reports written.“ | — |
| 8 | Reporte aus `/data/akeno-system-explorer/` holen | fünf Dateien (siehe README) mit echten Werten | Datei fehlt → Schreibfehler, in der Benachrichtigung genannt |

## Was die Ergebnisse zeigen sollen

- **Modul A / Kontext:** Der Explorer sollte sich als **Hintergrundprozess** einordnen (eigene
  App-ID 0 bzw. keine, laufende Big-App = Spiel oder Home). Das ist die Variable, die in früherer
  AKENO-Forschung über hörbares/stummes Audio entschied.
- **Timeline:** Der Wechsel Home → Spiel sollte als `context_change` erscheinen, mit passender
  Big-App-ID, zeitlich zu deinen Markern. Scheduling-Lücken (falls der Prozess während des Spiels
  seltener läuft) werden als Messwert notiert, nicht gedeutet.
- **Modul B:** Welche Audio-/User-/System-Bibliotheken auf 12.20 vorhanden und ladbar sind und
  welche Symbolnamen auflösbar sind (nur nachgeschlagen). Die unter Quarantäne stehenden
  Aufnahme-/Broadcast-Bibliotheken werden **nicht** geladen.
- **Modul C:** Die frühere Hardware-Lage (stumme Hintergrundausgabe, `0x809B0081` Mikrofon,
  `0x80260011`) wird als Beleg wiedergegeben; dieser Lauf misst sie **nicht** erneut (kein Port,
  kein Mikrofon).

## Sichere Beendigung

Jederzeit: `stop` in `control` schreiben, oder das Payload über den Loader/Launcher beenden, oder im
Notfall das Spiel schließen bzw. die Konsole neu starten. Es bleibt nur der Ordner
`/data/akeno-system-explorer/` zurück, der gefahrlos gelöscht werden kann.

## Ausdrücklich nicht Teil dieses Tests

Keine Audioausgabe, kein Mikrofon, keine Fokus-/Attach-/Routing-Funktion, keine Account-, PSN-,
DNS- oder Firmware-Änderung, kein Schließen eines Spiels durch den Explorer. Der optionale aktive
Audio-Probe ist ein **getrennter**, eigens freizugebender Test (siehe
[docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md](../../../docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md),
Abschnitt „Nächste Experimente“).
