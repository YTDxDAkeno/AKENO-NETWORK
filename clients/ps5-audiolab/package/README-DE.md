# AKENO Audio Lab 3.1 — Automatischer Einmaltest

Dieses eigenständige PS5-Payload führt ohne FTP, Steuerdatei oder Netzwerk genau einen Audio-Test
aus. Es ändert weder AKENO NETWORK noch OnionHEN, Spiele oder Einstellungen der Konsole.

## Ablauf

1. `akeno-audio-lab.elf` mit dem PS5-Payload-Loader oder über den Homebrew-Launcher starten.
2. Eine Benachrichtigung kündigt den Test an.
3. Nach **5 Sekunden** gibt das Payload genau einen 440-Hz-Ton aus: S16 Stereo, 48 kHz,
   1024 Frames, −20 dBFS, 250 ms Stille + 1,5 s Ton + 250 ms Stille.
4. Der eigene Audioport wird geschlossen.
5. `report.json`, `report.txt` und `status.json` werden unter `/data/akeno-audiolab/` gespeichert.
6. Eine Abschlussbenachrichtigung zeigt `0x00000000` oder einen Fehlercode.
7. Das Payload beendet sich selbst.

Es gibt pro Ausführung keinen zweiten Ton und keine Eingabemöglichkeit zum Wiederholen.

## Paketinhalt und Prüfsumme

```
AKENO-Audio-Lab-3.1/
  akeno-audio-lab.elf
  AKENOAudioLab/
  README-DE.md
  HARDWARE-TEST-DE.md
  SAFETY.md
  SHA256SUMS.txt
```

Vor dem Start optional prüfen:

```sh
cd AKENO-Audio-Lab-3.1
sha256sum -c SHA256SUMS.txt
```

## Ergebnisse

| Datei | Inhalt |
| --- | --- |
| `report.json` | Maschinenlesbare Rückgabecodes und Zähler des einen Laufs. |
| `report.txt` | Deutsche Zusammenfassung einschließlich Open/Output/Close-Ergebnis. |
| `status.json` | Zuerst `WAITING`, nach dem Lauf `COMPLETED`. |

Ein erfolgreiches API-Ergebnis beweist keine Hörbarkeit. Das Payload misst oder speichert kein
Mikrofon und sendet keine Daten.
