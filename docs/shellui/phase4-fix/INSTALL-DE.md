# AKENO NETWORK Phase 4.1 – korrigierter Schnellzugang aus dem Spiel (Installation)

> **EXPERIMENTELL – auf keiner PS5 getestet, nicht stabil.** Gebaut und auf dem PC getestet, mehr
> nicht. Nichts wird automatisch auf die Konsole übertragen. Teste nur in der Reihenfolge von
> [TEST-DE.md](TEST-DE.md) und brich beim ersten Problem ab. Dein funktionierender Weg (Original-
> onionHEN v0.9.0 + `toolbox=l2_r3` → Toolbox → Plugins → AKENO NETWORK) bleibt jederzeit der
> Rückweg: [ROLLBACK-DE.md](ROLLBACK-DE.md).
>
> **Den alten Phase-4-Host `OnionHEN-AKENO-direct.elf` nicht mehr verwenden.** Er hat am 19.10.2026
> auf deiner PS5 die Controller-Eingabe blockiert. Dieses Paket enthält ihn nicht.

## Was im Paket ist

| Datei | Status | Was tun |
| --- | --- | --- |
| `host/OnionHEN-AKENO-direct-fixed.elf` | **neu, experimentell**: Original-onionHEN v0.9.0 + Phase-4-Patch + Phase-4.1-Korrektur (`source/`) | nur für die Tests statt `OnionHEN.elf` senden |
| `host/config-akeno-direct.ini` | die Zeilen für `/data/OnionHEN/config.ini` | erst ab Test H5 eintragen |
| `plugin/AKEN00001.elf` | **unverändert** v1.10, byte-gleich mit deiner Datei (SHA-256 `201d2c88…`) | nichts tun, deine Datei behalten |
| AKENO-Daemon | **unverändert**, nicht im Paket | wie bisher |
| `docs/` | diese Anleitung, [TEST-DE.md](TEST-DE.md), [ROLLBACK-DE.md](ROLLBACK-DE.md), technische Beschreibung, Prüfprotokolle (`docs/gates/`) | lesen |
| `source/` | die beiden Patches (Quelltext, GPL-3.0) | nur zur Information |
| `licenses/`, `BUILDINFO.txt`, `SHA256SUMS` | Lizenzen, Build-Daten, Prüfsummen | Schritt 1 |

## Was sich gegenüber dem fehlgeschlagenen Phase-4-Host ändert

**1. Standard ist jetzt der Link-Modus (`toolbox_target_mode=link`).**
L2 + R3 öffnet wieder die **normale Toolbox** – genau der Weg, der bei dir funktioniert. Neu ist nur:
**„AKENO NETWORK“ steht in der Toolbox ganz oben und ist schon ausgewählt.** Also:
L2 + R3 halten → **✕** → AKENO. Zurück: ○ → Toolbox → ○ → Spiel.
Die Einstellungs-Startseite wird dabei **nicht** ersetzt; AKENO öffnet sich als normale Unterseite,
so wie heute über Toolbox → Plugins. Das ist ein Tastendruck mehr als „direkt“, nutzt aber nur
Abläufe, die auf deiner PS5 schon funktionieren.

**2. Der Direkt-Modus (`toolbox_target_mode=root`) ist nur noch ein Test-Schalter.**
Er öffnet AKENO ohne Zwischenschritt wie in Phase 4, aber korrigiert:

- Die AKENO-Startseite trägt jetzt die Kennung `id_debug_settings`, die jede Einstellungs-Wurzel
  braucht. onionHEN macht das für die Toolbox und für sein eigenes Cheats-Kürzel genauso; Phase 4
  hatte hier eine andere Kennung verwendet (der wahrscheinlichste Fehler, siehe
  [SHELLUI_DIRECT_ENTRY.md](../../SHELLUI_DIRECT_ENTRY.md#phase-41)).
- Doppelte, veraltete oder unterbrochene Anfragen werden verworfen; ohne laufendes Plugin öffnet
  sich die Toolbox.
- Startet die ShellUI während einer Direkt-Sitzung neu, schaltet sich der Direkt-Modus bis zum
  nächsten Konsolen-Neustart **selbst ab** (L2 + R3 öffnet dann die Toolbox).
- Jeder Schritt schreibt eine Zeile `AKENO_DIRECT: …` ins Log, damit wir sehen, wo es hakt.

**3. Unverändert:** Plugin, Daemon, Server, Konto, Freunde, Nachrichten, Party, Anmeldung, kstuff.
Kein PSN, keine Sony-Systemdatei. Ohne die neuen Zeilen verhält sich der Host wie das Original.

## 1. Paket prüfen (am PC)

Im entpackten Ordner `AKENO-NETWORK-PS5-PHASE4-FIX`:

```sh
sha256sum -c SHA256SUMS
```

Jede Zeile muss `OK` zeigen. Windows (PowerShell):
`Get-FileHash host\OnionHEN-AKENO-direct-fixed.elf -Algorithm SHA256` und mit `BUILDINFO.txt`
vergleichen; ebenso `plugin\AKEN00001.elf` → muss mit `201d2c88` beginnen.

## 2. Sichern (vor dem ersten Test)

Per FTP auf den PC kopieren:

1. `/data/OnionHEN/config.ini` → z. B. `config-vor-4.1.ini`.
2. Dein originales `OnionHEN.elf` v0.9.0 (SHA-256 `1f1f7c88…7c212`) liegt sicher auf dem PC.
3. `/data/OnionHEN/OnionHEN.log` und `/data/OnionHEN/OnionHEN_util_daemon.log` → auf den PC kopieren,
   danach **auf der Konsole umbenennen** (z. B. `OnionHEN-alt.log`). So beginnt der Test mit einem
   frischen Log und alte Starts werden nicht verwechselt.
4. Vor jedem Test **mit Spiel**: im Spiel speichern (und am besten Cloud/USB-Sicherung).

## 3. Konsole komplett neu starten

**Neustart, nicht Ruhemodus.** Pro Start nur **ein** onionHEN. Wenn heute schon ein onionHEN lief:
erst neu starten.

## 4. config.ini für die Tests H1 bis H4

Für H1–H4 läuft der neue Host **mit Original-Verhalten**. In `/data/OnionHEN/config.ini`:

```ini
[game_menu]
show_onionhen_options=true
plugin_entry=none

[shortcuts]
cheats_menu=off
toolbox=l2_r3
toolbox_target=toolbox
```

Stehen dort noch Zeilen aus dem Phase-4-Test (`toolbox_target=AKEN00001`, `plugin_entry=AKEN00001`),
genau so wie oben setzen.

## 5. Starten

Wie gewohnt, nur mit dem neuen Host:

1. Exploit → **dein kstuff** → ELF-Loader (Port 9021).
2. AKENO-Daemon an Port 9021 senden, Benachrichtigung abwarten.
3. **`host/OnionHEN-AKENO-direct-fixed.elf` genau einmal** an Port 9021 senden. Auf die
   Toolbox-Benachrichtigung warten. **Nie** ein zweites onionHEN im selben Start.
4. Das Plugin AKEN00001 startet wie bisher (Autostart), sonst Einstellungen → OnionHEN-Toolbox →
   Plugins → AKENO NETWORK → „Läuft“.

## 6. Später: Link-Modus (H5) und Direkt-Modus (H6–H8)

Nur wenn die vorherigen Tests bestanden sind. **Nach jeder Änderung der config.ini die Konsole neu
starten** und wie in Schritt 5 laden.

Link-Modus (ab H5):

```ini
[shortcuts]
toolbox=l2_r3
toolbox_target=AKEN00001
toolbox_target_mode=link
```

Direkt-Modus (nur H6–H8):

```ini
[shortcuts]
toolbox=l2_r3
toolbox_target=AKEN00001
toolbox_target_mode=root
```

`plugin_entry` bleibt in dieser Testrunde `none`.

## 7. Log prüfen

`/data/OnionHEN/OnionHEN.log` per FTP:

- `Toolbox online for SceShellUI pid=…` genau **einmal** pro Start.
- Mit `toolbox_target=AKEN00001`: beim Start `AKENO_DIRECT: shortcut target AKEN00001, link mode`
  (bzw. `root mode`).
- Welche `AKENO_DIRECT`-Zeilen bei welchem Test kommen sollen, steht in [TEST-DE.md](TEST-DE.md).
- **Abbruch-Zeilen** (sofort aufhören, neu starten, [ROLLBACK-DE.md](ROLLBACK-DE.md)):
  `Detour failed`, `relocation failed`, `Toolbox already active`, ein zweites
  `Toolbox online for SceShellUI` oder `Activating toolbox...` im selben Start,
  `[plugin-bridge] ShellUI disconnected` während eines Tests,
  `AKENO_DIRECT: SceShellUI pid=… starts while a direct-entry session … was open`,
  `AKENO_DIRECT: root mode off until the console restarts`.

## Risiken (ehrlich)

- Der Host ist **ein anderes Binary** als dein Original (anderer Compiler: Ubuntu clang 18.1.3
  statt Homebrew clang 18.1.8). Darum prüfen H1–H4 zuerst den Host **ohne** neue Funktion.
- Die Ursache des Phase-4-Fehlers ist **nicht bewiesen**, nur eingegrenzt. Der Link-Modus vermeidet
  den verdächtigen Ablauf ganz; der Direkt-Modus ist ein Versuch, den nur deine Konsole bestätigen
  oder widerlegen kann.
- Ein ShellUI-Absturz oder eine Eingabesperre wie in Phase 4 ist im Direkt-Modus weiterhin möglich.
  Darum zuerst auf dem Startbildschirm (H6), erst danach im Spiel (H8), und **vorher speichern**.
- onionHEN selbst (Original wie Patch) mountet `/system` und `/system_ex` neu, legt
  `/system_ex/vsh_asset/onionhen.png` an und löscht eine heruntergeladene Systemaktualisierung
  (`/update/PS5UPDATE.PUP`). Das ist unverändertes Original-Verhalten.
