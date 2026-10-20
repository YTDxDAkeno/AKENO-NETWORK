# AKENO Audio Lab 3.1 — Hardwaretestplan (DEUTSCH)

**Firmware:** 12.20 · **Umgebung:** Jailbreak, ELF-Loader oder Launcher · **Dauer:** ca. 5 Minuten.

Dieser erste Lauf wird **vom Home-Bildschirm aus** gemacht, **ohne laufendes Spiel**, **ohne
Mikrofon**. Es gibt keinen automatischen Ton: Jeder Ton kommt nur auf deinen `tone`-Befehl.

## Vorbereitung

1. Prüfsummen kontrollieren (`sha256sum -c SHA256SUMS.txt`).
2. Eine Möglichkeit bereitlegen, `/data/akeno-audiolab/control` zu beschreiben und
   `/data/akeno-audiolab/` auszulesen (FTP oder Dateiansicht).
3. **Entscheiden, ob die Port-State-Messung mitlaufen soll** (der eigentliche neue Nutzen). Wenn ja:
   die Datei `/data/akeno-audiolab/enable-portstate` anlegen (leer genügt). Risiko und Nutzen:
   [SAFETY.md](SAFETY.md).

## Testablauf (minimal und kontrolliert)

| Schritt | Aktion | Erwartung |
| --- | --- | --- |
| 1 | **Vom Home-Bildschirm** `akeno-audio-lab.elf` laden | Benachrichtigung „AKENO Audio Lab 3.1 started …“; `status.json` = RUNNING |
| 2 | `control` anlegen mit einer Zeile `status` | `report.txt` erscheint; „Toene gespielt: 0“ |
| 3 | TV-/Headset-Lautstärke normal stellen | — |
| 4 | `control` um `tone` ergänzen (Standard: S16 Stereo, 1024, moderate) | nach ~2 s steht ein Lauf in `report.txt` |
| 5 | **Ehrlich notieren: hast du den 440-Hz-Ton gehört, und wo?** (TV, Headset …) | das ist die entscheidende Angabe |
| 6 | Optional `tone mono16-256 clear` für einen zweiten, etwas lauteren Versuch | wie oben |
| 7 | Mit `stop` kannst du einen Ton jederzeit abbrechen | Lauf als `cancelled` |
| 8 | `control` um `exit` ergänzen (oder 30 min ablaufen lassen) | Benachrichtigung „finished“; `status.json` = COMPLETED |
| 9 | `report.json`, `report.txt` holen und zurückschicken | fünf Ergebnisklassen, deine „gehört“-Angabe |

Beispielinhalt der Datei `control`:

```
status
tone
# jetzt im Bericht nachsehen und notieren, ob du den Ton gehoert hast
exit
```

## Was die Ergebnisse bedeuten (Ergebnismatrix)

| Ergebnis | Was es sagt | Nächster konkreter Schritt |
| --- | --- | --- |
| `port-open-failed` | der Hintergrund-Payload darf den Port gar nicht öffnen | Fehlercode melden; mit dem früheren `0x80260011`-Befund vergleichen |
| `sample-output-failed` | Port offen, aber Ausgabe abgelehnt | Fehlercode melden; Parameter sind bereits auf die geprüfte SDL-Konfiguration beschränkt |
| `accepted-but-not-paced` | Samples werden ohne Playback-Takt verschluckt | deutet auf Verwerfen statt Wiedergabe — anders als der bekannte v2-Befund |
| `accepted+paced-but-inaudible` | **der bekannte Fall**: angenommen, getaktet, stumm | **nur** mit Port-State (Opt-in) kommt hier neue Information; sonst nichts Neues |
| `AUDIBLE (owner-confirmed)` | du hast ihn gehört | widerspricht dem bisherigen Befund: Lauf behalten, Sitzung verlängern, dann Zwei-Wege prüfen |

### Wenn die Port-State-Messung aktiviert war

Der Bericht zeigt dann pro Ton die **rohen** Port-State-Bytes und — klar als **UNVERIFIZIERT /
PS4-Layout** markiert — eine tentative Deutung:

| Port-State (bei stummem, getaktetem Ton) | Mögliche Deutung (kein Beweis) | Nächster Schritt |
| --- | --- | --- |
| Aufruf schlägt fehl (Code) | aus dem Payload nicht nutzbar | nur auf das „gehört“-Ergebnis stützen |
| `output` ohne Geräte-Bit (0) | der Port ist offen, aber an **kein** Ausgabegerät gebunden | Indiz für eine Prozess-/Session-Routing-Grenze |
| `output` primary/headphone, `volume` > 0, trotzdem stumm | das System behauptet ein Gerät; Verlust liegt später (Mixer/Stummschaltung) | gegen das „gehört“-Ergebnis halten |
| Prüfbyte verletzt | die Struktur ist größer/anders als angenommen | Abfrage stoppt automatisch; rohe Bytes melden, nichts dekodieren |

**Wichtig:** Fehlende Geräte-Bits beweisen **nicht** allein, wo das Routing scheitert — die
Feldbedeutung ist auf 12.20 nicht verifiziert. Der Bericht behandelt die Werte als Indizien.

## Sichere Beendigung

Jederzeit `stop` (bricht den Ton ab) bzw. `exit` (beendet sauber), oder das Payload über den
Loader/Launcher beenden, oder im Notfall die Konsole neu starten. Es bleibt nur der Ordner
`/data/akeno-audiolab/` zurück, der gefahrlos gelöscht werden kann.

## Ausdrücklich **nicht** Teil dieses Tests

Kein Mikrofon, keine Fokus-/Attach-/Routing-/Lautstärke-Funktion, kein Netzwerk, keine Account-,
PSN-, DNS- oder Firmware-Änderung, kein Schließen eines Spiels, kein Eingriff in OnionHEN, AKENO
NETWORK oder die Party-Voice.
