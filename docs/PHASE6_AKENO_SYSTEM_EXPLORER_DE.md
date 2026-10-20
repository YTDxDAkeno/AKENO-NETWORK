# AKENO NETWORK Phase 6.0 — AKENO System Explorer

**Stand:** 20. Oktober 2026 (Europe/Berlin)
**Status:** implementiertes, nur lesendes Diagnose-Payload plus PC-Simulation und Tests. Kein
Konsolenversuch ist durch dieses Dokument freigegeben; nichts wurde auf einer PS5 ausgeführt.
**Komponente:** `clients/ps5-system-explorer/` · **Branch:** `research/phase6-akeno-system-explorer`

Der AKENO System Explorer beantwortet eine Vorfrage der Phasen 4/5: *Welche Belege über das
Verhalten der PS5 sind aus legitimen, verfügbaren Schnittstellen überhaupt zu bekommen?* Er sammelt
diese Belege als Hintergrund-Payload (gleicher Prozesstyp wie der AKENO-Daemon), **ohne** einen
Audio-Port oder ein Mikrofon zu öffnen und **ohne** die Konsole zu verändern. Er ersetzt nichts an
Sonys Game Base und behauptet nicht, sie zu ersetzen.

Diese Forschung baut auf [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md),
[PS5_VOICE_AUDIO_0.2.md](PS5_VOICE_AUDIO_0.2.md), [PS5_BACKGROUND_VOICE.md](PS5_BACKGROUND_VOICE.md),
[PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md),
[SHELLUI_CONTROL_CENTER_RESEARCH.md](SHELLUI_CONTROL_CENTER_RESEARCH.md),
[PS5_NATIVE_ADR.md](PS5_NATIVE_ADR.md) und
[phase5-fakepsn-native-gamebase-feasibility-de.md](phase5-fakepsn-native-gamebase-feasibility-de.md)
auf und fasst deren Hardware-Belege maschinenlesbar zusammen.

---

## A. Architektur

Der Explorer trennt die Diagnoselogik strikt von den PS5-Aufrufen. Nur eine Datei ruft
Systemfunktionen auf; alles andere ist portabel und mit Unit-Tests abgedeckt.

```
                 ex_execute()  (ex.c)
                      │  Module nacheinander, alle nur lesend
   ┌──────────────────┼───────────────────┬───────────────┬───────────────┐
   ▼                  ▼                   ▼               ▼               ▼
 Modul A           Modul B             Modul C         Modul D         Modul E
 Prozess/App       Bibliotheken        Audio/Mikro     ShellUI/        Timeline
 (ex_process.c)    (ex_libraries.c)    read-only       Game Base       (ex_timeline.c)
   │                  │                 (ex_audio.c)    (ex_shellui.c)    │
   └──────────────────┴─────────┬───────┴───────────────┴────────────────┘
                                 ▼
                      ex_backend (ex_backend.h): die einzige Naht zur Konsole
                      ┌───────────────────────────┬───────────────────────────┐
                      ▼                           ▼
             ex_backend_ps5.c               ex_backend_host.c
             (echte PS5-Aufrufe,            (Simulation mit Fehler-
              nur lesend)                    injektion, virtuelle Uhr)
                                 │
                                 ▼
                      Modul F: Reporte (ex_report.c)
   report.json · modules.json · audio-diagnostics.json · events.jsonl · system-summary.txt
```

**Genutzte PS5-APIs (alle schreibgeschützt, alle auf 12.20 in einem AKENO-Build belegt oder
Standard-FreeBSD/POSIX des gepinnten SDK; Datei `src/ex_backend_ps5.c`):**

