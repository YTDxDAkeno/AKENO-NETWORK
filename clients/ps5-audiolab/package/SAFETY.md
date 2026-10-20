# AKENO Audio Lab 3.1 — Sicherheit, Abbruch und Rückkehr (SAFETY)

Dieses Payload ist ein **eigenständiges, isoliertes** Experiment. Was es nicht tut, ist im Code
durch das Import-Gate `console/check-audiolab.sh` erzwungen (bei jedem Build geprüft).

## Was das Payload tut

- Läuft als **Hintergrund-Payload** (gleicher Prozesstyp wie der AKENO-Daemon), über ELF-Loader
  oder Launcher (`daemon: true`).
- Wartet auf datei-gesteuerte Befehle und spielt einen Ton **nur** nach deinem `tone`-Befehl.
- Öffnet dafür **einen** Audio-Port (System-Benutzer `0xFF`, Haupt-Port 0), schreibt den Ton,
  schließt den Port wieder. Genau diese Aufrufe und Parameter stammen unverändert aus dem bereits
  auf 12.20 gelaufenen Audio Lab v3.
- Schreibt ausschließlich nach `/data/akeno-audiolab/`.

## Was das Payload NICHT tut (hartes Verbot, CI-geprüft)

- **Kein** Mikrofon. `libSceAudioIn` wird nirgends referenziert.
- **Keine** Fokus-/Attach-/Routing-/Lautstärke-/HDMI-Funktion (`sceAudioOutSetVolume`,
  `…GetFocusEnablePid`, `…AttachToApplicationByPid`, `…ChangeAppModuleState`, …). Nicht aufgerufen.
- **Kein** Netzwerk: kein Socket, keine Weboberfläche, Port 8642 wird nicht belegt, kein VPS, kein
  LiveKit. Kein Zugriff auf `/data/akeno`, keine Zugangsdaten, keine Pairing-Daten.
- Kein Start/Beenden von Apps, kein Mounten, kein `ptrace`, keine Kernel-Speicher-Helfer, kein
  Controller, keine Registry, keine ShellUI-/ShellCore-Patches, keine Injektion.
- Die Audio-Ausgabefunktionen sind **keine** statischen Importe: Sie werden erst zur Laufzeit per
  `dlopen`/`dlsym` aufgelöst (geprüft: die ELF importiert kein `sceAudio*`-Symbol).
- Es läuft nur **eine** Instanz (Dateisperre `flock`). Ein zweiter Start wird abgewiesen — zwei
  Audio-Sitzungen können nicht parallel entstehen.

## Der optionale Port-State-Aufruf (`sceAudioOutGetPortState`)

**Standardmäßig deaktiviert.** Er läuft nur, wenn du die Datei `enable-portstate` anlegst.

- Er wird **nur auf dem eigenen Handle** dieses Tests aufgerufen, nie auf dem eines Spiels.
- Die Signatur `(int32 handle, void* state)` ist in fünf unabhängigen Quellen gleich (OpenOrbis,
  shadPS4, fpPS4, SharpProspero, AnyPS5); die Struktur ist dort 24–32 Byte groß.
- **Begründung der Puffergröße (nicht allein Prüfbytes):** Der Zielpuffer ist ein **256-Byte-Fenster
  innerhalb einer 4-KB-Arena** — also rund **8× größer** als das größte dokumentierte Layout.
  Selbst wenn 12.20 etwas mehr schreibt als die PS4-Doku, bleibt ein großer Sicherheitsabstand.
- Die 64 Prüfbytes vor und die >3,7 KB hinter dem Fenster sind eine **zusätzliche** Erkennung: Wird
  ein Überlauf bemerkt, wird die Abfrage für den Rest des Laufs **dauerhaft gestoppt** und als
  Fehler gemeldet; es wird nichts dekodiert.
- **Grenze:** Prüfbytes erkennen einen Überlauf erst **danach**; eine falsche Signatur oder ein
  unerwartet großer Schreibzugriff über 3,8 KB hinaus könnten theoretisch Schaden anrichten, bevor
  die Prüfung greift. Deshalb ist der Aufruf Opt-in und nicht Standard. Wenn du dieses Restrisiko
  nicht eingehen willst, lege die Datei einfach nicht an — der Tonlauf funktioniert auch ohne, und
  die dekodierten Felder gelten ohnehin nur als Indiz (PS4-Layout, auf 12.20 unverifiziert).

## Protokolle und Datenschutz

Die Berichte enthalten nur gemessene Zustände, Codes, Zähler und deine eigene „gehört“-Angabe. Es
gibt keine Tokens, keine Zugangsdaten, keine Nachrichten und **keine Audioaufnahme** (es wird nichts
aufgenommen und nichts gesendet).

## Abbruch und Rückbau

- `stop` bricht einen laufenden/wartenden Ton ab; `exit` beendet die Sitzung sauber.
- Ein Absturz beendet nur dieses Payload. Die Dateisperre wird vom Kernel freigegeben; ein erneuter
  Start funktioniert danach.
- Es bleibt nur der Ordner `/data/akeno-audiolab/` zurück, der jederzeit gefahrlos gelöscht werden
  kann. Es ist kein Installations- oder Rückbauschritt nötig.
- OnionHEN-Host, AKENO NETWORK, die Party-Voice und Spiele bleiben unberührt.
