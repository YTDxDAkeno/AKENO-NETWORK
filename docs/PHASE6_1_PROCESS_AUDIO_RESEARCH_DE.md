# AKENO NETWORK Phase 6.1 — Prozess-, Anwendungs- und Audio-Diagnose

**Stand:** 20. Oktober 2026 (Europe/Berlin)
**Status:** implementierte, nur lesende Erweiterung des Explorers 6.0, mit PC-Simulation und
Unit-Tests; eine echte PS5-ELF wurde gebaut. Kein Konsolenversuch ist durch dieses Dokument
freigegeben; die KI hat **keinen** Hardwaretest durchgeführt.
**Komponente:** `clients/ps5-system-explorer/` · **Branch:** `research/phase6.1-process-audio-diagnostics`
(abgezweigt von der Phase-6.0-Linie, Commit `ca8cfe1`).

Dieses Dokument baut auf [PHASE6_AKENO_SYSTEM_EXPLORER_DE.md](PHASE6_AKENO_SYSTEM_EXPLORER_DE.md)
(6.0) auf und beschreibt nur, was 6.1 **zusätzlich** misst und erklärt. Es trennt durchgängig:

- **[QUELLE]** — in Quellcode oder dem gepinnten SDK belegt (zitiert);
- **[HW]** — auf der PS5 des Eigentümers (FW 12.20) gemessen, vom Eigentümer berichtet;
- **[SIM]** — in der PC-Simulation reproduziert (nie ein Beleg für Konsolenverhalten);
- **[HYP]** — plausibel, nicht bewiesen;
- **[UNBEKANNT]** — mit den verfügbaren Mitteln nicht feststellbar.

> **Oberziel (unverändert):** unabhängige AKENO-NETWORK-Party-Voice während eines laufenden
> PS5-Spiels, ohne PSN. 6.1 löst das **nicht** und behauptet es nicht. 6.1 schärft die Messungen,
> macht die bisher unerklärte fehlgeschlagene Prozessabfrage diagnostizierbar, ermittelt — wo
> technisch möglich — die Title-ID und stellt einen konkreten, nach Nutzen gewichteten
> Forschungsplan für das noch ungelöste Hintergrund-Audio auf.

---

## A. Phase-6.0-Hardwarebefunde (vom Eigentümer berichtet)

Zwei Läufe des Explorers 6.0 (Build `ca8cfe1749a5-ps5`) auf FW 12.20:

| Größe | Test 1 | Test 2 (Spiel vom Eigentümer: Monster Hunter Stories 3) |
| --- | --- | --- |
| Explorer-PID | 147 | 201 |
| Laufzeit | 300 s | 300 s |
| Aufgezeichnete Ereignisse | 22 | 23 |
| Kontextänderungen | 2 | 1 |
| Scheduling-Lücken | 0 | 0 |
| Erkannte Runtime-App-ID | `0x4018` | `0x8018` |
| Verlauf | Big-App vor Testende wieder „nicht aktiv“ | App ~32 s nach Start erkannt, blieb bis Testende aktiv |
| `process_record` | `NOT_AVAILABLE` | `NOT_AVAILABLE` |
| Explorer-Kontext | Hintergrundprozess, eigene App-ID 0 | Hintergrundprozess, eigene App-ID 0 |

**Wichtig und bewusst nicht behauptet:**

- `0x4018`/`0x8018` sind **Laufzeit-App-IDs**, keine dauerhaften PS5-Title-IDs. Die Verbindung von
  `0x8018` zu Monster Hunter Stories 3 beruht auf der Testbeschreibung des Eigentümers; die
  dauerhafte Title-ID wurde **nicht** unabhängig ermittelt [HW, HYP].
- Genaue Fehlercodes und exakte Ereignis-Zeitstempel beider Läufe liegen hier **nicht** vor (die
  Originaldateien `report.json`, `events.jsonl`, `system-summary.txt` usw. sind nicht im Workspace).
  Sie werden **nicht** erfunden. Für belastbare Aussagen dazu werden die Originaldateien benötigt.
- Dass der Explorer während der beobachteten App-Sitzung weiterlief, belegt **nicht**, dass
  Hintergrund-Audio oder Mikrofonzugriff verfügbar sind.

