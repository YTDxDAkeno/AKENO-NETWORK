# AKENO Voice Alpha 0.2.0 – Hardwaretest (FW 12.20)

Ziel in dieser Reihenfolge: 1. ein hörbarer Testton · 2. den Freund hören · 3. Mikrofon ·
4. echtes Gespräch · 5. Gespräch während eines Spiels. Jeder Schritt ist klein und begrenzt.
**Nur deine Antworten zählen als „hörbar".** Notiere nach jedem Schritt, was du gehört hast.

Vorher: im Spiel speichern. Kopfhörer am DualSense oder TV – notiere, was du benutzt.
Ein Freund am PC oder Handy in derselben AKENO-Party (für A3, C, D).

## Test A – Vordergrund-Referenz (AKENO Native 1.1, ohne Spiel)

Damit prüfen wir, dass Konsole, Konto und Server heute Ton und Mikrofon können – in der App, die
das nachweislich darf. **Vorher im Sprachdienst „Leave voice chat"** drücken (sonst ersetzt die eine
Verbindung die andere: pro Konto nur eine Konsolen-Sprachverbindung).

Starte **dein installiertes AKENO Native 1.1.0-beta.1** wie gewohnt über den Homebrew-Launcher
(nichts Neues installieren; der Launcher beendet dafür ein laufendes Spiel – deshalb ohne Spiel).

| Schritt | Was tun | Notieren |
| --- | --- | --- |
| A1 | Settings → **Audio test** → Testton abspielen | gehört? (ja/nein, wo) |
| A2 | Settings → **Microphone test** → bestätigen → sprechen | bewegt sich der Pegel? welche Hz (16000/48000)? |
| A3 | Party → **Join voice** → bestätigen; der Freund spricht, dann du | hörst du ihn? hört er dich? |
| A4 | Leave voice, AKENO Native beenden | – |

Wenn A3 klappt: **echte Zwei-Wege-Sprache auf der PS5 funktioniert** (im Vordergrund).

## Ab hier: der Sprachdienst 0.2.0 (Hintergrund), zuerst OHNE Spiel

Starte wie in [INSTALL-DE.md](INSTALL-DE.md) (Daemon → Host → `akeno-party-voice.elf` 0.2.0).

### Test B – Testton ohne Party (Startbildschirm)

| Schritt | Was tun | Notieren |
| --- | --- | --- |
| B1 | L2 + R3 → AKENO NETWORK → Voice chat → Speaker route → **Route 1** | – |
| B2 | **Play the test sound** (1 Piepton, leise, ca. 0,5 s) | **gehört? ja/nein** |
| B3 | Diagnostics & help öffnen (oder den Bericht später lesen) | – |

Im Bericht (`report.txt`, Abschnitt **[Test sound]**) steht dann z. B.
`Result: SUBMITTED - … measured at 880 Hz …` und unter **[Speakers]** die Zeile
`Clock probe: … -> consumed by an audio clock` (oder `NOT consumed in real time`).
Das heißt: Der Ton war wirklich im Puffer und wurde angenommen. **Ob er hörbar war, sagst nur du.**

### Test C – den Freund hören (Startbildschirm)

| Schritt | Was tun | Notieren |
| --- | --- | --- |
| C1 | Voice chat → **Join voice chat** (bestätigen, Mikrofon bleibt aus) | sieht der Freund dich in der Sprachparty? |
| C2 | Der Freund spricht 30 Sekunden | **hörst du ihn? ja/nein** |
| C3 | Speaker route → **Play the test sound** (wird in die Sprache gemischt) | gehört? |

### Test D – Mikrofon, genau einmal (Startbildschirm)

| Schritt | Was tun | Notieren |
| --- | --- | --- |
| D1 | Voice chat → **Turn on microphone** → bestätigen; sprechen | Benachrichtigung? Hört dich der Freund? |
| D2 | Falls abgelehnt: **„Try the microphone again"** einmal drücken | es muss *„… It is tried again when a game starts or ends."* kommen (kein neuer Versuch) |

Die Erwartung nach allem, was wir wissen: D1 meldet `0x809B0081` (abgelehnt für Hintergrundprozesse).
Jedes andere Ergebnis ist eine wichtige Neuigkeit – bitte genau notieren.

### Test E – Startbildschirm-Zusammenfassung

Noch in der Sprachparty: L2 + R3 → **Diagnostics & help** öffnen und schließen. Dann
**Leave voice chat**.

## Test F – mit laufendem Spiel (erst nach B–E)

1. Spiel starten (beliebig, mit Ton), kurz spielen.
2. L2 + R3 → Voice chat → **Join voice chat**; der Freund spricht → **hörst du ihn?**
3. Speaker route → **Play the test sound** → **gehört?**
4. **Turn on microphone** einmal (ein neuer Versuch ist hier erlaubt, weil jetzt ein Spiel läuft)
   → Ergebnis notieren.
5. Zurück ins Spiel: **lässt es sich normal steuern? Ist der Spielton noch da?**

## Test G – AKENO mehrmals öffnen (Sprache aktiv)

Im Spiel, während du in der Sprachparty bist: **L2 + R3** öffnen und mit **○** schließen,
**fünfmal**. Notiere: Hängt der Controller? Friert etwas ein? Bleibt das Spiel bedienbar?

Danach **Leave voice chat**.

## Was du zurückschickst

Per FTP aus `/data/akeno-party-voice/`:

- `report.txt` (aktueller Lauf), `report-previous.txt` (vorheriger Lauf), `voice.log`
- außerdem bitte die **Logs deines ersten Tests** (0.1.0): `voice(1).log`, `report(1).txt`,
  `OnionHEN(5).log`, `OnionHEN_util_daemon(5).log` – die liegen mir noch nicht vor

Aus `/data/akeno-native/` (Test A): `native-report.txt`, `native.log`.

Und deine Antworten (am Ende von `report.txt` zum Ausfüllen, oder einfach so):

```
A1 Native-Testton gehört:            ____
A2 Native-Mikrofonpegel bewegt sich: ____ (Hz: ____)
A3 Native: ich höre Freund / Freund hört mich: ____ / ____
B2 Dienst-Testton (Startbildschirm): ____
C2 Freund im Dienst gehört:          ____
D1 Mikrofon-Ergebnis (Code):         ____
F2/F3 im Spiel gehört (Freund / Ton): ____ / ____
F4 Mikrofon im Spiel (Code):         ____
F5 Spiel bedienbar, Spielton da:     ____
G  5x L2+R3 ohne Probleme:           ____
Kopfhörer/TV:                        ____
```

## Was die Ergebnisse bedeuten

| Ergebnis | Bedeutung |
| --- | --- |
| A ok, B/C **nicht** gehört, Uhr-Test „consumed", Ton „measured at 880 Hz", D `0x809B0081` | Bestätigt: Die PS5 gibt einem Hintergrundprozess keinen hörbaren Ton und kein Mikrofon. Sprache auf der PS5 dann über AKENO Native (ohne Spiel); im Spiel über Handy/PC in derselben Party |
| B oder C **gehört** | Sehr wichtig – widerspricht allen bisherigen Messungen. Dann bauen wir darauf auf |
| D öffnet und dein Freund hört dich | Ebenfalls neu: dann wird „nur sprechen" im Spiel möglich |
| A klappt nicht | Zuerst Konsole/Konto/Server klären; die Hintergrund-Ergebnisse warten |

Bei Problemen sofort zurück: [ROLLBACK-DE.md](ROLLBACK-DE.md).
