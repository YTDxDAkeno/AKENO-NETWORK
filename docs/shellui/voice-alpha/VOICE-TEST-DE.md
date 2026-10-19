# AKENO Party-Voice Alpha – der erste echte Sprachtest (ca. 30 Minuten)

**Du brauchst:** deine PS5 (FW 12.20) mit Phase-4.1-Host, Daemon, Plugin 1.20 und dem Sprachdienst
([INSTALL-DE.md](INSTALL-DE.md)); **einen Freund** am PC oder Handy in derselben AKENO-Party
(Website, Sprache im Browser); **ein Headset am DualSense** (gegen Echo; notiere, was du benutzt:
DualSense-Mikrofon, Headset am Controller, USB-Headset).

**Voraussetzung:** Der AKENO-Server hat das Sprach-Update ([SERVER-PREREQUISITES-DE.md](SERVER-PREREQUISITES-DE.md)).
Ohne es endet Test A mit *„The AKENO server has no console voice yet (voice pass: HTTP 404)"* – das
ist dann das Ergebnis, kein PS5-Fehler.

Brich beim ersten Problem ab (Konsole hängt, Controller reagiert nicht, Spiel ruckelt stark):
[ROLLBACK-DE.md](ROLLBACK-DE.md). Schreib bei jedem Schritt kurz auf, was passiert ist.

## A – Party-Verbindung (Startbildschirm, kein Spiel)

1. Du und dein Freund seid in **derselben AKENO-Party** (z. B. über die Website oder AKENO → Party).
2. Dein Freund tritt im Browser der **Sprache** der Party bei (Mikrofon erlauben).
3. PS5: L2 + R3 → AKENO NETWORK → **Voice chat** → **Join voice chat** → Frage mit ✕ bestätigen.
4. Erwartet: Benachrichtigung *„AKENO Voice: in the party's voice chat"*. Seite neu öffnen:
   *„Status: Connected · Listening · Microphone off"*, darunter die Teilnehmer.
5. **Frag deinen Freund:** Sieht er dich in der Sprach-Party (mit Stumm-Symbol)?

→ Notiere: **„Ich bin der Party beigetreten" ja/nein**, was die Seite zeigt.

## B – Ton vom Freund (eingehend)

1. Dein Freund spricht durchgehend (zählt z. B. langsam bis 30).
2. Hörst du ihn über **TV** oder **Headset**?
3. Wenn nein: **Speaker route** öffnen → **Play the test sound** (Route 1 = 1 Piepton). Dann
   **Route 2**, Testton (2 Pieptöne), **Route 3** (3), **Route 4** (4). Notiere, welche Pieptöne du
   hörst. Wähle die hörbare Route und lass deinen Freund noch einmal sprechen.
4. Optional: **Voice volume · louder** (bis 150 %).

→ Notiere: **„Ich höre meinen Freund" ja/nein**, über welche Route, wo (TV/Headset).

## C – Dein Ton (ausgehend)

1. Sprachseite → **Turn on microphone** → Frage bestätigen. Benachrichtigung *„AKENO Voice:
   microphone on"* oder *„microphone unavailable (0x…)"* (dann den Code notieren).
2. Sprich deutlich ins Headset/DualSense.
3. **Frag deinen Freund:** Hört er dich? Leuchtet bei ihm dein Sprech-Anzeiger?
4. **Mute microphone** → hört er dich nicht mehr? **Unmute microphone** → wieder?

→ Notiere: **„Mein Freund hört mich" ja/nein**, Mute wirkt ja/nein.

## D – Während eines Spiels

1. Im Spiel speichern. Spiel starten oder fortsetzen (z. B. *Atelier Yumia*). **Die Sprach-Party
   bleibt verbunden** – du musst dafür nichts tun.
2. Spiele ein, zwei Minuten. Dein Freund spricht zwischendurch, du auch.
3. Wiederhole kurz **B** (hörst du ihn im Spiel?) und **C** (hört er dich?).
4. Läuft der Spielton weiter? Ruckelt etwas?

→ Notiere getrennt für das Spiel: hören ja/nein, gehört werden ja/nein, Spielton ja/nein.

## E – AKENO im Spiel öffnen und schließen

1. Im Spiel **L2 + R3** → AKENO → Voice chat → **Mute** → ○ zurück ins Spiel.
2. Noch einmal L2 + R3 → **Unmute** → ○ zurück. Zwei-, dreimal wiederholen.
3. **Deafen** → hörst du nichts mehr? **Undeafen** → wieder?
4. Zum Schluss **Leave voice chat** (Frage bestätigen) → ○.
5. Reagiert der Controller im Spiel weiter normal? Läuft das Spiel?

→ Notiere: **„Alles funktioniert nach der Rückkehr ins Spiel" ja/nein**.

## F – Diagnose holen

Per FTP auf den PC kopieren und mit deinen Notizen schicken
([DIAGNOSTICS-DE.md](DIAGNOSTICS-DE.md)):

- `/data/akeno-party-voice/report.txt` (und `report-previous.txt`, falls vorhanden)
- `/data/akeno-party-voice/voice.log`
- `/data/OnionHEN/OnionHEN.log`

Im Bericht ganz unten die Antworten ausfüllen (`____`).

## Was die Antworten bedeuten

| Ergebnis | Bedeutung |
| --- | --- |
| A ja, B ja, C ja, auch in D | Zwei-Wege-Sprache im Spiel funktioniert auf deiner PS5 – erst das gilt als „funktioniert" |
| C ja, B nein (auf allen Routen) | nur **Sprechen** im Spiel möglich; zuhören z. B. über Handy |
| B ja, C nein (`Microphone unavailable` oder Freund hört nichts) | nur **Zuhören** |
| A ja, B nein, C nein | Netzwerk geht, Ton nicht: die PS5 blockiert Hintergrund-Audio; der Bericht zeigt, an welcher Stelle |
| A nein mit HTTP 404 | Server-Update fehlt noch |

Der Bericht unterscheidet: **NETWORK CONNECTED** (Party beigetreten), **MICROPHONE TRANSMITTING**
(Ton mit Inhalt wurde aufgenommen, kodiert und gesendet – ob er ankommt, sagt nur dein Freund) und
**SPEAKER OUTPUT ACTIVE** (Ton wurde dekodiert und an den Audioausgang gegeben – ob er hörbar war,
sagst nur du). Ein „yes" dort ist **kein** Beweis, dass man etwas hört.
