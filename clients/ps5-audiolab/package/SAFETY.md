# AKENO Audio Lab 3.1 — Sicherheit und Rückbau

Das Payload ist ein isolierter Einmaltest. Es verwendet ausschließlich
`/data/akeno-audiolab/` für Berichte und eine kurzlebige Dateisperre.

- Ein zweiter gleichzeitiger Start wird abgewiesen.
- Vor der Audioausgabe werden die Berichte vorbereitet; schlägt das fehl, startet kein Ton.
- Der verwendete Audioport gehört nur diesem Payload und wird nach dem einen Lauf geschlossen,
  auch wenn die Ausgabe fehlschlägt.
- `sceAudioOutGetPortState` bleibt vollständig deaktiviert: Es wird weder aufgelöst noch aufgerufen.
- Es gibt kein Mikrofon, Netzwerk, PSN, Routing-, Fokus-, Lautstärke- oder HDMI-Steuerung.
- Das Payload startet oder beendet keine Spiele und verändert weder OnionHEN noch AKENO NETWORK.

Bei einem Absturz endet nur dieses Payload; die Sperre wird vom System freigegeben. Als Rückbau
kann der Berichtsordner `/data/akeno-audiolab/` gelöscht werden. Es gibt keine Installation und
keine dauerhafte Konsolenänderung.
