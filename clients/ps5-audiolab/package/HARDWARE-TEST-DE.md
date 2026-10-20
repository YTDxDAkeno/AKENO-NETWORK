# AKENO Audio Lab 3.1 — Hardwaretest (DEUTSCH)

**Firmware:** 12.20 · **Dauer:** etwa 7 Sekunden · **Vorbereitung:** keine FTP-Steuerung nötig.

1. Prüfsummen bei Bedarf mit `sha256sum -c SHA256SUMS.txt` prüfen.
2. Vom Home-Bildschirm `akeno-audio-lab.elf` mit dem Payload-Loader starten.
3. Die Ankündigung abwarten. Nach fünf Sekunden folgt genau ein kurzer 440-Hz-Ton.
4. Die Abschlussbenachrichtigung abwarten und danach die drei Berichte aus
   `/data/akeno-audiolab/` lesen.

Erwartete Erfolgsmeldung: `Testton abgeschlossen (0x00000000)`. Bei einem Fehler die angezeigte
Hex-Zahl zusammen mit `report.json` und `report.txt` festhalten. Das Payload beendet sich danach;
es gibt keinen zweiten Ton in derselben Ausführung.

Der Test verwendet kein Mikrofon, kein Netzwerk, kein PSN, keine Steuerdatei und keine
Port-State-Abfrage. Es gibt keine Eingriffe in Spiele, OnionHEN oder AKENO NETWORK.