Die in 6.0 ererbten Audio-Befunde (Vordergrund hörbar; Hintergrundausgabe akzeptiert, aber stumm;
Mikrofon `0x809B0081`; Vordergrundbenutzer-Port `0x80260011`) bleiben unverändert und werden in
Abschnitt D referenziert, **nicht** als neue 6.1-Messung.

---

## B. In 6.1 umgesetzte Verbesserungen

| Stufe | Verbesserung | Dateien |
| --- | --- | --- |
| A | **Title-ID der laufenden App** über `sceSystemServiceGetAppTitleId` (schwach, nur lesend), nur für die vom System gemeldete Big-App, mit Budget und geschütztem Puffer; alle Ergebnis-/Fehlerfälle klassifiziert | `ex_backend_ps5.c`, `ex.c`, `ex_process.c`, `ex_timeline.c` |
| B | **Diagnostizierbare Prozessabfrage**: zweistufiges `sysctl(KERN_PROC_PID)` mit Rückgabewert, `errno`, angefragter/gelieferter Größe und SDK-Strukturgröße; Dekodierung nur bei passendem Layout | `ex_backend_ps5.c`, `ex.c`, `ex_process.c` |
| C | **Genauere Timeline**: Vordergrund-Sitzungen mit Title-ID, Abfragelatenz, Polling-Intervalle, „App weg → noch erreichbar?“-Nachprüfung, Peak-RSS je Sitzung | `ex_timeline.c`, `ex.c`, `ex_report.c` |
| G | **`status.json`-Lebenszeichen und zuverlässiger Wiederanlauf**: `flock` + Heartbeat, alte `control`-Datei wird beiseitegelegt, alte Berichte nach `previous/`; erneuter Start setzt einen Marker statt einer zweiten Sitzung | `main.c`, `ex_session.c`, `ex_report.c` |
| D/E/F | **Quellanalyse**: Audio-Fokus-/ShellUI-/Audio-Sitzungsfunktionen weiter untersucht; Status der früheren Audio-Builds geprüft; Entscheidungsbaum (Abschnitt H) | `ex_audio.c`, `ex_shellui.c`, dieses Dokument |

Die fünf Berichtdateien bleiben erhalten (`report.json`, `modules.json`, `audio-diagnostics.json`,
`events.jsonl`, `system-summary.txt`); Felder kamen additiv hinzu. `status.json` ist neu und nicht
Teil der fünf.

---

## C. Prozess- und Anwendungsarchitektur

### C.1 Die fehlgeschlagene Prozessabfrage (`process_record = NOT_AVAILABLE`) [QUELLE] [SIM]

6.0 verlangte implizit eine feste `kinfo_proc`-Größe und meldete bei Abweichung nur
`NOT_AVAILABLE`. 6.1 führt die Abfrage in zwei Schritten aus und protokolliert jeden:

1. **Größenabfrage** `sysctl(mib, NULL, &size)` → Rückgabewert und `errno`.
2. **Leseschritt** in einen Puffer von genau `sizeof(struct kinfo_proc)` → Rückgabewert, `errno`,
   angefragte und gelieferte Bytezahl, `ki_structsize` (das erste Feld der Antwort).

Der Datensatz wird **nur dann** dekodiert, wenn das vom Kernel gemeldete `ki_structsize` der
SDK-Größe entspricht **und** `ki_pid` die eigene PID ist. Andernfalls nennt 6.1 die genaue Ursache
(`SIZE_QUERY_DENIED`, `SIZE_QUERY_UNSUPPORTED`, `RECORD_TOO_LARGE`, `READ_FAILED` mit `errno`,
`EMPTY`, `LAYOUT_MISMATCH`, `PID_MISMATCH`) und rät **keine** firmware-spezifische Struktur
(`ex_pq_classify`, Unit-Tests `test_pq_classify`).

Das SDK (`include/freebsd/machine/proc.h`) setzt `KINFO_PROC_SIZE 1088`. Das ps5-payload-dev-SDK
und onionHEN iterieren die Prozessliste über das vom Kernel gelieferte `ki_structsize` statt über
`sizeof` [QUELLE: `samples/ps/main.c`, onionHEN `util_platform.c`] — ein Hinweis, dass die
Kernelgröße abweichen kann. 6.1 **misst** diese Differenz jetzt, statt sie zu vermuten. Welcher der
oben genannten Fälle auf 12.20 zutrifft, zeigt erst der nächste Hardwarelauf; die Originaldateien
der 6.0-Läufe würden ihn ebenfalls belegen.

