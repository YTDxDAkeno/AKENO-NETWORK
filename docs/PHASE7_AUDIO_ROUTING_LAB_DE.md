# AKENO NETWORK Phase 7 — Audio Lab 3.1 und die Audio-Routing-Untersuchung

**Stand:** 20. Oktober 2026 · **Status:** implementiert, getestet, PS5-ELF gebaut. Kein Konsolentest
durch die KI; kein Merge/Release/Deployment. **Komponente:** `clients/ps5-audiolab/` ·
**Branch:** `research/phase7-audio-routing-lab` (abgezweigt von der Phase-6.1-Linie).

Belegstufen durchgängig: **[HW]** auf der PS5 des Eigentümers (FW 12.20) gemessen · **[QUELLE]** in
Quellcode/SDK belegt · **[SIM]** in der PC-Simulation reproduziert · **[HYP]** Hypothese ·
**[UNBEKANNT]**.

> **Aktualisierung Audio Lab 3.1:** Der frühere dateigesteuerte Ablauf und jede Port-State-Messung
> sind ersetzt. Das aktuelle ELF kündigt den Test an, wartet fünf Sekunden, gibt genau einen
> Standardton aus, schließt den Port, schreibt die drei Berichte und beendet sich. Es gibt keine
> Steuerdatei, keine FTP-Befehle und keine Wiederholung. `sceAudioOutGetPortState` wird weder
> aufgelöst noch aufgerufen. Die folgenden Abschnitte dokumentieren die vorherige Forschungsfrage;
> für die aktuelle Bedienung gilt `clients/ps5-audiolab/package/README-DE.md`.

---

## 1. Audit von Audio Lab v3 (Commit `05ad4f3`) — war es sicher genug zur Wiederverwendung?

**Ergebnis: der Code ist sicher und sauber; das ELF drumherum ist es nicht.**

- Der Tonpfad entspricht den Vorgaben [QUELLE]: 440 Hz, −20 dBFS, 250 ms + 1,5 s + 250 ms, S16
  Stereo, 1024 Frames, 48 kHz, System-Benutzer `0xFF`, Haupt-Port 0. Es spielt nichts automatisch.
- Lebenszyklus sauber: ein Port je Test, Öffnen/Schließen in derselben Funktion unter eigenem Lock,
  nur ein Test gleichzeitig, 3 s Abstand, max. 30 Läufe/Start, abbrechbar, feste Frame-Grenze (keine
  Endlosschleife) [QUELLE].
- `sceAudioOutGetPortState` nur auf dem eigenen Handle, 256-Byte-Fenster in 4-KB-Arena mit
  Prüfbytes, dauerhafter Stopp nach Überlauf, dekodierte Felder als PS4-Layout **unverifiziert**
  gekennzeichnet [QUELLE].
- **Das Problem:** Das v3-ELF ist kein eigenständiger Test, sondern eine Variante der kompletten
  Alpha-0.5-Konsolen-App. Diese nutzt `/data/akeno` und belegt `127.0.0.1:8642` für ihre
  Weboberfläche [QUELLE]. Damit ist es nicht von einer aktiven AKENO-Installation isoliert und wird
  **nicht** unverändert auf der PS5 des Eigentümers gestartet.

**Entscheidung:** den geprüften Code unverändert übernehmen, aber in ein **isoliertes** Payload
packen → Audio Lab 3.1.

## 2. Was an 3.1 neu gebaut wurde, was übernommen

- **Unverändert übernommen (byte-gleich):** `audio_lab_pcm.c/.h`, `audio_lab_state.c/.h` (die
  Tonerzeugung und die geschützte Port-State-Messung). Die Ausgabe-Aufrufsequenz und ihre Parameter
  sind aus `platform_ps5.c` wörtlich übernommen.
- **Neu, klein, isoliert:** ein datei-gesteuerter Kontroll-Loop (`main.c`), das Modell und die
  Kommandos (`al.c`), die Ausführung eines Tons (`al_engine.c`), die Berichte (`al_report.c`), die
  PS5-Naht (`al_backend_ps5.c`, die einzige Datei mit Konsolenaufrufen) und die Simulation
  (`al_backend_host.c`). Muster: der System Explorer.
- **Isolation:** nur `/data/akeno-audiolab/`, kein Netzwerk, keine Weboberfläche, kein Port 8642,
  kein Mikrofon, eine Instanz (flock), Ton nur auf Befehl.

