# AKENO Voice Alpha 0.2.0 – erwartete Log-Zeilen (`/data/akeno-party-voice/voice.log`)

Jede Zeile: Sekunden seit dem Start, Unix-Zeit, Ereignis, Text. Nur Zustände, Codes und Zähler –
keine Namen, keine Party-/Konto-IDs, kein Sprachpass, kein Ton. `…` steht für Zahlen, die bei dir
anders sind.

## Start

```
service.process: this process: background process (own app 0x00000000 (no title ID), foreground app 0x…)
service.start: akeno-party-voice 0.2.0-alpha (build …-ps5) on PS5; control on 127.0.0.1:8646; nothing is opened until you join voice
```

`background process` bestätigt, dass der Dienst ein Hintergrundprozess ist (das ist der
entscheidende Unterschied zu AKENO Native). Möglich ist auch `unknown (own app error 0x…, …)`, falls
die PS5 einem Payload keine App-Info gibt. Steht dort `foreground application`, bitte melden.

## Test B – Testton ohne Party

```
voice.test_sound: test sound requested on route 1 outside voice: 1 beep through the voice pipeline on a port of its own
voice.output: route 1: output port open (sceAudioOut, user 0x000000FF, port type 0, 48000 Hz, S16 stereo, 1024 frames)
voice.output: clock probe: 12 silent blocks, calls 2..12 took … ms for 234.7 ms of audio -> an audio clock consumes the port
voice.test_sound: route 1 (on its own): … Opus packets encoded, … decoded; … accepted output blocks carried the tone, … measured at 880 Hz +-5 % in the submitted buffer (…..… Hz), peak 10%; timing paced. Audible only if you heard 1 beep
voice.output: test sound output port closed: … calls, … of … ms inside the calls, 0 sleeps ahead of real time -> paced at playback speed by the port
```

Mögliche andere Uhr-Ergebnisse: `-> the calls return at once (the port does not consume in real time)`
(der Ausgang verwirft die Blöcke) oder `-> undecided`. Beides ist ein Messergebnis, kein Fehler.

## Test C – in der Party

```
voice.join: joining party voice (microphone off until the user turns it on)
voice.output: route 1: output port open (sceAudioOut, user 0x000000FF, port type 0, 48000 Hz, S16 stereo, 1024 frames)
voice.output: clock probe: 12 silent blocks, … -> an audio clock consumes the port
voice.stats: sent=0 sent_nonzero=0 received=… decoded=… … paced=1 …
```

`paced=1` heißt: Der Ausgang hat die Blöcke im Echtzeittakt angenommen (`0` = sofort zurück,
`-1` = unentschieden). **Hörbar heißt es nicht.**

## Test D – Mikrofon

Erwartet (Hintergrundprozess):

```
voice.mic: the user turned the microphone on (consent for this voice session)
voice.mic: the microphone could not be opened: 0x809B0081 - refused to this process: … (sceAudioIn via dlopen, user 0x…, type 1, 256 frames, 16000 Hz, S16 mono); tried: 16000 Hz -> 0x809B0081
```

Nur **ein** Aufruf (16000 Hz) – 48000 Hz wird nicht mehr probiert, weil die Ablehnung nichts mit
der Abtastrate zu tun hat. Zweiter Druck auf „Try the microphone again" in derselben Situation:

```
voice.mic: turn on: not tried again: refused before in this situation (same user, same running app) (0x809B0081, process)
```

Würde das Mikrofon doch öffnen: `voice.mic: microphone open (sceAudioIn via dlopen, …, 16000 Hz, S16 mono)` –
dann bitte sofort melden.

## Route 2 (nur falls gewählt)

```
voice.output: route 2: no output port: 0x80260011, no port for this user (PS4 SDK name SYSTEM_RESOURCE); …
```

In derselben Situation danach: `… not tried again: route 2 was refused (0x80260011) in this situation …`.
Routen 3/4 werden nicht mehr geöffnet (`Route 3 is retired …` auf der Seite).

## Im Spiel

Beim Spielstart erscheint (Spiel-ID statt `0x…`):

```
service.process: this process: background process (own app 0x00000000 (no title ID), foreground app 0x…)
```

Danach darf das Mikrofon **einmal** neu versucht werden.

## Ende

```
voice.leave: …
voice.output: voice output port closed: … calls, … -> paced at playback speed by the port
voice.mic: microphone closed
```

## Bericht `report.txt` – wichtigste Zeilen

```
NETWORK CONNECTED:             yes - …
REMOTE AUDIO RECEIVED:         yes - … packets received, … decoded, … frames with sound
AUDIO SUBMITTED:               yes - accepted output blocks with sound: … of party speech, … of the test sound
                               the port: consumed by an audio clock (…)
AUDIO AUDIBLE:                 NOT CONFIRMED - only you can answer it (below). A successful call is not sound.
MICROPHONE OPEN:               no - 0x809B0081: refused to this process: …
MICROPHONE CAPTURING:          no - 0 frames captured, 0 not exact zeros
MICROPHONE TRANSMITTING:       no - … packets sent, 0 of them with sound
REMOTE USER HEARD MICROPHONE:  NOT CONFIRMED - only your friend can answer it …
```

`AUDIO AUDIBLE` und `REMOTE USER HEARD MICROPHONE` stehen **immer** auf „NOT CONFIRMED": Nur du
und dein Freund können das bestätigen (Abschnitt „[Your answers]" am Ende).