| Funktion | Zweck | Beleg |
| --- | --- | --- |
| `sceKernelSendNotificationRequest` | Start-/Endbenachrichtigung | Daemon, Native, Voice Lab (12.20) |
| `kernel_get_fw_version` | Firmware | Daemon, Native (12.20) |
| `sysctl(KERN_PROC_PID)` | eigener Prozesssatz (nur diese PID) | FreeBSD, SDK `samples/ps` |
| `sceKernelGetAppInfo` (weak) | eigene App-Identität für `getpid()` | Natives Kontextcheck (12.20) |
| `sceSystemServiceGetAppIdOfRunningBigApp` (weak) | Vordergrund-App-ID | Native; websrv `hbldr.c` |
| `sceUserServiceInitialize` / `GetForegroundUser` (weak) | Vordergrundbenutzer | Daemon; Native |
| `getrusage` / `getrlimit` | CPU-Zeit, Ressourcengrenzen des Prozesses | POSIX (SDK-libc) |
| `dlopen`/`dlsym`/`dlclose` | Bibliotheks-/Symbol-**Nachschlag** (nie aufgerufen) | `samples/hello_dlfcn`; AKENO-Labs |
| `stat` | Dateivorhandensein unter `/system/common/lib` | POSIX |

Das Import-Gate `console/check-explorer.sh` erzwingt bei jedem Build: nur diese Importe, nur
`ex_backend_ps5.o` ruft Konsolenfunktionen auf, `ex_backend_ps5.o` enthält keinen fest verdrahteten
Bibliotheksnamen (kann also nur übergebene Namen laden), jede `.sprx`-Zeichenkette ist eine bekannte
Bibliothek, und **kein** `sceAudioOut`/`sceAudioIn`-Symbol ist in der ELF. Verifiziert: die gebaute
ELF importiert genau fünf Systemfunktionen (drei davon weak) und keine Audio-/Pad-/Voice-Symbole.

**Report-Format.** Jede Beobachtung ist eine „Probe“ mit Modul, Name, Status
(`OK` / `NOT_AVAILABLE` / `UNSUPPORTED_ON_FW_12_20` / `SKIPPED` / `ERROR`), Belegstufe
(`HARDWARE_VERIFIED` / `SOURCE_VERIFIED` / `HYPOTHESIS` / `UNKNOWN`), optionalem Fehlercode,
gemessenem Wert und einer **Grenzangabe** (was die Probe *nicht* belegt). Fehlende Messungen
erscheinen als expliziter Status, nie als erfundener Wert. Alle Texte werden redigiert.

---

## B. Capability-Matrix — was auf FW 12.20 inspizierbar ist

| Datum | Inspizierbar? | Wie / warum nicht |
| --- | --- | --- |
| Firmware-Version | **Ja** | `kernel_get_fw_version` |
| Eigene PID/PPID | **Ja** | `getpid`/`getppid` |
| Eigener Prozesssatz (comm, Zustand, Startzeit, RSS) | **Ja** | `sysctl KERN_PROC_PID` nur für die eigene PID |
| Eigene App-Identität (App-ID, Title-ID) | **Teilweise** | `sceKernelGetAppInfo(getpid)`; Struktur-Layout undokumentiert, Title-ID konservativ an zwei Offsets (20/16) gelesen |
| Vordergrund-App-ID | **Ja** | `sceSystemServiceGetAppIdOfRunningBigApp` (nur ID, kein Fokuszustand) |
| **Prozesskontext** (Vordergrund/Hintergrund) | **Ja (abgeleitet)** | eigene App-ID vs. laufende Big-App; entscheidende Variable für Audio/Mikro |
| Vordergrundbenutzer | **Ja** | `sceUserServiceGetForegroundUser` |
| Ressourcengrenzen/-nutzung | **Ja** | `getrlimit`/`getrusage` (eigener Prozess) |
| Scheduling über die Zeit | **Ja (indirekt)** | gemessene Lücken zwischen Polls in der Timeline |
| Vorhandensein von Systembibliotheken | **Ja** | `stat` unter `/system/common/lib` |
| Exportierte Symbolnamen | **Ja (nur Name)** | `dlsym` bestätigt den Namen, nicht Signatur oder Aufrufbarkeit |
| Audio-Port-Zustand | **Nein (Baseline)** | braucht einen offenen Port; der Baseline-Build öffnet keinen |
| Mikrofon-Fähigkeit | **Nein (Baseline)** | kein Mikrofon-Open; frühere Hardware-Belege werden wiedergegeben |
| Privater Speicher fremder Prozesse | **Nein (verboten)** | wird nicht gelesen; fremde Prozesse werden nicht gescannt |
| Fokus/Attach-pro-Prozess | **Nein** | keine öffentliche Deklaration; Namen nur aufgelistet, nie aufgerufen |
| Interne Game-Base-/Party-Signalwege | **Nein** | keine identifizierte lokale Schnittstelle (Abschnitt D) |

