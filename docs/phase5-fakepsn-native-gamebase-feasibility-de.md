# AKENO NETWORK Phase 5.0 – Machbarkeit: FakePSN und native Game Base

**Stand:** 19. Oktober 2026 (Europe/Berlin)
**Status:** reine Offline-Forschung. Kein Konsolenversuch ist durch dieses Dokument freigegeben.

## Ergebnis

AKENO kann seine eigene soziale Identität, Freundesliste, Nachrichten und Partys unabhängig betreiben. Die vier untersuchten Quellen liefern aber keinen Nachweis, dass die originale PS5 Game Base oder ihr Party-Audio ohne eine von Sony authentifizierte NP-Sitzung als lokale, generische Schnittstelle nutzbar ist. Ein Fake-Anmeldestatus ist keine Ersatzimplementierung dieser Dienste.

Die sichere Richtung ist daher ein **klar benannter AKENO-Social-Bereich in einem vorhandenen, flüchtigen PS5-UI-Einstiegspunkt**, gespeist durch die bestehende lokale AKENO-IPC. Game Base bleibt unangetastet; AKENO-Voice bleibt zunächst ein eigener Audio-Pfad. Dies ist die nächstliegende native wirkende Alternative, ohne Sony-Dienste nachzubilden oder Systemzustand zu verändern.

- **GO:** Offline-Evidenzprüfung und ein späterer, separat freizugebender flüchtiger UI-Prototyp mit statischen AKENO-Identitätsdaten.
- **NO-GO:** NP Fake Signin, OffAct, Registry-/Account-ID-Änderungen, persistente NP-Dateien, ShellCore-Patches, Game-Base-Umleitung, Zertifikatsersatz, PSN-Weiterleitung und native Party-Audio-Behauptungen.

## Methode und Beleggrenzen

Die Referenzen wurden nur als lokale, flache Checkouts gelesen. Der beigefügte Test pinnt die gelesenen Revisionen und prüft konkrete Textmarker. Er führt keinen Payload aus und kontaktiert keine Sony-Dienste.