> Peak-RSS-Unterschiede zwischen zwei Läufen gelten **nicht** als Speicherleck. 6.1 misst den
> Peak-RSS deshalb **innerhalb** einer Sitzung (Start vs. Ende, Delta) statt zwei Läufe zu
> vergleichen (`memory_peak`-Probe).

### C.2 Title-ID der laufenden Anwendung [QUELLE]

`sceSystemServiceGetAppTitleId` ist im SDK nur als Stub-Name vorhanden (keine Deklaration). Die
Signatur ist jedoch in drei unabhängigen, auf echten Konsolen gelesenen Quellen identisch:

| Quelle | Deklaration |
| --- | --- |
| onionHEN `util_platform.c`, `daemon_utils.cpp` | `int sceSystemServiceGetAppTitleId(int app_id, char* title_id)` |
| etaHEN `commands.cpp`, `Discord_RPC.c` | `int sceSystemServiceGetAppTitleId(int app_id, char* title_id)` |
| LiveShell `liveshell_daemon.cpp` | `int sceSystemServiceGetAppTitleId(int app_id, char* title_id)` |

Alle drei rufen sie mit `sceSystemServiceGetAppIdOfRunningBigApp()` als `app_id` auf, genau wie
AKENO. Die übergebenen Puffer sind 14, 32 bzw. 255 Byte. Die Funktion nimmt **keine** Längenangabe.

**Sicherheitsbegründung (nicht allein Prüfbytes):** Die Signatur (zwei Argumente, `int` und
`char*`) ist quellbelegt übereinstimmend; der Zielpuffer ist mit **256 Byte größer als der größte
bekannte Aufrufer-Puffer** (255 Byte), sodass die Funktion nicht über das hinausschreiben kann, was
jene Quellen ihr bereits gefahrlos geben. Die 64 Prüfbytes vor und die >3,7 KB hinter dem Fenster
sind eine **zusätzliche** Erkennung: Wird ein Überlauf bemerkt, wird die Abfrage für den Rest des
Laufs deaktiviert und als Fehler gemeldet (`EX_TITLE_OVERRUN`, `title_query_guard`-Probe). Außerdem
gilt: nur für eine gerade gemeldete laufende App, Aufruf-Budget pro Sitzung, abschaltbar über die
Datei `no-title-query`. Die Klassifikation aller Ausgänge ist unit-getestet (`test_title_classify`).

### C.3 Prozesskontext und Zeitachse [SIM]

Der Explorer ordnet sich weiter als Vordergrund/Hintergrund ein (eigene App-ID vs. laufende
Big-App). 6.1 zeichnet zusätzlich **Vordergrund-Sitzungen** auf: Beginn, Ende, Endzustand
(`still_running` / `replaced` / `not_reported`) und — wenn die App ohne gemeldete Nachfolge-App
verschwindet — eine Nachprüfung, ob ihre App-ID noch eine Title-ID liefert (`still_answers` =
eher in den Hintergrund/Home; `gone` = eher beendet). Das adressiert direkt Test 1, in dem die
Big-App vor Testende „nicht aktiv“ wurde: 6.1 kann nun Hinweise liefern, ob sie beendet wurde oder
nur den Vordergrund verließ (LiveShell dokumentiert, dass die PS5 **keine** laufende Big-App
meldet, solange ein Spiel auf dem Home-Bildschirm pausiert ist [QUELLE: `liveshell_daemon.cpp`]).

---

## D. Audio-Bibliotheks- und Sitzungsforschung

### D.1 Die ererbte Kernaussage (unverändert) [HW]

Bei byte-identischen Aufrufen auf derselben Konsole, Firmware und demselben Benutzer entscheidet
der **Prozesstyp**: Ein Vordergrund-Anwendungsprozess wird gehört und bekommt das Mikrofon, ein
Hintergrund-Payload nicht (`output_background` stumm, `microphone_background` `0x809B0081`). Welche
Systemkomponente das durchsetzt, ist nicht dokumentiert und wird nicht benötigt.