## 3. Kann 3.1 etwas Neues zeigen? (die wichtigste Frage)

**Ehrliche Antwort: der Tonlauf allein — nein. Die Port-State-Messung — ja, als einziges.**

- Wir wissen bereits [HW]: Ein Hintergrund-Port **akzeptiert** Samples, **taktet** sie im
  Playback-Tempo, bleibt aber **stumm**; der Home-Bildschirm ändert das nicht; verschiedene Formate
  wurden schon getestet. Ein weiterer Tonlauf aus einem Hintergrund-Payload (und 3.1 ist, so geladen,
  genau das) **wiederholt** diesen Befund. Der Bericht sagt das offen (`accepted+paced-but-inaudible`).
- Der **einzige neue** Messwert ist die **Port-State-Abfrage des eigenen Ports** (Opt-in). Sie kann
  andeuten:
  - `output` **ohne** Geräte-Bit → der Port ist offen, aber an **kein** Ausgabegerät gebunden
    (Indiz: Routing/Session wird dem Hintergrundprozess nicht gewährt);
  - `output` **mit** Geräte-Bit und `volume > 0`, trotzdem stumm → der Verlust liegt **später**
    (Mixer-/Prozess-Stummschaltung).
  Beides unter dem Vorbehalt, dass das Feld-Layout auf 12.20 **nicht verifiziert** ist [UNBEKANNT];
  der Bericht führt rohe Bytes und tentative Deutung getrennt.

### Was 3.1 **nicht** beweisen kann

- Es kann **nicht** allein Mixer-Stummschaltung von Routing-Ausfall sicher trennen, solange die
  Feldbedeutung der Struktur auf 12.20 nicht bestätigt ist. Fehlende Geräte-Bits sind ein Indiz,
  kein Beweis.
- Es sagt nichts über das Mikrofon (bewusst nicht Teil dieses Experiments).

### Falls du die Port-State-Messung nicht fahren willst (alternative Richtung)

Dann liefert ein weiterer Konsolentest **keine** neue Information, und die begründete Alternative
ist, das Konsolen-Hintergrund-Audio vorerst **nicht** weiter zu verfolgen, sondern den bereits
belegten Weg zum Ziel zu nehmen: hörbare/aufnehmende Audio existiert auf der PS5 nur **im
Vordergrund-App-Prozess** oder **in einem Systemprozess (ShellUI)** [HW/QUELLE]. Für Party-Voice
**während eines Spiels** heißt das konkret: ein zweites Gerät (Handy/PC) in derselben
LiveKit-Party führt Mikrofon und Lautsprecher, während das PS5-Payload nur Signalisierung/Benach-
richtigungen übernimmt. Das umgeht die Hintergrund-Audio-Grenze, statt gegen sie zu testen.

## 3a. Code-Review-Korrekturen vor dem ersten Hardwaretest

Nach einer zusätzlichen Code-Prüfung wurden vor dem ersten Hardwaretest umgesetzt:

- **Sichere Session-Grenze (kritisch):** Beim Start legt das Lab eine vorhandene `control`-Datei
  beiseite (`control.consumed`) und liest nur noch danach angehängte Befehle. Ein `tone` aus einem
  früheren Lauf kann so nach einem Neustart **nie** erneut einen Ton auslösen. Portables Modul
  `al_control.c`, gegen echte Dateien unit-getestet (`test_control_session_boundary`).
- **Einzelinstanz-Sperre:** Schlägt schon das Öffnen der Sperrdatei fehl, startet das Lab **nicht**
  (vorher lief es ungeschützt weiter). `flock`-Fehler (belegt vs. nicht unterstützt) werden
  unterschieden.
- **Thread-Sicherheit:** Der Tonthread schreibt sein Ergebnis nur unter einer `al_state`-Sperre
  zurück (nie während des blockierenden Audioaufrufs); die Berichtschreiber lesen unter derselben
  Sperre. Stop-Flag und Worker-Flags sind `atomic_int`. Geprüft mit **ThreadSanitizer** und einem
  Nebenläufigkeitstest (`test_thread_safety`).
- **Opt-in eindeutig:** `enable-portstate` wird **bei jedem `tone`-Befehl** neu geprüft (nicht nur
  beim Start), sodass „Datei vor dem Ton anlegen“ eine wohldefinierte Aktivierung ist.

## 4. Entscheidungsbaum: nächster Schritt zu hörbarem Hintergrund-Audio

