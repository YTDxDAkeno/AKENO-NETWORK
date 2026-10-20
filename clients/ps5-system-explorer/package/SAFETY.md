# AKENO System Explorer 6.1 — Sicherheit (SAFETY)

Dieses Payload ist bewusst **nur lesend** und ändert nichts an der Konsole. Was es nicht tut, ist
im Code durch das Import-Gate `console/check-explorer.sh` erzwungen (bei jedem Build geprüft).

## Was das Payload tut

- Läuft als **Hintergrund-Payload** (gleicher Prozesstyp wie der AKENO-Daemon), über den ELF-Loader
  (Port 9021) oder den Homebrew-Launcher (`daemon: true`).
- Liest **nur den eigenen Prozess** (PID, PPID, Prozesssatz per `sysctl KERN_PROC_PID` genau für
  diese PID, App-Info, Ressourcengrenzen) und einige **schreibgeschützte** Systemabfragen
  (Firmware, Vordergrund-App-ID, Vordergrundbenutzer).
- **6.1:** fragt zusätzlich die **Title-ID der laufenden Anwendung** ab
  (`sceSystemServiceGetAppTitleId`, nur lesend) — nur für die vom System selbst gemeldete laufende
  Big-App, nie für beliebige Apps, mit begrenztem Aufruf-Budget und einem geschützten Zielpuffer.
  Abschaltbar über die Datei `no-title-query`.
- **6.1:** hält eine **Dateisperre** (`flock` auf `explorer.lock`) und schreibt ein Lebenszeichen
  (`status.json`), damit ein erneuter Start keine zweite Diagnosesitzung erzeugt und ein Absturz
  die nächste Sitzung nicht verfälscht. Beides ist dateibasiert und ändert nichts am System.
- **Sucht** Systembibliotheken und Symbole nur nach (`dlopen` + `dlsym`); gefundene Adressen werden
  mit NULL verglichen und **nie aufgerufen**.
- Schreibt eine zeitlich begrenzte Diagnose-Sitzung (Standard 5 Minuten, hartes Maximum 30 Minuten)
  nach `/data/akeno-system-explorer/` und beendet sich sauber.

## Was das Payload NICHT tut (hartes Verbot, CI-geprüft)

- **Keine** Audioausgabe und **kein** Mikrofon in diesem Build. Die ELF importiert kein einziges
  `sceAudioOut`/`sceAudioIn`-Symbol (geprüft).
- Kein Start/Beenden von Apps, kein Mounten, kein `ptrace`, keine rohen Syscalls, keine
  Kernel-Speicher-Helfer des SDK, kein Controller-Zugriff, keine Registry, keine Install-Utilities.
- Keine Fokus-/Attach-/Routing-/Lautstärke-/HDMI-Funktion wird aufgerufen (die Namen werden nur als
  nicht belegte Einträge aufgelistet).
- Kein Zugriff auf den privaten Speicher fremder Prozesse; fremde Prozesse werden nicht gescannt.
- Keine ShellCore-/ShellUI-Patches, keine firmwarespezifischen Offsets, keine Injektion in andere
  Prozesse, keine NP-/Account-/DNS-/PSN-Änderung, kein Netzwerk-Probing, keine VPS-Bereitstellung.
- Die als **unsicher markierten** Bibliotheken `libSceVideoRecording.sprx` und
  `libSceGameLiveStreaming.sprx` (deren `dlopen` auf 12.20 in früherer AKENO-Forschung hängenblieb)
  werden **nie geladen** — nur ihr Dateivorhandensein wird gemeldet.

## Protokolle und Datenschutz

Die Reporte enthalten nur gemessene Zustände, Codes und Zähler. Automatisch entfernt werden:
Passwörter, PSN-/AKENO-Tokens (`akd_…`, `akp_…`, `Bearer …`, `X-Akeno-*-Key`), lange Hex-Schlüssel,
Hostnamen in URLs (außer Loopback). Es werden keine Nachrichteninhalte und kein Mikrofon-Audio
aufgezeichnet.

## Optionaler aktiver Audio-Probe (nicht in diesem Build)

Ein Build **mit** aktivem Audio-Probe (`AKENO_EXPLORER_AUDIO_PROBE`) ist bewusst nicht Teil dieser
ELF. Ein solcher Probe würde einen Audio-Port öffnen und braucht eine eigene Review, eine eigene
Freigabe und einen eigenen Hardwaretestplan.

## Abbruch und Rückbau

Ein Absturz des Payloads beendet nur das Payload. Nichts bleibt auf der Konsole zurück außer den
Dateien unter `/data/akeno-system-explorer/`, die gefahrlos gelöscht werden können. Es ist kein
Installations- oder Rückbauschritt nötig.