### D.2 Entdeckte Symbolnamen, weiter untersucht [QUELLE]

Der 6.0-Explorer löste `sceAudioOutGetFocusEnablePid`, `…AttachToApplicationByPid`,
`…DetachFromApplicationByPid`, `sceAudioOutChangeAppModuleState`, `sceAudioOutGetPortState`,
`sceAudioInGetSilentState` auf. 6.1 ergänzt die Quellanalyse, **ohne** eine dieser Funktionen
aufzurufen:

- Im gepinnten SDK sind diese Namen im `libSceAudioOut`-Stub generiert (aus Firmware-Exports)
  vorhanden; die zugehörige **Mbus-Familie** (`sceMbusAttachToApplicationByPid`,
  `sceMbusChangeAppModuleState`, `sceMbusDetachFromApplicationByPid`) ist in AnyPS5 nur als
  `NotImplemented`-Stub modelliert [QUELLE: `AnyPS5/core/libs/prx/libSceMbus/PendingExports.cpp`].
  Es gibt **keine** öffentliche Deklaration und keine belegte sichere Aufrufkonvention.
- `sceAudioOutGetPortState` ist in fünf Quellen mit zwei Argumenten deklariert
  (OpenOrbis, shadPS4, fpPS4, SharpProspero, AnyPS5), die Struktur mit 32 Byte in drei davon; die
  **Strukturgröße ist auf 12.20 unverifiziert** [QUELLE]. Ein read-only-Lesen wäre nur auf einem
  **eigenen** offenen Port sinnvoll — das kann nur der getrennte aktive Audio-Probe, nicht die
  Baseline.
- `sceAudioInGetSilentState` ist `int(int handle)` (shadPS4/SharpProspero/KytyPS5/AnyPS5) und
  braucht ein **offenes** Mikrofon-Handle, das dem Hintergrund-Payload verweigert wird
  (`0x809B0081`). Damit ist es im Hintergrund nicht nutzbar (`mic_silent_state_interface`-Probe).

**Diese Funktionen werden auf der PS5 des Eigentümers nicht aufgerufen.** Sie mit geratenen
Signaturen aufzurufen hieße, dem System AKENOs Audio als das einer anderen App auszugeben
(Spoofing); das ist ausgeschlossen und per CI verboten.

### D.3 Hypothese zur Audio-Sitzungs-Eigentümerschaft [HYP]

Jede Quelle mit **hörbarem** Ton während eines Spiels besitzt eine System-Audio-Sitzung: LiveShell
spielt über den **eigenen Mono-`VideoPlayer` von SceShellUI** (`SetVolume`/`SetIsMuted`/
`SetBGMSuppressionDisabled`) [QUELLE: `liveshell_shellui.cpp`], und die hörbare Vordergrund-Homebrew
ist der gestartete Anwendungsprozess. Der Hintergrund-ELF-Payload hat weder das eine noch das
andere. Das ist ein konsistentes Muster über Quellen hinweg, **kein** gemessener Mechanismus, und
**kein** Weg, den AKENOs Hintergrund-Payload ohne ShellUI-Injektion gehen kann (`audio_session_ownership`-Probe).

---

## E. Vordergrund/Hintergrund-Vergleich und die Evidenzmatrix

| Experiment | Build | Auf 12.20 gelaufen? | Was es zeigte | Wiederholung sinnvoll? |
| --- | --- | --- | --- | --- |
| Vordergrund-Audio (AKENO Native 0.1) | `d094e62bcaf1-ps5` | **Ja** [HW] | vier Töne hörbar | nein (geklärt) |
| Vordergrund-Mikrofon (Native 1.1) | 1.1.0-beta.1 | **Ja** [HW] | Pegel folgte Sprache | nein (geklärt) |
| Audio-Lab v1/v2 (Hintergrund) | `ae94851`/`7e28255` | **Ja** [HW] | akzeptiert, getaktet, stumm | nein (geklärt) |
| **Audio Lab v3** (eigener Portzustand, read-only) | `05ad4f3` | **Nein** [QUELLE] | nur PC/Chromium getestet | **ja — der nächste Audio-Test** (Abschnitt H) |
| **Voice Lab A–E** (Hintergrund-Mikro/Ausgabe/Duplex) | `7647019` | **Nein** [QUELLE] | nur PC getestet | teils — v3 liefert zuerst den entscheidenden Portzustand |
| Voice Alpha 0.1.0 (Hintergrunddienst) | `ce06ba04c048-ps5` | **Ja** [HW-berichtet] | verbunden+dekodiert, stumm, Mikro `0x809B0081` | nein (geklärt); Rohdateien fehlen im Repo |
| **Voice Alpha 0.2.0** (ehrliches Timing, Test-Ton) | `63b7ded` | **Nein** [QUELLE] | nur PC getestet | ja, aber erst nach v3-Portzustand |

