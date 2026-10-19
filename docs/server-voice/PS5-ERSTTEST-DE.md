# Erster echter PS5-Sprachtest (nach dem Server-Update)

Erst ausführen, wenn das Server-Update freigegeben, ausgeführt und mit `voice-upgrade.sh verify`
geprüft ist ([DEPLOY-DE.md](DEPLOY-DE.md)). Vorher kann die PS5 nur „HTTP 404" melden.

## Was auf der PS5 läuft (nichts davon wird neu gebaut)

| Teil | Stand |
| --- | --- |
| OnionHEN-Host | der funktionierende **Phase-4.1-Host** (PR #17), unverändert, `toolbox_target_mode=root` |
| AKENO-Daemon | der vorhandene **Native-IPC-Daemon, Schnittstelle 3** (holt den Sprachpass für die Konsole) |
| Plugin | **AKENO 1.20** aus PR #18 (Seite „Voice chat") |
| Sprachdienst | **`akeno-party-voice.elf` 0.1.0-alpha** aus PR #18 |

Installation und Rückweg der PS5-Teile: die Anleitung in PR #18
(`docs/shellui/voice-alpha/INSTALL-DE.md`, `VOICE-TEST-DE.md`, `DIAGNOSTICS-DE.md`).

## Vorbereitung

1. Website → *Settings → Security → Paired devices*: die PS5 hat die Berechtigung
   **„Create, join and leave parties and invite friends"** (`parties:write`).
2. Du und ein Freund seid in **derselben AKENO-Party**; der Freund nutzt die Website am PC (oder
   das Handy) und tritt dort dem Sprachchat bei.
3. Auf dem VPS mitlesen: `docker compose logs -f voice-gateway`.

## Die Tests (jeweils einzeln notieren)

| Test | Ablauf | Bestanden, wenn |
| --- | --- | --- |
| **A Netzwerk** | L2 + R3 → AKENO → *Voice chat* → *Join voice chat* | die PS5 zeigt *Connected*, der Freund sieht dich im Sprachchat, das Gateway-Log zeigt `console voice session live` |
| **B Eingehend** | der Freund spricht | **du hörst ihn** auf der PS5 (Route 1; sonst Routen 2–4 im Menü probieren, Testton zählt N Pieptöne auf Route N) |
| **C Ausgehend** | *Turn on microphone* (Bestätigung), du sprichst | **der Freund hört dich** |
| **D Im Spiel** | ein Spiel starten oder fortsetzen, weiterreden | Sprache läuft im Spiel weiter (beide Richtungen) |
| **E Stabilität** | AKENO mit L2 + R3 mehrmals öffnen und schließen | die Verbindung bleibt, kein zweites Beitreten, Controller normal |
| **F Diagnose** | danach sammeln | siehe unten |

## Diagnose sammeln (F)

- PS5 (per FTP): `/data/akeno-party-voice/report.txt` und `voice.log` (enthalten keine Namen,
  IDs, Pässe, Schlüssel oder Audio).
- VPS: `docker compose logs --since 30m voice-gateway > ~/gateway.txt` und
  `docker compose logs --since 30m app | grep -i voice > ~/app-voice.txt`.

## Wie die Ergebnisse eingeordnet werden

| Ergebnis | Einordnung |
| --- | --- |
| A nein, „HTTP 404" | Server noch nicht aktualisiert |
| A nein, „HTTP 403" | der PS5 fehlt `parties:write` (siehe Vorbereitung 1) oder du bist nicht in der Party |
| A nein, „503" / *Console voice is not set up* | `VOICE_GATEWAY_URL` fehlt (Rückweg-Stufe 1 aktiv?) |
| A ja, B/C nein, der Bericht zeigt aber *NETWORK CONNECTED* und dekodierte Pakete | **Server funktioniert**; es ist eine **Audio-Grenze der Konsole** (Hintergrund-Wiedergabe bzw. Mikrofon auf FW 12.20), kein Server-Fehler |
| B ja, C nein (Mikrofon nur Nullen / verweigert) | nur Zuhören im Spiel |
| C ja, B nicht hörbar | nur Sprechen im Spiel; Zuhören über Handy/PC |
| alles ja | Ziel erreicht; danach längere Sitzung, Latenz, Echo |

**Hintergrund-Audio und Mikrofonaufnahme gelten erst als funktionierend, wenn du den Freund
tatsächlich hörst und er dich hört.** Ein PC-Test beweist nur die Leitung, nicht die Konsole.