---

## C. Audio-Befunde — Vordergrund (funktioniert) vs. Hintergrund (stumm)

Der Explorer selbst öffnet **keinen** Port und **kein** Mikrofon. Modul C gibt die bereits auf der
Konsole des Eigentümers (FW 12.20) gemessenen Befunde wieder und ordnet sie dem gemessenen
Prozesskontext zu. Die Belege stammen aus den Audio-Labs v1/v2 und Voice Alpha 0.1.0.

| Derselbe Aufruf | Vordergrund-App (AKENO Native, `FAKE00000`) | Hintergrund-Payload (ELF-Loader) |
| --- | --- | --- |
| `sceAudioOutOpen(0xFF, 0, 0, 1024, 48000, S16-Stereo)` + `Output` | **hörbar** (Native 0.1, vier Töne) | **stumm**: akzeptiert und getaktet, Home-Bildschirm und Spiel |
| `sceAudioOutOpen` mit Vordergrundbenutzer, Port 0/2/3 | — | **`0x80260011`** |
| `sceAudioInOpen(user, 1, 0, 256, 16000, 0)` via `dlopen` | **geöffnet**, Pegel folgte Sprache (Native 1.1) | **`0x809B0081`** (gleicher Code wie `scePadOpen` im Hintergrund) |

**Schlussfolgerung (HARDWARE_VERIFIED als Korrelation, Mechanismus HYPOTHESE):** Bei
byte-identischen Aufrufen auf derselben Konsole, derselben Firmware, demselben Benutzer entscheidet
der **Prozesstyp**. Ein Vordergrund-Anwendungsprozess hat die Audio-Session und Gerätebindung des
Benutzers; ein Hintergrund-Payload nicht. Welche Systemkomponente das durchsetzt, ist nicht
dokumentiert und wird nicht benötigt. Der Explorer misst daher den Prozesskontext und stellt ihn
den früheren Audio-Ergebnissen gegenüber — das ist der nächste belastbare Schritt ohne einen
weiteren Audio-Aufruf.

Die Fokus-/Attach-Funktionen (`sceAudioOutGetFocusEnablePid`, `…AttachToApplicationByPid`) existieren
nur als **Namen** ohne öffentliche Deklaration. Sie aufzurufen hieße, dem System AKENOs Audio als
das einer anderen App auszugeben (Spoofing); das ist ausgeschlossen und per CI verboten. Der
Explorer listet die Namen nur auf.

---

## D. ShellUI und Game Base — quellbelegte Schlüsse und offene Abhängigkeiten

Für Phase 6 lokal gelesene, gepinnte Revisionen: aydencharles/onionHEN `aaa4264` (v0.9.0) und
`b23ffe6`, OnionBuddies/onionHEN-plugin-sdk `72527c3`, etaHEN/etaHEN `dafa13b`, ZiZc3/LiveShell
`1f87dcc` (v1.4.1), Blaze414/ps5-theme `e29735f`, earthonion/np-fake-signin `03403bc`,
ps5-payload-dev/offact `8c870eb`, boykopovar/AnyPS5 `eb75006`.