**Geprüfte Hardwaretest-Historie (Anforderung 6):** Von den Builds, die im Repository als
„Hardware-Ergebnis“ auftauchen, ist nur **Voice Alpha 0.1.0** tatsächlich auf 12.20 gelaufen — und
dessen Zahlen sind die **berichtete** Fassung des Eigentümers; die Rohdateien (`voice(1).log`,
`report(1).txt` …) sind **nicht** im Repository [QUELLE: `PS5_PARTY_VOICE_0.1_HARDWARE_RESULTS_FW12.20.md`,
`PS5_VOICE_AUDIO_0.2.md`]. **Audio Lab v3, die Voice Lab (A–E) und Voice Alpha 0.2.0 sind nach
ihren eigenen Quellen und den Testplänen ausdrücklich noch nicht auf Hardware gelaufen**
([PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md), [PS5_BACKGROUND_VOICE.md](PS5_BACKGROUND_VOICE.md),
[PS5_VOICE_AUDIO_0.2.md](PS5_VOICE_AUDIO_0.2.md)). Diese Lücke ist in `prior_build_status`
maschinenlesbar festgehalten und steuert den Entscheidungsbaum: Ein nächster Test sollte Belege
liefern, die noch **fehlen** — nicht bereits Geklärtes wiederholen.

---

## F. LiveShell- und ShellUI-Befunde [QUELLE]

- LiveShell injiziert ein Modul in **SceShellUI** (OnionHEN-Injektor + Detour von
  `PUI.Application.Update`) und steuert Sonys `PUI.UI3.VideoView`/`VideoPlayer` per Mono. Der Ton
  läuft **in** SceShellUI (`VideoPlayer.SetVolume/SetIsMuted`), und der Player unterdrückt die
  System-BGM (`SetBGMSuppressionDisabled`). Hörbar ist er, **weil er in einem Systemprozess läuft**,
  nicht aus einem ELF-Loader-Payload [QUELLE: `liveshell_shellui.cpp`].
- Prozess-Eigentümerschaft: Die Abspieloperation gehört **SceShellUI**, nicht LiveShells Daemon.
  Der Daemon mountet nur Videos per nullfs in die Sandboxes und injiziert das Modul; er erzeugt
  keinen eigenen hörbaren Audiopfad [QUELLE: `liveshell_daemon.cpp`].
- Spielerkennung: `sceSystemServiceGetAppIdOfRunningBigApp` + `sceSystemServiceGetAppTitleId`
  (~1 Hz) — genau die read-only-Methode, die AKENO nun ebenfalls nutzt; sagt nichts über
  Audio-Rechte [QUELLE].

**Entscheidende Unterscheidung:** Erfolgreiche Audiowiedergabe **innerhalb** von ShellUI belegt
**nicht**, dass ein unabhängiger Hintergrund-Payload dieselben Audio-Rechte erhält. Was vor einem
Nachbau zu verstehen wäre: wie SceShellUI seinem eigenen `VideoPlayer` die Audio-Sitzung des
Benutzers zuteilt, und ob diese Zuteilung an den Systemprozess oder an eine aufrufbare,
dokumentierte Schnittstelle gebunden ist. In Phase 6.1 wird **kein** Code in `SceShellUI`, Spiele
oder den OnionHEN-Host injiziert oder geändert.

---

## G. Verbliebene Unbekannte

- Welcher der Prozessabfrage-Fehlerfälle auf 12.20 zutrifft (Messung steht aus bzw. braucht die
  6.0-Originaldateien) [UNBEKANNT].
