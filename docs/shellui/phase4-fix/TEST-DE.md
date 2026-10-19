# AKENO NETWORK Phase 4.1 – Hardware-Testplan (FW 12.20)

**Den fehlgeschlagenen Phase-4-Host nicht erneut testen.** Dieser Plan gilt nur für
`host/OnionHEN-AKENO-direct-fixed.elf`. Die Schritte bauen aufeinander auf: **erst wenn ein Schritt
bestanden ist, kommt der nächste.**

## Grundregeln

- **Abbruch beim ersten Problem:** Controller reagiert nicht, schwarzer Bildschirm, Fokus weg,
  Menüs reagieren nicht, ShellUI startet neu, unerwartete Toolbox-Benachrichtigung oder eine der
  Abbruch-Zeilen unten im Log → **sofort aufhören**, Konsole neu starten, Original-Host
  ([ROLLBACK-DE.md](ROLLBACK-DE.md)), Logs sichern (H10) und melden. Nicht „noch einmal probieren“.
- **Wenn der Controller nicht mehr reagiert:** 30 Sekunden warten (das Log schreibt dann noch
  Diagnosezeilen), PS-Taste probieren. Reagiert nichts: Power-Taste an der Konsole gedrückt halten,
  bis sie ausgeht (zweiter Piepton), danach normal starten – mit dem **Original**-Host.
- Pro Start nur **ein** onionHEN. Nach jeder Änderung der `config.ini`: Konsole neu starten.
- Vor jedem Test mit Spiel: **im Spiel speichern.**
- Nach jedem Schritt kurz notieren: bestanden / nicht bestanden, Uhrzeit, was zu sehen war.

**Abbruch-Zeilen im Log** (`/data/OnionHEN/OnionHEN.log`):

```text
Detour failed                      relocation failed               Toolbox already active
[plugin-bridge] ShellUI disconnected   (während eines Tests)
Activating toolbox... / Toolbox online for SceShellUI   (ein ZWEITES Mal im selben Start)
AKENO_DIRECT: SceShellUI pid=… starts while a direct-entry session … was open
AKENO_DIRECT: root mode off until the console restarts
```

## H0 – Vorbereitung (Rückweg sichern)