```
Audio Lab 3.1 vom Home-Bildschirm, ein `tone`-Lauf
│
├─ port-open-failed / sample-output-failed
│     → der Hintergrund-Payload darf den Port nicht öffnen/bespielen.
│       Fehlercode melden; mit 0x80260011 (Vordergrundbenutzer-Port) vergleichen.
│       Konsequenz: Konsolen-Hintergrund-Audio ist über öffentliche Schnittstellen zu.
│
├─ accepted+paced-but-inaudible  (der erwartete Fall)
│   │
│   ├─ Port-State NICHT aktiviert
│   │     → KEINE neue Information. Entweder Opt-in nachholen, oder die
│   │       alternative Richtung (Abschnitt 3) nehmen.
│   │
│   └─ Port-State aktiviert  (die eine neue Messung)
│         ├─ Aufruf schlägt fehl → aus dem Payload nicht nutzbar; nur „gehört“ zählt.
│         ├─ output OHNE Geräte-Bit → Indiz: Routing/Session fehlt dem Prozess.
│         │     Nächster Schritt: read-only erforschen, wie das System einem Prozess
│         │     Ausgabe zuteilt (KEINE Fokus-/Attach-Aufrufe). Realistisch: Ziel über
│         │     ein zweites Party-Gerät erreichen.
│         └─ output MIT Geräte-Bit, volume>0, stumm → Indiz: Verlust im Mixer.
│               Nächster Schritt: prüfen, ob eine dokumentierte, nicht
│               zustandsändernde Abfrage die Mixer-Beteiligung bestätigt; sonst
│               ebenfalls der Zwei-Geräte-Weg.
│
└─ AUDIBLE (owner-confirmed)   (unerwartet)
      → widerspricht dem bisherigen Befund. Lauf behalten, Sitzung verlängern,
        dann Richtung Zwei-Wege-Audio (Mikrofon ist ein separater, später
        freizugebender Test).
```

**Wichtigste anschließende Engineering-Aktion (eine):** Wenn die Port-State-Messung `output` **ohne
Geräte-Bit** zeigt, ist das das stärkste Indiz, dass die fehlende **Audio-Session des Prozesses**
(nicht Format, Pacing oder Mixer) der Kern ist — und damit, dass hörbares Party-Audio während eines
Spiels realistisch **nur** über ein zweites Gerät in der Party zu erreichen ist, nicht durch mehr
Parameter-Varianten im Hintergrund-Payload. Diese eine Messung kann die Richtung der ganzen
Phase 8 festlegen.

## 5. Risiken und Kompatibilität

| Bereich | Maßnahme |
| --- | --- |
| Zweite Audio-Sitzung | Dateisperre (flock); zweiter Start wird abgewiesen (unit-getestet) |
| Automatische Wiedergabe | ausgeschlossen: Ton nur auf `tone`-Befehl |
| Port-State-Risiko | Opt-in, eigenes Handle, 256-Byte-Fenster (8× Doku), Prüfbytes + dauerhafter Stopp, nicht per Standard |
| Mikrofon | nirgends referenziert (CI-geprüft) |
| Netzwerk/VPS/LiveKit/8642/`/data/akeno` | nicht vorhanden/berührt (CI-geprüft, kein Netzcode) |
| Audio als statischer Import | ausgeschlossen: dlopen-only (CI-geprüft) |
| OnionHEN, AKENO NETWORK, Party-Voice, Spiele | unverändert |

## 6. Tests und Build

- Host-Unit-Tests (62 Checks) unter GCC **ASan/UBSan** und clang-18, 0 Fehler: Kommandoparser,
  Ton-Lebenszyklus/Ergebnisse, Port-State-Opt-in-Gate und Überlauf-Stopp, Pacing, Abbruch,
  Per-Start-Limit, PCM, JSON-Gültigkeit, Berichtsinhalt, Guard-Primitive.
- End-to-End-Smoke (Host-`main`): `status`/`tone`/`stop`/`exit`, saubere Beendigung, und die
  Einzelinstanz-Abweisung (zweiter Start rc=1).
- PS5-Cross-Build mit dem gepinnten SDK, Import-Gate grün (nur `sceKernelSendNotificationRequest`;
  Audio dlopen-only; nur `libSceAudioOut.sprx`), **byte-identisch reproduzierbar**.

Die SHA-256 der ausgelieferten ELF steht im Liefertext dieser Phase (sie bindet den Commit-Hash als
Build-ID ein).