- Die dauerhafte Title-ID zu `0x4018`/`0x8018` — erst der 6.1-Lauf oder die Originaldateien zeigen,
  ob `sceSystemServiceGetAppTitleId` auf 12.20 eine liefert [UNBEKANNT].
- Der genaue Strukturaufbau von `SceAudioOutPortState`/`SystemServiceStatus` auf 12.20 [UNBEKANNT].
- Ob ein Hintergrund-Payload je einen hörbaren Port oder ein Mikrofon bekommt [UNBEKANNT].
- Interne Game-Base-/Party-Datenquellen und -Signalwege [UNBEKANNT].

---

## H. Audio-Experiment-Entscheidungsbaum

Für jede verbliebene Hypothese: was bekannt/unbekannt ist, welche **eine** unterscheidende Messung
sie beantwortet, und ob ein vorhandener AKENO-Baustein sie ausführen kann. Priorisiert nach dem
Nutzen für die Engineering-Entscheidung, nicht nach Anzahl.

### H1 — Geht der Hintergrund-Port an **kein** Ausgabegerät (Routing) oder wird er stumm gemischt (Mixer)?

- **Bekannt:** Hintergrundausgabe wird akzeptiert und getaktet, ist aber stumm; Vordergrund ist
  hörbar [HW].
- **Unbekannt:** ob das System den Port an ein Gerät bindet.
- **Unterscheidende Messung:** `sceAudioOutGetPortState` read-only auf dem **eigenen** offenen Port
  (Gerätebits in `output`, `volume`).
- **API/Quelle:** zwei-Argument-Deklaration in fünf Quellen; Strukturgröße unverifiziert → mit
  Prüfbyte-Fenster und Stopp bei Überlauf.
- **Risiko:** mittel (öffnet einen eigenen Port). **Reversibel:** hoch.
- **Welcher Baustein:** **Audio Lab v3** (`05ad4f3`), bereits gebaut, noch nicht auf Hardware.
- **Ergebnisdeutung:** keine Gerätebits → Prozess-Routing-Beschränkung; Gerätebits + Volume>0 trotz
  Stille → Verlust später (Mixer/Mute des Prozesses).
- **Empfehlung:** **Rang 1.** Getrennter, bereits bestehender ELF; eigene Freigabe/Hardwareplan.
  Vom Explorer **nicht** automatisch gestartet.

### H2 — Ist der Mikrofon-Refusal (`0x809B0081`) prozessgebunden und auf 12.20 reproduzierbar?

- **Bekannt:** `sceAudioInOpen` im Hintergrund `0x809B0081`, im Vordergrund offen [HW].
- **Unbekannt:** ob es ausschließlich am Prozesstyp liegt.
- **Unterscheidende Messung:** ein einziger klassifizierter `sceAudioInOpen` im Hintergrund,
  danach sofort schließen.
- **Welcher Baustein:** die **Voice Lab** (Test A) oder Voice Alpha 0.2.0; beides öffnet ein Gerät →
  getrennter Build, nicht die Explorer-Baseline.
- **Risiko:** mittel-hoch. **Rang 3** (H1 liefert zuerst den Portzustand, der die Ausgabefrage trennt).

### H3 — Stimmt die Kontext-Einordnung des Explorers mit einem zweiten Systemsignal überein?

- **Bekannt:** Kontext wird aus eigener App-ID vs. Big-App abgeleitet [SIM/HW].
- **Unbekannt:** ob `sceSystemServiceGetStatus.is_in_background_execution` dasselbe sagt.
- **Unterscheidende Messung:** read-only `sceSystemServiceGetStatus` (Struktur in AnyPS5/shadPS4
  modelliert; Größe auf 12.20 unverifiziert → nur mit Prüfbyte-Fenster).
- **Welcher Baustein:** eine kleine Erweiterung des Explorers wäre denkbar, aber erst nach
  Verifikation der Strukturgröße — bis dahin **Forschungsblocker**, nicht umgesetzt.
- **Risiko:** niedrig (read-only), aber Layout unsicher. **Rang 4.**

### H4 — Liefert `sceSystemServiceGetAppTitleId` auf 12.20 eine Title-ID für die laufende App?