1. Paket prüfen: `sha256sum -c SHA256SUMS` → alles `OK` ([INSTALL-DE.md](INSTALL-DE.md#1-paket-prüfen-am-pc)).
2. Sicherungen nach [INSTALL-DE.md](INSTALL-DE.md#2-sichern-vor-dem-ersten-test): `config.ini`,
   Original-`OnionHEN.elf` (`1f1f7c88…7c212`), alte Logs kopieren und umbenennen.
3. **Rückweg einmal üben:** Konsole komplett neu starten → kstuff → Loader → Daemon →
   **Original**-`OnionHEN.elf` → im Spiel L2 + R3 → Toolbox → Plugins → AKENO NETWORK → ○ zurück.
   Funktioniert das (wie am 19.10. bestätigt), ist der Rückweg geprüft.
4. `config.ini` für H1–H4 einstellen ([INSTALL-DE.md](INSTALL-DE.md#4-configini-für-die-tests-h1-bis-h4),
   `toolbox_target=toolbox`, `plugin_entry=none`).

## H1 – Neuer Host startet

Konsole **komplett neu starten** → kstuff → Loader → Daemon → **`OnionHEN-AKENO-direct-fixed.elf`**
einmal senden. Toolbox-Benachrichtigung abwarten, 1 Minute warten.

**Bestanden, wenn** im Log: `starting OnionHEN (0x1220)`, `[plugin-bridge] ShellUI connected`,
genau **ein** `Toolbox online for SceShellUI pid=… (ready protocol)`, `[plugins] started AKEN00001 1.10`,
und keine Abbruch-Zeile.

## H2 – Startbildschirm, DualSense, normale Einstellungen

Ohne Spiel: Startbildschirm mit dem Controller bedienen, PS-Taste (Kontrollzentrum) öffnen und
schließen, **Sony-Einstellungen** öffnen, zwei Untermenüs öffnen, mit ○ zurück zum Startbildschirm.

**Bestanden, wenn** alles normal reagiert.

## H3 – Original-Toolbox

1. Einstellungen → OnionHEN-Toolbox öffnen, durch zwei Bereiche blättern, mit ○ hinaus.
2. Auf dem Startbildschirm **L2 + R3 eine Sekunde halten** → die Toolbox öffnet sich → ○.

**Bestanden, wenn** beides wie beim Original funktioniert (Toolbox-Startseite wie immer, ohne
AKENO-Eintrag oben – `toolbox_target=toolbox`).

## H4 – AKENO über den bekannten Weg

Toolbox → Plugins → AKENO NETWORK → Freunde, Nachrichten öffnen → ○ bis zum Startbildschirm.

**Bestanden, wenn** AKENO wie bisher funktioniert. Damit ist der neue Host mit Original-Verhalten
bestätigt. **Wenn H1–H4 nicht bestehen:** Abbruch – der Fehler liegt dann am Host-Build selbst,
nicht am Direktzugang. Bitte melden.

## H5 – Link-Modus (der empfohlene Schnellzugang)

`config.ini` auf den Link-Modus stellen ([INSTALL-DE.md](INSTALL-DE.md#6-später-link-modus-h5-und-direkt-modus-h6h8)),
Konsole neu starten, Host laden (H1). Im Log beim Start:
`AKENO_DIRECT: shortcut target AKEN00001, link mode`.

**H5a, Startbildschirm (3×):** L2 + R3 halten → Toolbox öffnet sich, **„AKENO NETWORK“ steht oben
und ist ausgewählt** → ✕ → AKENO-Hauptseite → Freunde → ○ → ○ (Toolbox) → ○ (Startbildschirm).
Im Log je Durchgang: `AKENO_DIRECT: shortcut l2_r3 -> Toolbox, AKEN00001 first (link mode)`.

**H5b, im Spiel (nach dem Speichern, 2×):** L2 + R3 halten → Toolbox mit AKENO oben → ✕ → AKENO →
Nachrichten öffnen → ○ ○ ○ → **dasselbe Spiel**. Danach 1 Minute spielen: L2, rechter Stick, alle
Tasten.

**Bestanden, wenn** alles reagiert und das Spiel normal weiterläuft. Ab hier hast du einen stabilen
Zwei-Schritt-Zugang (L2 + R3, ✕). **Wenn du nur diesen willst, kannst du hier aufhören** und direkt
zu H9/H10 gehen.

## H6 – Direkt-Modus auf dem Startbildschirm (experimentell)

Nur wenn H1–H5 bestanden sind. `config.ini` auf `toolbox_target_mode=root`, Konsole neu starten,
Host laden. Im Log beim Start: `AKENO_DIRECT: shortcut target AKEN00001, root mode`.

**Ohne Spiel**, auf dem Startbildschirm: L2 + R3 halten → AKENO soll **direkt** erscheinen →
Freunde öffnen → ○ → ○ → Startbildschirm. Dann **eine Minute** nichts tun außer den Controller zu
bewegen (Sticks, Tasten), danach PS-Taste und Sony-Einstellungen öffnen/schließen.

Erwartete Log-Zeilen in dieser Reihenfolge (Zahlen ändern sich):

```text
AKENO_DIRECT: shortcut l2_r3 -> URI OnionHEN?Plugin=AKEN00001
AKENO_DIRECT: boot redirect -> Settings, AKEN00001 pending as its root
AKENO_DIRECT: Settings root requested … ms after the redirect
AKENO_DIRECT: AKEN00001 root page served as the Settings root (setting_list id_debug_settings, fresh page stack)
AKENO_DIRECT: root activated (transition …, … ms after it was served); input goes to AKEN00001
AKENO_DIRECT: page shown, depth 2
AKENO_DIRECT: page popped, depth 1
AKENO_DIRECT: session ended (root left, Settings closing) …        (oder erst beim nächsten Öffnen:
AKENO_DIRECT: session ended (the Settings root was opened again) …)
AKENO_DIRECT: ShellUI reads the pad: … reads, … button changes in the last interval   (alle 5 s)
AKENO_DIRECT: input watch done: ShellUI reads the pad (…)
```

**Bestanden, wenn** AKENO direkt erscheint, bedienbar ist, ○ zurückführt, der Controller danach
normal reagiert und keine Abbruch-Zeile im Log steht. **Fehlt `root activated`** oder erscheint
AKENO nicht: Abbruch und melden (dann lehnt die Einstellungs-App die Seite ab).

## H7 – Zurück-Navigation und Eingabe (Startbildschirm, 3×)

H6 dreimal wiederholen, dabei einmal **mit der PS-Taste** statt ○ aus AKENO herausgehen und danach
erneut L2 + R3. Zweimal schnell hintereinander L2 + R3 ist erlaubt (das zweite wird ignoriert:
`AKENO_DIRECT: shortcut l2_r3 ignored …`).

**Bestanden, wenn** jedes Mal AKENO öffnet, ○ zurückführt und der Controller danach reagiert.

## H8 – Direkt-Modus im laufenden Spiel

Nur wenn H6 und H7 bestanden sind. **Im Spiel speichern.** Dann: L2 + R3 halten → AKENO direkt →
Freunde → ○ → ○ → **dasselbe Spiel**. 1 Minute spielen (L2, rechter Stick, alle Tasten).

**Bestanden, wenn** das Spiel normal weiterläuft und reagiert. Nur dann noch **einmal**
wiederholen. Bei jeder Auffälligkeit: Abbruch.

## H9 – Plugin läuft nicht (Rückfall)

Toolbox → Plugins → AKENO NETWORK → „Läuft“ ausschalten. Dann L2 + R3 halten:

- Direkt-Modus: die **normale Toolbox** öffnet sich; Log:
  `AKENO_DIRECT: shortcut l2_r3 -> Toolbox (AKEN00001 has no page; plugin not running?)`.
- Link-Modus: die normale Toolbox öffnet sich, ohne AKENO-Eintrag oben.

Kein leerer Bildschirm, keine Sperre. Danach das Plugin wieder einschalten („Läuft“).

## H10 – Logs sammeln und melden

Per FTP auf den PC kopieren (auch nach einem Abbruch, dann **nach** dem Neustart):

- `/data/OnionHEN/OnionHEN.log` (und `OnionHEN.log.1`, falls vorhanden)
- `/data/OnionHEN/OnionHEN_util_daemon.log`
- falls per FTP sichtbar: `/system_tmp/onionhen/akeno_direct_session` und
  `/system_tmp/onionhen/akeno_direct_disabled` (gibt es nur nach einem Problem)

Melden: Ergebnis jedes Schritts H0–H9, Uhrzeit eines Problems, und die Zeilen mit `AKENO_DIRECT`,
`plugin-bridge`, `Toolbox online`, `Activating toolbox`, `rest:` aus dem Log. Die Logs enthalten
keine Passwörter, Tokens oder Nachrichteninhalte (AKENO_DIRECT schreibt nur Tiefe und Ergebnis
eines Tastendrucks, keine Texte).

## Danach

- Alles bis H5 bestanden: Link-Modus ist der stabile Schnellzugang; im Zweifel bei ihm bleiben.
- H6–H8 bestanden: Direkt-Modus funktioniert auf deiner Konsole – noch kein „stabil“, aber der
  nächste Schritt wäre ein längerer Alltagstest.
- Abbruch in H6–H8: zurück zum Link-Modus (oder Original-Host); die Log-Zeilen zeigen uns, an welcher
  Stelle es hängt.
