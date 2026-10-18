# Diagnose: Original-onionHEN gegen den angepassten Host aus PR #14

Ausführlich (englisch, mit Quellenstellen): [SHELLUI_HOST_DIAGNOSIS.md](../SHELLUI_HOST_DIAGNOSIS.md)
und [SHELLUI_CONTROL_CENTER_RESEARCH.md](../SHELLUI_CONTROL_CENTER_RESEARCH.md).

## Entscheidung

**Der originale, unveränderte onionHEN v0.9.0 bleibt der Host.** AKENO wird nur über dessen
offizielle Plugin-Schnittstelle eingebunden. Der Host aus PR #14 wird **nicht** gebraucht und
nicht weiter verwendet: Alles, was AKENO braucht (Plugin-Manager, Plugin-Brücke, Toolbox-Seite,
dynamische Plugin-Seiten), steckt schon im Original – und genau das lief auf deiner PS5.

## Warum der PR-#14-Host scheiterte

- onionHEN hängt als **ersten, zwingenden** Hook die Funktion `sceRegMgrGetInt` in der ShellUI um.
  Dazu schreibt er an ihren Anfang einen 6-Byte-Sprung (`FF 25 00 00 00 00`) und dahinter 8 Byte
  Zieladresse. **Entfernt werden diese Hooks nie**, nur ein Neustart der ShellUI räumt sie weg.
- „`relocation failed at +6`“ heißt: Ein Toolbox-Start fand am Funktionsanfang bereits so einen
  Sprung vor und versuchte, die alte Zieladresse ab Byte 6 als Maschinenbefehle zu lesen. Die
  ShellUI war also **schon von einem früheren Toolbox-Start umgehängt** (z. B. nach einem
  unvollständigen ersten Start, nach dem Ruhemodus oder durch zwei Hosts im selben Start).
  „`toolbox recovery exhausted`“ ist die Wiederholungsschleife, die viermal an derselben Stelle
  scheitert.
- Ob dieser zweite Versuch scheitert, hängt nur von den Bytes der alten Adresse ab, also vom Aufbau
  des Builds. Am PC mit onionHENs eigenem Code nachgestellt
  (`tools/onionhen/check-double-detour.sh`):
  - **Original v0.9.0:** der zweite Versuch geht **immer durch** (der Fehler bleibt unsichtbar).
  - **PR-#14-Host:** er scheitert bei **43 %** der Ladeadressen, bei **25 % genau mit „+6“**.
- PR #14 hat am Hook-Code nichts geändert; durch das Entfernen anderer Teile ist die Funktion nur
  an eine ungünstige Adresse gerutscht. Er ist also nicht zuverlässiger, sondern scheitert an
  derselben Situation sichtbar.

## Regeln für den Betrieb

1. **onionHEN nur einmal pro Start senden.** Nie zweimal, nie einen anderen onionHEN-Build
   hinterher.
2. **Host wechseln oder neu starten = Konsole komplett neu starten** (nicht Ruhemodus).
3. Im Log `/data/OnionHEN/OnionHEN.log` soll `Toolbox online for SceShellUI` **einmal** stehen.
   Bei `Detour failed`, `relocation failed` oder `Toolbox already active`: neu starten.
4. Nie zwei kstuff: onionHEN muss `kstuff already running … skip launch` melden, oder du setzt in
   `/data/OnionHEN/config.ini` unter `[kstuff]` `autoload=false`.

## Nebenbefund: Gruppen auf Plugin-Seiten

onionHEN v0.9.0 nimmt beim Verlassen einer **Gruppe** (verschachtelte Liste) einen Eintrag zu viel
von seinem Seitenstapel. Danach können Tasten auf der Seite darüber ins Leere gehen. Die alte
Seite 1.00 hatte zwei Gruppen („AKENO“, „Quick actions“). Version 1.10 verwendet **keine Gruppen**
mehr, nur noch Links auf eigene Seiten – die behandelt onionHEN korrekt. Test S16 prüft das.

## Kontrollzentrum und Game Base

| Wunsch | Stand |
| --- | --- |
| AKENO im Spiel öffnen, ohne das Spiel zu beenden | **über das Toolbox-Tastenkürzel** (L2 + R3 halten) → Toolbox → AKENO NETWORK. Das ist eine Funktion von onionHEN selbst; auf 12.20 mit laufendem Spiel noch zu bestätigen (Test B) |
| Eintrag im Sony-**Kontrollzentrum** | **nicht umgesetzt**: onionHEN hat dafür keinen Hook und keine Plugin-Schnittstelle; ein eigener ShellUI-Patch hieße, wieder einen veränderten Host zu pflegen (genau das Problem von PR #14) |
| **Game Base** ersetzen oder erweitern | **nicht umgesetzt**: Game Base ist Sonys PSN-gebundene React-Native-Oberfläche; es gibt keinen geprüften Eingriffspunkt für 12.20 und keinen sicheren Rückfall |
| AKENO im **Optionen-Menü eines Spiels** | möglich wäre es nach dem Muster, mit dem onionHEN dort „Cheats“ einfügt – aber nur als **Änderung am Host**. Vorschlag: bei onionHEN upstream zwei kleine Funktionen anfragen (Tastenkürzel direkt auf eine Plugin-Seite; Plugin-Eintrag im Spiel-Optionsmenü) |
| Overlay über das laufende Spiel | keine Schnittstelle bei irgendeinem geprüften Host |

## Party-Sprache im Spiel

- Sprache funktioniert in **AKENO Native** (Vordergrund-App); das Mikrofon wurde auf 12.20
  bestätigt. `0x809B0081` ist der **Controller**-Fehler von Hintergrundprozessen, kein Audiofehler.
- Ein onionHEN-Plugin ist ein **Hintergrundprozess** wie der Daemon: Es bekommt durch den Host
  keine zusätzlichen Audio-Rechte. Audio direkt in der ShellUI oder im Spielprozess hieße
  Code-Injektion – ausgeschlossen.
- Der nächste prüfbare Schritt ist schon gebaut: das **Voice Lab** (PR #10, Tests A–E) misst
  Mikrofon und hörbaren Ton eines Hintergrundprozesses während eines Spiels. Vor dessen Ergebnis
  wird nichts behauptet.