- **Bekannt:** Signatur quellbelegt; von LiveShell/etaHEN/onionHEN auf Konsolen genutzt [QUELLE].
- **Unterscheidende Messung:** der 6.1-Explorer selbst (bereits umgesetzt, read-only, mit Budget).
- **Welcher Baustein:** **dieser Explorer 6.1**. **Rang 2** (geringes Risiko, direkt verfügbar).

### H5 — Gibt ShellUI seinem `VideoPlayer` die Audio-Sitzung über eine aufrufbare Schnittstelle oder nur kraft Systemprozess?

- **Bekannt:** Audio ist hörbar, weil es in SceShellUI läuft [QUELLE].
- **Unbekannt:** ob eine dokumentierte, aufrufbare Zuteilung existiert.
- **Messung:** reine Quellanalyse; **kein** Eingriff in SceShellUI in dieser Phase.
- **Forschungsblocker:** ein Experiment hierzu bräuchte ShellUI-Injektion → separater Vorschlag mit
  Risiken und Voraussetzungen, nicht Teil von 6.1.

> Priorität: **H1 → H4 → H2 → H3 → H5.** H1 kann die Engineering-Entscheidung („Routing vs. Mixer“)
> ändern und nutzt einen bereits gebauten ELF; H4 ist ohne Risiko im Explorer enthalten. H3/H5 sind
> bis zur Klärung der Strukturgröße bzw. ohne ShellUI-Eingriff **Forschungsblocker**.

---

## I. Risiken und Kompatibilität

| Bereich | Maßnahme in 6.1 |
| --- | --- |
| Zweite Diagnosesitzung durch erneuten Start | `flock` + Heartbeat; erneuter Start setzt nur einen Marker (unit-getestet) |
| Absturz verfälscht nächste Sitzung | alte `control` beiseitegelegt (nie ausgeführt), alte Berichte nach `previous/` |
| Title-Abfrage schreibt über den Puffer | 256-Byte-Fenster > größter bekannter Aufrufer-Puffer; Prüfbytes + Stopp bei Überlauf; Budget; abschaltbar |
| Firmware exportiert eine Funktion nicht | weak Imports; fehlende Funktion → `UNSUPPORTED`/`NO_FUNCTION`, Rest läuft |
| Geratene Audio-Struktur/Signatur | nicht getan; nur quellbelegte Signaturen; Fokus/Attach/Routing nie aufgerufen (CI) |
| Audio/Mikrofon im Standard-Build | kein `sceAudioOut`/`sceAudioIn`-Symbol in der ELF (CI geprüft) |
| Stabiler OnionHEN-Host, Spiele, Account, PSN, DNS, Firmware | unverändert; keine Injektion, kein Patch, kein Netzwerk-Probing |

Das Import-Gate `console/check-explorer.sh` erzwingt die erlaubten Importe (jetzt inkl.
`sceSystemServiceGetAppTitleId` als weak) und verbietet weiterhin jede audio-/mikrofon-/
zustandsändernde Funktion.

---

## J. Vorgeschlagene nächste Entwicklungsphase

1. **6.1 auf 12.20 ausführen** (dieser read-only Build, Home → Spiel, z. B. Monster Hunter Stories 3):
   bestätigt live den `process_record`-Fehlerfall, die Title-ID-Verfügbarkeit und die Timeline. Sehr
   geringes Risiko, nur Berichtdateien. **Oder** die 6.0-Originaldateien nachliefern, um Abschnitt A
   mit genauen Codes/Zeitstempeln zu untermauern.
2. **Audio Lab v3 als getrennten Hardwaretest** (H1): read-only Portzustand des eigenen Ports. Eigene
   Freigabe und eigener Hardwareplan; vom Explorer **nicht** automatisch gestartet.
3. Erst nach dem Portzustand über Mikrofon-/Duplex-Tests (Voice Lab, H2) entscheiden.

Das Oberziel bleibt unverändert: unabhängige AKENO-Party-Voice während eines Spiels ohne PSN. Es
gilt erst als erreicht, wenn **auf echter PS5-Hardware mit laufendem Spiel** Zwei-Wege-Audio gehört
und getestet wurde.