| Projekt | Revision | Belegt | Nicht belegt |
| --- | --- | --- | --- |
| [np-fake-signin](https://github.com/earthonion/np-fake-signin/tree/03403bc7bc33e339cc30e7811847c66655af2756) | `03403bc7` | lokale NP-Dateien und Registry-Status | Game-Base-Datenadapter, Friends, Nachrichten oder PS5-Party-Audio |
| [offact](https://github.com/ps5-payload-dev/offact/tree/8c870eb809cc46f96835e977cae3354b4cf70247) | `8c870eb8` | lokale Aktivierung durch Account-ID/Typ/Flags | Online-PSN-Spiel, soziale Backend-Emulation oder Game Base |
| [ps5-theme](https://github.com/Blaze414/ps5-theme/tree/e29735f852e6d7bbd2140355a8852b72bc9207de) | `e29735f` | flüchtige, schreibgeschützte ShellUI-Asset-Overlays | dynamische AKENO-Daten in Game Base oder 12.20-Kompatibilität |
| [etaHEN](https://github.com/etaHEN/etaHEN/tree/dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c) | `dafa13b` | ShellCore-Patches brauchen firmwaregenaue Offsets | Offsets, ShellUI- oder Party-Audio-Unterstützung für 12.20 |

Eine fehlende Funktion in diesen vier Quellen beweist nicht, dass sie auf der Konsole unmöglich ist. Sie bedeutet jedoch, dass für Phase 5 kein Quellnachweis vorliegt, auf den eine Implementierungs- oder Sicherheitszusage gestützt werden darf.

## Quellbefunde

### NP Fake Signin

Die [README](https://github.com/earthonion/np-fake-signin/blob/03403bc7bc33e339cc30e7811847c66655af2756/README.md#L7-L7) beschreibt ein Payload, das NP-Dateien schreibt und Registry-/ShellCore-Zustand verändert. Im PS5-Pfad schreibt [`write_np_files`](https://github.com/earthonion/np-fake-signin/blob/03403bc7bc33e339cc30e7811847c66655af2756/np-fake-signin.c#L234-L253) `auth.dat` und `config.dat` in die private Systembenutzerstruktur. [`write_registry_state`](https://github.com/earthonion/np-fake-signin/blob/03403bc7bc33e339cc30e7811847c66655af2756/np-fake-signin.c#L256-L287) setzt u. a. Online-ID, NP-Umgebung und Anmeldeflag.

Die oft zitierte ShellCore-Speicheränderung ist im gezeigten Quellstand [PS4-exklusiv kompiliert](https://github.com/earthonion/np-fake-signin/blob/03403bc7bc33e339cc30e7811847c66655af2756/np-fake-signin.c#L299-L377); die Aufrufstelle ist ebenfalls mit `#ifndef PS5` geschützt ([L401–L410 und L503–L507](https://github.com/earthonion/np-fake-signin/blob/03403bc7bc33e339cc30e7811847c66655af2756/np-fake-signin.c#L401-L410)). Der PS5-Pfad belegt daher lokale Persistenz, nicht einen dynamisch aktualisierten PS5-ShellCore-Kontext.

Es gibt keinen Code für ein AKENO-kompatibles Friends-/Presence-/Message-/Party-Protokoll und keine Game-Base-Backend-Implementierung. Das Payload setzt außerdem vorherige Aktivierung und eine nicht-nullige Account-ID voraus. Es ist deshalb Risikobeleg, keine Basis für AKENO-Anmeldung.

### OffAct

OffAct ist laut [README](https://github.com/ps5-payload-dev/offact/blob/8c870eb809cc46f96835e977cae3354b4cf70247/README.md#L1-L9) lokale Kontoaktivierung und erklärt ausdrücklich, dass es kein Online-PSN-Gaming aktiviert. [`OffAct_SetAccountId`](https://github.com/ps5-payload-dev/offact/blob/8c870eb809cc46f96835e977cae3354b4cf70247/offact.c#L67-L72), `OffAct_SetAccountType` und `OffAct_SetAccountFlags` schreiben Registry-Werte; die UI ruft sie direkt auf ([main.c L72–L93](https://github.com/ps5-payload-dev/offact/blob/8c870eb809cc46f96835e977cae3354b4cf70247/main.c#L72-L93)).

Damit demonstriert OffAct eine lokale Zuordnung von PS5-Benutzer-Slot zu Account-ID. Es demonstriert weder eine sichere AKENO-ID-Verknüpfung noch eine reversible Migration von Speicherdaten, Trophäen oder bestehender lokaler Identität. Diese Änderungen bleiben ausgeschlossen.

### ps5-theme

ps5-theme zeigt einen Sicherheitsgrundsatz, keine soziale Integration. Es verwendet [read-only nullfs-Overlays](https://github.com/Blaze414/ps5-theme/blob/e29735f852e6d7bbd2140355a8852b72bc9207de/docs/how-it-works.md#L77-L84), die im Speicher leben. Es dokumentiert außerdem, dass ShellUI durch eigene Sandbox-Views auf System-Assets zugreift ([L86–L101](https://github.com/Blaze414/ps5-theme/blob/e29735f852e6d7bbd2140355a8852b72bc9207de/docs/how-it-works.md#L86-L101)).

Die Quelle beschränkt sich absichtlich auf Medien; Layout und Logik liegen laut Dokumentation in verschlüsseltem ShellUI-JavaScript, Runtime-Patching ist außerhalb ihres Scopes ([L120–L146](https://github.com/Blaze414/ps5-theme/blob/e29735f852e6d7bbd2140355a8852b72bc9207de/docs/how-it-works.md#L120-L146)). Ein Icon- oder Bildoverlay kann keine AKENO-Freundesliste in Game Base erzeugen. Die dort beschriebenen Laufzeitbeobachtungen gelten zudem nicht als Nachweis für 12.20.

### etaHEN

etaHEN belegt, dass ShellCore-Änderungen firmwaregenaue Offsets benötigen: [`apply_shellcore_patches`](https://github.com/etaHEN/etaHEN/blob/dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c/Source%20Code/bootstrapper/Byepervisor/hen/src/patch_shellcore.cpp#L157-L247) wählt nur bekannte Versionen und bricht sonst ab. Dieser Snapshot enthält keinen 12.20-Fall. Das ist ein **Nicht-Kompatibilitätsnachweis für 12.20**, nicht bloß ein fehlender Test.

Eine Fehlertext-Tabelle nennt NP-Party-Zustände wie „not initialized“, „signed out“, „server error“ und „voice not enabled“. Sie ist keine Party-Implementierung, kein Protokoll und keine Alternative zum Sony-Backend. Sie zeigt nur, dass Party-Funktionen mehrere nichtlokale Zustände voraussetzen können.

## Architekturkarte für Firmware 12.20

| Ebene | belegte Rolle | Aussage für Phase 5 / FW 12.20 |
| --- | --- | --- |
| lokaler PS5-Benutzer | Foreground User und lokaler Slot; NP Fake Signin liest ihn über UserService | AKENO-Zuordnung außerhalb dieser Identität halten; keine Account-ID-Änderung |
| NP-Dateien und Registry | lokale NP-/Profilfelder und Anmeldeflags | beobachtete Persistenzpfade, die verboten sind; kein AKENO-Adapter |
| ShellCore | proprietärer Prozess mit binär-/firmwarespezifischen Patches | keine 12.20-Offets in der Quelle; nicht patchen |
| ShellUI | gerenderte Shell mit Sandbox-Views; Asset-Overlay ist möglich | dynamische Seiten, Game Base und Datenbindung sind damit nicht nachgewiesen |
| Game Base | ursprüngliche soziale UI und NP-Party-Fehlerdomäne | keine offline-fähige API oder lokales Datenschema in den Referenzen |
| Party-/Audio-Dienste | getrennte NP-Party- und Voice-Zustände | keine offene Schnittstelle, kein Signaling-/Mixer-Nachweis und keine 12.20-Unterstützung |

## Funktionsbewertung

| Funktion | Verifizierter Beleg | Abhängigkeiten / FW 12.20 | Risiken und Schwierigkeit | Hardwaretest / sicherste reversible Umsetzung |
| --- | --- | --- | --- | --- |
| AKENO-Anmeldung | nur lokale NP-Statusänderungen; kein AKENO-Auth-Adapter | eigene AKENO-Session, lokale IPC, eigene UI; 12.20 nicht belegt | hohe Verwechslungs- und Kontorisiken; echte Shell-Anmeldung hoch | für Offline-Adapter nein; AKENO-Anmeldung nur im eigenen flüchtigen Bereich |
| lokales Profil verknüpfen | OffAct zeigt nur Registry-Schreiben | AKENO-Mapping ohne PS5-System-ID; firmwareunabhängig | mit Account-ID sehr hohes Save-/Trophäenrisiko | nein; nutzerbestätigter zufälliger Alias, keine PS5-Identifier speichern oder ändern |
| Name, Avatar, Banner, Status | ps5-theme: statische Medienoverlays, keine Datenbindung | AKENO-IPC und serialisiertes Profil; 12.20 unbekannt | ShellUI-Stabilität; mittel außerhalb des Originalprofils, sehr hoch darin | erst nach Offline-Renderer; klar markierte temporäre AKENO-Profilkarte |
| Freunde und Nachrichten | keine Referenz implementiert Friends-/Message-Backend | AKENO-API/IPC, Cache, Fehlermodell; ShellUI-Anzeige unbekannt | Datenschutz und fehlerhafte Presence; mittel im eigenen Bereich, extrem hoch in Game Base | Adapter-/Renderer nein; read-only Freundes-Snapshot in eigener Seite |
| originale Game Base | nur Assetnamen/Fehlertexte, keine Datenquelle oder API | unbekannte ShellUI-/NP-Dienste, wahrscheinlich Sony-Auth; 12.20 nicht belegt | höchste Stufe: Shell, Kontostatus, Rest Mode; extrem hoch | später zwingend, aber nicht Teil dieses Plans; nicht verwenden |
| originales Party-UI und AKENO-Voice | keine Party-Signaling-, Medien- oder Mixerimplementierung | unbekannte privilegierte Sessions, Schlüssel, Mixer; 12.20 nicht belegt | Mikrofon, Audioverlust, Absturz; extrem hoch | zwingend für spätere Hypothese; AKENO-Voice nur im eigenen Pfad |
| System-, Spielaudio und Controller erhalten | kein unabhängiger nativer Party-Mixerzugriff belegt | eigener kontrollierbarer AKENO-Audiopfad und Fokusregeln; 12.20 unbekannt | Stille, Fokuskonflikte und Geräteprobleme; hoch | ja, später; keine Systemmixer-Hooks, nur belegter Vordergrund-Audiokontext |

## Warum der Hintergrund-Voice-Payload stumm bleibt

Die Quellbasis dieses Checkouts enthält keine AKENO-Voice-Implementierung. Diese Diagnose ist daher eine **Hypothese aus der beschriebenen Beobachtung**, kein geprüfter Befund. Dass AKENO Native im Vordergrund hörbar ist, während ein Hintergrund-Payload stumm bleibt, passt zu drei getrennten Voraussetzungen:

1. Der Vordergrundprozess besitzt wahrscheinlich den aktiven Audio-Session- und Ausgaberouten-Kontext. Ein Hintergrundprozess kann decodieren, aber keinen hörbaren Systemport erhalten.
2. Beim Spielstart oder Fokuswechsel kann der Hintergrundprozess suspendiert, gedrosselt oder vom Audio-Fokus getrennt werden.
3. Mikrofonfreigabe, Lautstärkeroute und Mixer-Registrierung können pro Prozess bzw. UI-Lifecycle gelten. Ohne diese Registrierung bleibt ein Medienpfad lokal, aber stumm.

Die Folgerung ist nicht, diese Schranken zu umgehen. Ein sicherer nächster Schritt wäre ein reiner Softwaretest mit den Zuständen „Decoder aktiv“, „Audio-Port erstellt“, „Frames geschrieben“, „Ausgabe-Callback bestätigt“ und „Fokuswechsel“, zunächst mit Mock-Backend. Er wird in Phase 5 nicht auf einer Konsole ausgeführt.

## Isolierter, rein lesender PoC-Plan

1. **Evidenz einfrieren:** `python3 tools/phase5_reference_evidence.py /pfad/zu/referenzcheckouts` gegen die vier genannten Commits ausführen. Der Test prüft nur die Forschungsbelege.
2. **Adaptermodell offline:** Auf dem Entwicklungsrechner eine feste `AKENOIdentity`-Fixture mit Name, Avatar-URI, Banner-URI, Presence und zwei Freunden durch denselben Serializer führen, den die vorhandene lokale IPC später konsumieren würde. Keine Zugangsdaten, Produktion oder Netzwerk.
3. **UI-Konzept:** Einen klar benannten „AKENO Social“-Einstiegspunkt in einer vorhandenen AKENO-nativen UI planen. Er ist kein Login-Screen und kein Ersatz für Game Base. Das Mock zeigt nur Identität und Friend-Summary.
4. **Stop-Kriterien:** Abbrechen bei Bedarf an NP-Dateien, Account-ID/Registry, ShellCore, ShellUI-Bundle-Patch, Sony-Endpunkten, Zertifikaten oder nativen Party-Audio-APIs.
5. **Spätere Hardwarephase getrennt:** Erst nach Code-Review, isoliertem Build, dokumentiertem Rückbau und Eigentümerfreigabe einen einmaligen flüchtigen UI-Render-Test erwägen. Keine Accounts, Saves, Trophäen, DNS, Server oder PSN berühren.

## Risikoregister

| Bereich | Risiko | Phase-5-Maßnahme |
| --- | --- | --- |
| Saves und Trophäen | Änderung lokaler Account-ID oder Slotzuordnung kann Datenkontext vermischen | strikt ausgeschlossen |
| bestehende Konten | NP-Dateien/Registry vermischen Identitätszustände | strikt ausgeschlossen |
| ShellCore/ShellUI | versionsfalsche Offsets oder unvollständige Sandbox-Views | keine Patches und kein FW-12.20-Versuch |
| Game Base | unbekannte Backend-/Auth-Abhängigkeiten | nicht umleiten und nicht emulieren |
| Audio | nicht nachgewiesener nativer Mixerzugriff | eigener AKENO-Audiopfad, zunächst nur Mock-Analyse |
| Netzwerk | Sony-Endpunkt- oder Zertifikatsumleitung | vollständig ausgeschlossen |

## Wiederholbare Prüfung

```sh
# lokale Referenzcheckouts: np-fake-signin/, offact/, ps5-theme/, etaHEN/
python3 tools/phase5_reference_evidence.py /pfad/zu/referenzcheckouts

# nur die nichtinvasiven Repository-Tests
python3 -m unittest discover -s tests -p 'test_phase5_*.py'
```

Ein Erfolgsfall bestätigt nur die markierten Quellbelege in den gepinnten Snapshots. Er ist kein Kompatibilitäts- oder Konsolentest und keine Freigabe für Game Base oder Party-Audio.