| Beobachtung | Belegstufe | Aussage | Grenze |
| --- | --- | --- | --- |
| onionHEN-Plugin-Seiten (onion.ui ABI 1) | HARDWARE_VERIFIED | AKEN00001 lief auf 12.20 und zeigte seine Seite | Eine per Shortcut erreichte Settings-Seite ist keine Control-Center-/Game-Base-Integration |
| onionHEN-Toolbox-Shortcut (L2+R3) | SOURCE_VERIFIED | der Host liest das Pad selbst; die Hintergrund-Controller-Sperre (`0x809B0081`) gilt dort nicht | öffnet nur die Toolbox-Wurzel, nicht direkt eine Plugin-Seite |
| LiveShell animierter Hintergrund | SOURCE_VERIFIED | injiziert ein Modul in **SceShellUI** (OnionHEN-Injektor + Detour von `PUI.Application.Update`) und steuert Sonys `PUI.UI3.VideoView` per Mono; Videos werden per nullfs in die Sandboxes gemountet | Host-Injektion in SceShellUI (Mono-Reflection), firmware-/buildspezifisch; für AKENO out of bounds |
| LiveShell Spiel-Pause | SOURCE_VERIFIED | erkennt das Spiel mit `GetAppIdOfRunningBigApp` + `GetAppTitleId` (1 Hz) — dieselbe read-only-Methode, die AKENO nutzt | bestätigt nur die Erkennungsmethode, keine Audio-Rechte |
| LiveShell Ton | SOURCE_VERIFIED | Audio läuft **in** SceShellUI (Mono `VideoPlayer.SetVolume/SetIsMuted`); hörbar, weil es in einem Systemprozess läuft | gerade **nicht** aus einem Hintergrund-Payload wie AKENO |
| ps5-theme statisches Overlay | SOURCE_VERIFIED | read-only nullfs-Overlays im Speicher | nur Medien; keine dynamische AKENO-Datenbindung in Game Base |
| etaHEN ShellCore-Offsets | SOURCE_VERIFIED | wählt nur bekannte Offset-Sätze, bricht sonst ab; kein 12.20-Fall | Nicht-Kompatibilitätsnachweis für 12.20-ShellCore-Patching |
| NP-Party-Zustände (etaHEN) | SOURCE_VERIFIED | Party-Funktionen setzen mehrere nichtlokale Zustände voraus | keine Implementierung/kein Protokoll |
| np-fake-signin (PS5-Pfad) | SOURCE_VERIFIED | schreibt lokale NP-Dateien/Registry; ShellCore-Patch nur PS4 | lokale Persistenz, kein Game-Base-Adapter; für AKENO verboten |
| offact | SOURCE_VERIFIED | lokale Account-Aktivierung; „kein Online-PSN-Gaming“ | ändert Account-Registry; verboten |
| AnyPS5 `SystemServiceStatus` | SOURCE_VERIFIED | Emulatormodell (`is_in_background_execution`, …); Fokus-PID nur Stubs | kein Konsolen-Messwert |
| originale Game Base als Backend | **UNKNOWN** | keine identifizierte Schnittstelle, um Freunde/Presence/Nachrichten/Partys/Voice in die System-UI zu speisen | „nicht verfügbar über identifizierte Schnittstellen“ ist kein Unmöglichkeitsbeweis; NP-Spoofing ist keine Alternative |
| lokales Party-Audio vs. Sony-Signaling | **UNKNOWN** | keine Quelle trennt ein lokal nutzbares Party-Audio von Sonys Netzsignalisierung | keine offene Schnittstelle, kein 12.20-Beleg |

**Kernaussage:** Jeder bekannte hörbare oder in die System-UI schreibende Homebrew-Weg (LiveShell,
etaHEN-Toolbox, Themes) läuft **in einem Systemprozess (SceShellUI)** oder injiziert in das Spiel —
beides für AKENO ausgeschlossen. AKENOs Hintergrund-Payload erhält über den ShellUI-Host **keine**
zusätzlichen Rechte: ein onionHEN-Plugin ist derselbe Prozesstyp wie der Daemon.

---

## E. Belegtabelle

