# Phase 4 – Sprach-Party im Spiel: Stand, Blocker und der nächste Hardware-Test

**Kurz:** Sprache während eines Spiels funktioniert auf der PS5 **noch nicht** und wird nicht
behauptet. Party-Sprache funktioniert in AKENO Native (Vordergrund-App, ohne laufendes Spiel). Für
das Spiel fehlen zwei Fakten, die bisher niemand auf deiner PS5 gemessen hat: Liefert das Mikrofon
einem **Hintergrundprozess** echten Ton, während ein Spiel läuft? Und ist dessen Tonausgabe dabei
**hörbar**? Genau das misst die **AKENO Voice Lab** (`voice/akeno-voicelab.elf`). Erst nach ihrem
Bericht wird der Sprachdienst gebaut.

## Was wir sicher wissen (Belege im Repository)

| Fakt | Beleg |
| --- | --- |
| `0x809B0081` ist die Ablehnung von **`scePadOpen`** (Controller) für Hintergrund-Payloads, kein Audiofehler. Die Bibliotheksfehler des Controllers sind `0x8092xxxx`; `0x809B…` kommt von einer anderen Stelle. Derselbe Code erscheint auf 13.60 bei einem fremden Payload | Alpha-0.5-Hardwaretest (12.20), `docs/QUICK_ACCESS.md`, AnyPS5 #3177 |
| Das neue Tastenkürzel betrifft das **nicht**: Die ShellUI liest den Controller selbst | onionHEN `hook_boot.cpp` (`GamePad.GetData`) |
| Mikrofon in der Vordergrund-App (AKENO Native 1.1): öffnet, nimmt auf, Pegel folgt der Stimme | Hardwaretest 12.20 |
| Tonausgabe eines Hintergrund-Payloads: angenommen, aber **nicht hörbar** (Startbildschirm, Browser) | Audio Lab v2, 12.20 |
| Mikrofon eines Hintergrund-Payloads, Ton **während eines Spiels** | **nie gemessen** |
| Das AKENO-Plugin AKEN00001 ist ein **eigener Prozess** (von onionHENs Plugin-Manager über dessen ELF-Loader gestartet), genau wie der Daemon. Als onionHEN-Plugin bekommt es **keine** Audio- oder Mikrofonrechte; die Plugin-Schnittstelle bietet nur Log, Benachrichtigung und eigene Einstellungen | onionHEN `plugin_host_services.cpp`, Plugin-SDK `services.h` |

Darum gehören Sprachversuche **nicht** ins Plugin: Ein fehlgeschlagener Audioversuch soll Nachrichten
und Party-Anzeige nie gefährden.

## Der Test (ca. 5–7 Minuten)

Voraussetzungen: AKENO-Daemon läuft wie immer; Headset am DualSense, wenn möglich (notieren, was du
benutzt). Der Phase-4-Host ist dafür **nicht** nötig (geht mit Original und Phase 4).

1. `voice/akeno-voicelab.elf` **vom Startbildschirm** an den ELF-Loader (Port 9021) senden, wie den
   Daemon. (Oder mit `homebrew.js` aus dem Native-Paket in den Homebrew-Launcher legen.)
2. Den Benachrichtigungen folgen:
   - **A Startbildschirm:** „Jetzt sprechen“ → 6 Sekunden sprechen.
   - **B Startbildschirm:** vier Piep-Folgen (1, 2, 3, 4 Pieptöne). **Merken, welche du hörst.**
   - „Starte jetzt ein Spiel“: ein Spiel mit durchgehendem Ton starten (z. B. Atelier Yumia) und
     spielen. Die Lab wartet bis 300 s.
   - **A/B im Spiel:** wieder sprechen, Pieptöne zählen.
   - **C Echo:** 10 Sekunden sprechen; hörst du dich selbst mit kurzer Verzögerung?
   - **E:** Lief der Spielton dabei weiter?
3. Bei „finished“: `/data/akeno-voicelab/report.txt` per FTP holen, jede `____`-Stelle ausfüllen
   (gehörte Piep-Zahlen zu Hause/im Spiel, eigenes Echo ja/nein, Spielton ja/nein) und zurückschicken.

Die Lab schreibt nur `/data/akeno-voicelab/`, liest `/data/akeno/` nur, nimmt nichts auf und
sendet nichts. Sie läuft nur einmal (Port 8644) und beendet sich selbst. Stürzt sie ab, endet nur
sie. Bei Rucklern: Spiel schließen oder neu starten, nichts bleibt zurück.

## Was der Bericht entscheidet

| A (Mikrofon im Spiel) | B (Piep im Spiel gehört) | C (Echo) | Nächster Schritt |
| --- | --- | --- | --- |
| `SOUND` | ja (Variante n) | ja | AKENO-Sprachdienst als Hintergrund-Payload mit Variante n bauen, mit der bestehenden Party-/LiveKit-Anbindung (Voice-Gateway). Dann 10-Minuten-Party im Spiel testen |
| `SOUND` | nein | nein | nur **Sprechen** im Spiel möglich (Party hört dich, du hörst sie nicht). Nur wenn du das willst, klar beschriftet |
| `ALL_ZERO` / `OPEN_FAILED` | ja | – | nur **Zuhören** im Spiel |
| `ALL_ZERO` / `OPEN_FAILED` | nein | – | mit öffentlichen Schnittstellen keine Sprache im Hintergrund auf 12.20. Nächste Forschungsstufe: Audio im ShellUI-Prozess selbst (über eine Host-Erweiterung), erst nach deiner Freigabe |

Wichtige Zeilen im Bericht: `[Summary]`, `A.game.mic=…`, `B.game.toneN heard=…`,
`D.game.heartbeat_gap_max_ms` (groß = das System pausiert Hintergrundprozesse im Spiel).

## Was danach kommt (nur mit positivem Bericht)

Der Sprachdienst wird ein **eigener** Hintergrundprozess, getrennt vom Plugin und vom Daemon. Er
übernimmt AKENO Natives Sprachkern unverändert (DSP, Opus, Jitter, Voice-Protokoll) und den
bestehenden Voice-Gateway des Servers. Die AKENO-Partyseite bekommt dann Beitreten / Stumm /
Taub / Teilnehmer / Verlassen. Nichts davon wird als „funktioniert im Spiel“ bezeichnet, bevor es
auf deiner PS5 im Spiel getestet ist.
