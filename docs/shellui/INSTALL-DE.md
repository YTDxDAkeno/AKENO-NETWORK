# AKENO NETWORK in der PS5-Oberfläche (ShellUI) – Installation

**Experimentell.** Version 1.10 des Plugins `AKEN00001.elf` wurde gebaut und auf dem PC getestet
(Unit-Tests, Ende-zu-Ende-Test mit echtem Server und Daemon), aber **noch nicht auf einer PS5
ausgeführt**. Die Vorgängerversion 1.00 lief bereits auf deiner PS5 (Firmware 12.20) mit dem
originalen onionHEN. Nichts wird automatisch auf die Konsole übertragen.

## 1. Was du brauchst

| Was | Woher | Hinweis |
| --- | --- | --- |
| PS5, Firmware 12.20, dein bestehender Jailbreak mit **deinem kstuff** und dem ELF-Loader auf Port **9021** | wie bisher | unverändert |
| **AKENO Native-IPC-Daemon** (`akeno-network-ps5-native-ipc.elf`), gekoppelt mit deinem AKENO-Konto | wie bisher | Deine laufende Version `196ca13c71d1-ps5-native-ipc` (Schnittstelle 3) reicht. Im Paket liegt dieselbe Quelle neu gebaut, nur als Ersatz |
| **Original-onionHEN v0.9.0**, unverändert (`OnionHEN.elf` aus dem offiziellen Release von aydencharles/onionHEN, Tag `v0.9.0`, Commit `aaa4264`) | offizielles onionHEN-Release | **Nicht im Paket.** Das ist der Host, mit dem AKENO bei dir schon lief |
| **Nicht** verwenden: den angepassten Host aus PR #14 (`AKENO-ShellUI-Host-EXPERIMENTAL.elf`) | – | Warum: [DIAGNOSE-DE.md](DIAGNOSE-DE.md). Lösche alte Kopien |
| FTP-Zugang zur PS5 (oder USB) | wie bisher | zum Kopieren einer Datei |

## 2. Paket prüfen (am PC)

```
AKENO-NETWORK-PS5-SHELLUI/
  plugin/AKEN00001.elf                       das neue Plugin (1.10)
  daemon/akeno-network-ps5-native-ipc.elf    nur falls du keinen Native-IPC-Daemon hast
  docs/                                      diese Anleitungen, Testplan, Rücknahme, Diagnose
  licenses/                                  GPL-3.0 (Plugin), MIT (AKENO), Apache-2.0 (mbedTLS)
  BUILDINFO.txt                              Commit, SDK, SHA-256 der ELFs
  SHA256SUMS
```

Im entpackten Ordner: `sha256sum -c SHA256SUMS` muss für jede Datei `OK` melden
(Windows PowerShell: `Get-FileHash plugin\AKEN00001.elf -Algorithm SHA256` und mit `BUILDINFO.txt`
vergleichen).

## 3. Sicherung (vorher, einmalig)

Per FTP auf den PC kopieren:

1. `/data/OnionHEN/plugins/AKEN00001.elf` (deine Version 1.00) → z. B. `AKEN00001-1.00.elf`
   auf dem PC. **Nicht** als zweite Datei im Ordner `plugins` liegen lassen.
2. `/data/OnionHEN/config.ini`
3. `/data/akeno/` (Kopplung und Einstellungen des Daemons)

## 4. Installieren und starten

1. **Konsole komplett neu starten** (nicht Ruhemodus) – wichtig, falls vorher ein anderer
   onionHEN-Build oder onionHEN zweimal gestartet wurde.
2. Jailbreak wie gewohnt: Exploit → **dein kstuff** → ELF-Loader auf Port 9021.
3. Den AKENO Native-IPC-Daemon wie bisher an Port 9021 senden; auf seine Benachrichtigung warten.
4. Per FTP `plugin/AKEN00001.elf` nach **`/data/OnionHEN/plugins/AKEN00001.elf`** kopieren
   (ersetzt 1.00; genau dieser Name, keine weiteren Dateien).
5. Das **originale** `OnionHEN.elf` v0.9.0 **einmal** an Port 9021 senden. Auf die
   Toolbox-Benachrichtigung warten. Niemals ein zweites Mal im selben Start senden.
6. **Einstellungen → OnionHEN-Toolbox → Plugins → Installierte ELF-Plugins → „AKENO NETWORK
   (experimental)“**: „Läuft“ einschalten. Lief noch die alte 1.00 (z. B. wegen Autostart),
   „Läuft“ aus- und wieder einschalten. Optional „Autostart“ einschalten.

Kurz: **kstuff → ELF-Loader → AKENO-Daemon → (Plugin-Datei kopieren) → onionHEN einmal → Plugin
starten.**

## 5. AKENO öffnen

**Einstellungen → OnionHEN-Toolbox → AKENO NETWORK.** Die Hauptseite zeigt die Werte direkt in
den Zeilen:

```
AKENO NETWORK
  @dein_name · Online · Spielname       > Profil und Status (Online/Abwesend/Beschäftigt/Unsichtbar)
  Friends · 3 online · 1 playing        > Freunde; je Freund: Nachricht, Unterhaltung, Party
  Messages · 2 unread                   > Unterhaltungen; je Unterhaltung: letzte Nachrichten, Antworten
  Party · Squad · 3/8                   > Mitglieder, Party-Chat, Einladen, Verlassen / Erstellen, Beitreten
  Requests & invitations · 1 waiting    > Freundschaftsanfragen und Einladungen annehmen/ablehnen
  Refresh
  Diagnostics & help
```

Die Oberfläche ist (noch) englisch. Wichtige Bedienpunkte:

- **Antworten / Nachricht schreiben:** „Reply...“ bzw. „Send … a message“ öffnet die
  PS5-Tastatur (bis 250 Zeichen). Nach dem Bestätigen wird gesendet; das Ergebnis erscheint als
  Systembenachrichtigung („Sent to …“).
- **Aktualisierung:** onionHEN zeichnet eine Seite, wenn du sie öffnest. Neue Daten siehst du,
  sobald du eine Seite **verlässt und wieder öffnest** – ohne ShellUI-Neustart. Das Plugin selbst
  holt Änderungen alle 2 Sekunden vom Daemon.
- **Neue Nachrichten** meldet wie bisher der AKENO-Daemon als Systembenachrichtigung.
- „Leave the party“ und „Decline …“ fragen vorher nach.

## 6. Schnellzugriff im Spiel

**Einstellungen → OnionHEN-Toolbox → Controller-Tastenkürzel → „OnionHEN-Toolbox öffnen“ →
„L2 + R3 gehalten“.** (Alternativ in `/data/OnionHEN/config.ini` unter `[shortcuts]`:
`toolbox=l2_r3`.)

Im Spiel **L2 + R3 gedrückt halten** → die Toolbox öffnet sich, das Spiel bleibt im Hintergrund →
**AKENO NETWORK** → Freunde / Nachrichten / Party. Zurück zum Spiel: Einstellungen mit ○ verlassen
oder über die PS-Taste zum Spiel wechseln. „Share tippen“ ist auch möglich, ersetzt dann aber die
Share-Taste.

Das ist **kein** Eintrag im Sony-Kontrollzentrum und keine Game-Base-Integration, sondern die
Toolbox-Seite über ein Tastenkürzel. Warum es (noch) nicht mehr gibt:
[DIAGNOSE-DE.md](DIAGNOSE-DE.md#kontrollzentrum-und-game-base).

## 7. Diagnose

- In AKENO: **Diagnostics & help** – Daemon-Zustand, Schnittstelle, Serververbindung, letzte
  Aktualisierung, letztes Problem.
- Ist der Daemon nicht bereit, sagt die Hauptseite warum und was zu tun ist (z. B. „Load
  akeno-network-ps5-native-ipc.elf …“, „not paired“), mit „Refresh“.
- `/data/OnionHEN/OnionHEN.log` (per FTP) – erwartete Zeilen:
  - `Toolbox online for SceShellUI pid=…` (genau einmal pro Start)
  - `kstuff already running / mprotect OK -- skip launch` oder `kstuff disabled`
  - `[plugins] started AKEN00001 1.10 (AKENO NETWORK (experimental))`
  - `[akeno-ingame] connected to onionHEN`, dann `[akeno-ingame] daemon ready`
- **Neu starten**, wenn du `Detour failed`, `relocation failed` oder `Toolbox already active`
  siehst (siehe [DIAGNOSE-DE.md](DIAGNOSE-DE.md)).

Die Plugin-Logs enthalten nur Zustände, keine Namen, IDs oder Nachrichtentexte.

## 8. Was zu wissen ist (Risiken, ungetestet)

- **onionHEN selbst** (nicht AKENO) hängt seine Toolbox in die ShellUI, mountet `/system` und
  `/system_ex` neu, schreibt `/system_ex/vsh_asset/onionhen.png` und löscht bei jedem Start eine
  heruntergeladene Systemaktualisierung (`/update/PS5UPDATE.PUP`). Das hat bei dir schon
  stattgefunden, als du onionHEN gestartet hast. Das AKENO-Plugin schreibt **keine Datei** und
  nutzt keine Systemfunktion der Konsole (nur C-Bibliothek, Sockets, Threads, Uhren).
- Auf der PS5 **ungetestet**: alle Seiten außer der 1.00-Hauptseite, die PS5-Tastatur auf einer
  Plugin-Seite, Benachrichtigungen des Plugins, das Tastenkürzel während eines Spiels, Ruhemodus.
- Absturz der ShellUI ist möglich (dann kehrt die Konsole zum Startbildschirm zurück oder muss
  neu starten). Spielstände und installierte Spiele werden von AKENO nicht berührt.
- Kein PSN: alles kommt von deinem AKENO-Server über den Daemon.

Rücknahme: [ROLLBACK-DE.md](ROLLBACK-DE.md). Testplan: [TEST-PLAN-DE.md](TEST-PLAN-DE.md).
