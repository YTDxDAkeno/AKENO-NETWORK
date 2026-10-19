# Phase 4 – Hardware-Testplan: direkter Zugang, Spiel-Optionsmenü, Kontrollzentrum

> **Überholt:** Dieses Dokument gehört zum fehlgeschlagenen Phase-4-Host. Für den korrigierten Host gilt [../phase4-fix/INSTALL-DE.md](../phase4-fix/INSTALL-DE.md) mit dem Testplan [../phase4-fix/TEST-DE.md](../phase4-fix/TEST-DE.md).

Voraussetzung: Installation nach [INSTALL-DE.md](INSTALL-DE.md). Reihenfolge einhalten. Bei einem
Fehler: Ergebnis notieren, `/data/OnionHEN/OnionHEN.log` sichern, aufhören und
[ROLLBACK-DE.md](ROLLBACK-DE.md). Vor Tests mit Spiel **speichern**.

Bitte für jede Zeile melden: **OK / FEHLER / nicht getestet**, bei Fehler was zu sehen war (Foto)
und die Log-Zeilen.

## H – Der neue Host ersetzt das Original ohne Verlust

| # | Schritt | Erwartet |
| --- | --- | --- |
| H1 | Neustart, kstuff, ELF-Loader, Daemon, `OnionHEN-AKENO-direct.elf` einmal senden | Toolbox-Benachrichtigung wie beim Original; Log: `Toolbox online for SceShellUI pid=…` einmal, kein `Detour failed` |
| H2 | Einstellungen → OnionHEN-Toolbox öffnen, durch die Gruppen blättern | Toolbox wie beim Original |
| H3 | Toolbox → Plugins → AKENO NETWORK → AKENO NETWORK | AKENO-Hauptseite mit Profil, Freunden, Nachrichten, Party (wie v1.10) |
| H4 | Eine Nachricht senden (Messages → Unterhaltung → Reply…) | „Sent to …“, Nachricht kommt beim Empfänger an |
| H5 | Freundesliste, ein Freundesprofil, Party-Details öffnen | wie v1.10 |
| H6 | Ein homebrew-Payload/eine App starten, die du sonst nutzt (z. B. FTP) | wie beim Original |

## D – Direkter Zugang per Tastenkürzel (Meilenstein 4.1)

`[shortcuts] toolbox=l2_r3` und `toolbox_target=AKEN00001` gesetzt.

| # | Schritt | Erwartet |
| --- | --- | --- |
| D1 | Startbildschirm: L2 + R3 eine Sekunde halten | Einstellungen öffnen sich **direkt** mit „AKENO NETWORK“ (nicht die Toolbox). Log: `plugin page URI detected: AKEN00001`, `direct entry: AKEN00001 root page as the Settings root` |
| D2 | Darin Friends → ein Freund → zurück (○) → Messages → zurück | Navigation wie über die Toolbox; ○ auf der Hauptseite schließt die Einstellungen |
| D3 | **Spiel starten** (z. B. Atelier Yumia), im Spiel L2 + R3 halten | AKENO-Hauptseite, Spiel pausiert im Hintergrund |
| D4 | Im Spiel über AKENO eine **Nachricht senden** | „Sent to …“, Empfänger bekommt sie |
| D5 | ○ bis die Einstellungen zu sind | **dasselbe Spiel läuft weiter**, kein Neustart des Spiels |
| D6 | D3–D5 dreimal hintereinander | jedes Mal gleich, kein Absturz |
| D7 | Halten L2 + R3 im Spiel löst **keine** Spielaktion aus (Steuerung im Spiel kurz prüfen) | Das Kürzel blockiert die Tasten erst nach 1 s Halten, wie das Original |
| D8 | Plugin ausschalten (Toolbox → Plugins → AKENO → „Läuft“ aus), L2 + R3 halten | **Toolbox** öffnet sich (Rückfall), Log: `has no page; opening the Toolbox`. Danach Plugin wieder einschalten |
| D9 | Ruhemodus → aufwecken → L2 + R3 halten | AKENO öffnet sich oder (falls onionHEN neu injiziert) die Toolbox; kein `Detour failed`. Ergebnis + Log melden |

## O – „AKENO NETWORK“ im Spiel-Optionsmenü

`[game_menu] plugin_entry=AKEN00001` gesetzt.

| # | Schritt | Erwartet |
| --- | --- | --- |
| O1 | Startbildschirm: Spielkachel markieren → OPTIONS | Menü enthält „AKENO NETWORK“ (bei Spielen mit „Nach Update suchen“); alle Sony-Einträge sind noch da |
| O2 | „AKENO NETWORK“ wählen | Einstellungen öffnen sich direkt auf AKENO |
| O3 | Bei laufendem Spiel: PS-Taste → Spielkarte des laufenden Spiels → OPTIONS (falls angeboten) | Gibt es dort „AKENO NETWORK“? Foto. Auswählen → AKENO, danach ○ → zurück ins Spiel |
| O4 | `show_onionhen_options=true`: ist der onionHEN-Cheats-Eintrag weiterhin da | wie beim Original |
| O5 | Plugin ausschalten, OPTIONS erneut öffnen | Eintrag „AKENO NETWORK“ fehlt (kein toter Eintrag) |

## C – Kontrollzentrum (Meilenstein 4.2) {#kontrollzentrum}

Ein **eigener Eintrag im Kontrollzentrum** (PS-Taste-Leiste) ist **nicht gebaut**: onionHEN hat
dafür keinen Hook und kein bekanntes Muster für Firmware 12.20, und ungeprüfte Offsets werden nicht
ausgeliefert. Diese Tests sammeln die Fakten für den nächsten Schritt:

| # | Schritt | Melden |
| --- | --- | --- |
| C1 | Im Spiel PS-Taste: welche Karten/Kacheln zeigt das Kontrollzentrum? | Foto |
| C2 | Gibt es auf der Karte des laufenden Spiels ein „…“/OPTIONS-Menü? | ja/nein, Foto des Menüs |
| C3 | Wenn ja: steht dort „AKENO NETWORK“ (aus O)? | ja/nein |
| C4 | Funktioniert L2 + R3 auch, **während** das Kontrollzentrum offen ist? | ja/nein |
| C5 | Nach AKENO (über D oder O) mit der PS-Taste statt ○ zum Spiel zurück | Spiel läuft weiter? |
| C6 | 15 Minuten spielen mit 3× AKENO zwischendurch (Nachricht lesen/senden) | Absturz, Ruckler, Ton weg? |

Meilenstein **4.2** gilt erst als erreicht, wenn D3–D6, O1–O3 (oder C3) und C5–C6 auf deiner PS5
bestanden sind.

## Rückfall-Weg (immer verfügbar, ohne neuen Host)

Mit dem **Original**-onionHEN v0.9.0 und nur `toolbox=l2_r3`: im Spiel L2 + R3 halten → Toolbox →
Plugins → AKENO NETWORK → AKENO NETWORK. Das spart DebugMenu und „Launch Settings“, braucht kein
neues Binary (Test F0: bitte einmal ausprobieren und melden).