| Kategorie | Beleg |
| --- | --- |
| **Verifizierte Fakten (Quelle/SDK)** | Genutzte Funktionen sind im gepinnten SDK deklariert bzw. in zitierten Quellen belegt; `dlsym` bestätigt Symbolnamen; LiveShell/etaHEN/ps5-theme-Mechaniken wie oben. |
| **Hardware-Belege (FW 12.20, früher gemessen)** | Vordergrund-Audio hörbar (Native 0.1); Mikrofon im Vordergrund geöffnet, Pegel folgte Sprache (Native 1.1); Hintergrundausgabe akzeptiert+getaktet, aber stumm (Audio-Lab v2, Voice 0.1.0); `0x80260011` (Vordergrundbenutzer-Port), `0x809B0081` (Mikro/Pad im Hintergrund); Prozessliste + `sceKernelGetAppInfo` für Spielerkennung; Benachrichtigungen auch im Spiel; onionHEN-Plugin-Seite sichtbar. |
| **Hypothesen (nicht bewiesen)** | Ein pro-Prozess-Audio-/Gerätekontext, den das System der Vordergrund-App gibt, erklärt den Unterschied; Fokus ist eine mögliche Form davon. Scheduling-Lücken *könnten* auf Drosselung hindeuten — der Explorer notiert sie nur. |
| **Nicht verfügbar / unbekannt** | Audio-Port-Zustand und Mikrofon-Fähigkeit ohne offenen Port/Mikro; der genaue Audio-Port-Zustands-Strukturaufbau; Deklarationen der Fokus/Attach-APIs; interne Game-Base-/Party-Datenquellen und -Signalwege; ob ein Hintergrund-Payload je einen hörbaren Port/ein Mikrofon bekommt. |

Die maschinenlesbare Fassung dieser Tabelle steht pro Lauf in `report.json` (`probes[]` mit
`status`/`evidence`/`limitation`) und `modules.json`.

---

## F. Risiken

| Bereich | Risiko | Maßnahme in Phase 6 |
| --- | --- | --- |
| Performance | Diagnose stört das Spiel | 1-Hz-Polling, begrenzte Sitzung (max. 30 min), sauberes Ende; keine Hochfrequenz-Scans |
| Kompatibilität | Firmware exportiert eine Funktion nicht | weak Imports; fehlende Funktion → `UNSUPPORTED_ON_FW_12_20`, Rest läuft |
| Lifecycle | Prozess wird im Spiel seltener geplant | Scheduling-Lücken werden gemessen und notiert, **nicht** als Suspendierung gedeutet |
| Audio | versehentliche Audio-/Mikro-Nutzung | kein Audio-/Mikro-Symbol in der ELF (CI); aktiver Probe ist ein getrennter Build |
| Instabile Bibliothek | `dlopen` einer Broadcast-/Recording-Bibliothek hängt (12.20 belegt) | `libSceVideoRecording`/`libSceGameLiveStreaming` unter Quarantäne, nie geladen |
| Fremde Prozesse | Lesen fremden Speichers | ausgeschlossen; nur eigener Prozess, nur Prozess-**Metadaten** |
| Firmware/Account | System-/NP-/DNS-Änderung | vollständig ausgeschlossen; kein Netzwerk-Probing, keine Bereitstellung |
| Nutzerdaten | Tokens/Nachrichten in Reporten | automatische Redaktion; nur Zustände/Codes/Zähler |

---

## G. Nächste Experimente (nach Nutzen gewichtet)

| Rang | Experiment | Forschungswert | Vorhandene Belege | Risiko | Aufwand | Reversibilität |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | **Explorer auf 12.20 ausführen** (dieser read-only Build, Home → Spiel, Marker) | hoch: bestätigt Kontext/Timeline/Bibliothekslage live | Payload fertig, Tests grün | sehr niedrig | niedrig | vollständig (nur Reportdateien) |
| 2 | **Optionaler aktiver Audio-Probe** (getrennter Build `AKENO_EXPLORER_AUDIO_PROBE`): eigener Port, `sceAudioOutGetPortState` auf dem eigenen Handle, Hintergrund-vs-Vordergrund-A/B | hoch: trennt „Routing- vs. Mixerverlust“ | Audio-Lab v3 Pläne, Portzustands-Quellen | mittel (öffnet einen Port) | mittel | hoch (eigener Port, kein Systemeingriff) |
| 3 | **Foreground-Referenz in AKENO Native** erneut, mit gleichem Report-Schema | mittel: Gegenprobe zum Hintergrund | Native 0.1/1.1 Hardware | niedrig | niedrig | hoch |
| 4 | **Mikrofon-Prototyp im Hintergrund** (ein einziger `sceAudioInOpen`, Code klassifiziert) | mittel: prüft, ob `0x809B0081` reproduzierbar/umgehbar ist | Voice 0.1.0 | mittel-hoch | hoch | mittel (ein Open, sofort geschlossen) |
| 5 | **ShellUI-Weg** (Plugin-seitig, ohne Host-Injektion) für reine Anzeige | niedrig für Audio, mittel für UI | onionHEN-Plugin belegt | niedrig | mittel | hoch |

Jedes dieser Experimente braucht eine eigene Freigabe, eigene Review und einen eigenen
Hardwaretestplan. Rang 2 und 4 öffnen Geräte und sind **nicht** Teil dieses Builds.

---

## H. Grenzen (was der Explorer nicht kann)

- Er **misst keine** Audio-Hörbarkeit und **keine** Mikrofon-Aufnahme — er öffnet dafür nichts. Was
  er zu Audio sagt, ist der gemessene Prozesskontext plus frühere Hardware-Belege.
- Ein auflösbares Symbol (`dlsym`) belegt nur den **Namen**, nicht die Signatur und nicht, dass ein
  Aufruf sicher ist oder das tut, was der Name nahelegt.
- Das `sceKernelGetAppInfo`-Struktur-Layout ist undokumentiert; die Title-ID wird konservativ an
  zwei Offsets gelesen. Bei Widerspruch wird nichts behauptet.
- Scheduling-Lücken sind Messwerte, **keine** nachgewiesene Suspendierung.
- Er inspiziert **keine** fremden Oberflächen (Game Base, Party-UI, Spiele) und liest keinen
  fremden Prozessspeicher; Game-Base-/Party-Interna bleiben unbekannt.
- Ein PC-Lauf (Simulation) ist **nie** ein Beleg für Konsolenverhalten.
- Game Base wird nicht ersetzt, nicht umgeleitet und nicht emuliert; FakePSN und In-Game-Voice sind
  durch diese Phase **nicht** gelöst. Ziel ist, die nächste Entscheidung auf echte Belege zu
  stützen.

---

## Bauen und Prüfen

```sh
# PC: Diagnoselogik gegen die simulierte Konsole, mit Unit-Tests (ASan/UBSan)
cmake -S clients/ps5-system-explorer -B clients/ps5-system-explorer/build -DAKN_EX_SANITIZE=ON
cmake --build clients/ps5-system-explorer/build
clients/ps5-system-explorer/build/akn_ex_tests

# PS5: die echte ELF mit dem gepinnten SDK (kein Konsolen-Deployment)
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-system-explorer/console
clients/ps5-system-explorer/console/check-explorer.sh clients/ps5-system-explorer/build-ps5
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-system-explorer/console delivery
```

Das gepinnte SDK (`ps5-payload-dev/sdk` Commit `f5d40f02770e9b0001dd9e18056eca70b3da8b40`) ist das
der übrigen PS5-Builds. Ein Erfolgsfall der PC-Tests bestätigt nur die Logik, nicht das
Konsolenverhalten. Installationsanleitung und Testplan (Deutsch):
[clients/ps5-system-explorer/package/README-DE.md](../clients/ps5-system-explorer/package/README-DE.md),
[HARDWARE-TEST-DE.md](../clients/ps5-system-explorer/package/HARDWARE-TEST-DE.md),
[SAFETY.md](../clients/ps5-system-explorer/package/SAFETY.md).
