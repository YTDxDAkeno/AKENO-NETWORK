# Testplan – AKENO-Seiten in der PS5-ShellUI (Plugin 1.10, Original-onionHEN v0.9.0)

Erst ausführen, wenn du die Installation freigegeben hast ([INSTALL-DE.md](INSTALL-DE.md)).
Jede Zeile mit **OK / Fehler / nicht getestet** notieren; bei Fehlern ein Foto und die Logzeilen.
Für Nachrichten- und Party-Tests hilft ein zweites Konto (Website oder Handy).

## A – Host und Start (ohne Spiel)

| # | Schritt | Erwartet |
| --- | --- | --- |
| A1 | Konsole komplett neu starten, Startreihenfolge aus INSTALL-DE §4 | – |
| A2 | `/data/OnionHEN/OnionHEN.log` lesen | `Toolbox online for SceShellUI pid=…` **genau einmal**; kein `Detour failed`, kein `relocation failed` |
| A3 | dieselbe Datei | `kstuff already running / mprotect OK -- skip launch` oder `kstuff disabled` (nie ein zweites kstuff) |
| A4 | dieselbe Datei | `[plugins] started AKEN00001 1.10`, danach `[akeno-ingame] connected to onionHEN` und `[akeno-ingame] daemon ready` |
| A5 | Einstellungen → OnionHEN-Toolbox → Plugins | „AKENO NETWORK (experimental)“, Version 1.10, „Läuft“ |

## S – Seiten und Aktionen (ohne Spiel)

| # | Schritt | Erwartet |
| --- | --- | --- |
| S1 | Toolbox → AKENO NETWORK | Zeilen mit echten Werten: `@name · Status`, `Friends · N online`, `Messages · N unread`, `Party · …`, `Requests & invitations` – Zahlen wie in AKENO Native bzw. auf der Website |
| S2 | Friends öffnen | Freunde mit Status/Spiel („Bob · Playing …“, „Dan · Offline · last seen …“); Online zuerst |
| S3 | Einen Freund öffnen | `@id · Status`; „Send … a message“; ggf. „Conversation“ und „Invite … to your party“ |
| S4 | „Send … a message“ drücken, mit der PS5-Tastatur „Test 1“ eingeben, bestätigen | Systembenachrichtigung „Sent to …“; das zweite Konto erhält „Test 1“ |
| S5 | Zurück → Messages | Unterhaltungen mit ungelesenen Zahlen und letzter Zeile |
| S6 | Eine Unterhaltung öffnen | bis zu 5 letzte Nachrichten, eigene als „You: …“, dann „Reply...“ |
| S7 | „Reply...“ → Text → bestätigen | „Sent to …“; Nachricht kommt beim zweiten Konto an |
| S8 | Vom zweiten Konto eine Nachricht senden; Unterhaltung **verlassen und neu öffnen** | neue Nachricht erscheint **ohne ShellUI-Neustart** (spätestens nach ~2–5 s) |
| S9 | „Mark as read“ | „Marked … as read“; auf der Website 0 ungelesen |
| S10 | Party (nicht in einer Party): „Create a party“ → Namen eingeben | „Party created …“; Party erscheint auf der Website |
| S11 | Party → „Invite friends“ → einen Freund | „Invited … to your party“; Einladung beim Freund |
| S12 | „Leave the party“ | Rückfrage, danach „You left the party.“ |
| S13 | Vom zweiten Konto in eine Party einladen; Requests & invitations → „Accept: …“ | „Invitation accepted.“; du bist in der Party |
| S14 | Freundschaftsanfrage von einem Testkonto; „Accept friend request“ bzw. „Decline“ | „Friend request accepted/declined.“ |
| S15 | Profil-Zeile → Status „Away“ | „Your status is now Away.“; Website zeigt Abwesend |
| S16 | **Navigation:** in eine Unterseite (z. B. einen Freund), zurück, zurück, dann „Refresh“ auf der Hauptseite | „Refresh“ reagiert weiter (prüft den onionHEN-Seitenstapel) |
| S17 | Diagnostics & help | Daemon-Zustand, `Native interface 3`, Serververbindung, letzte Aktualisierung |

## B – Im Spiel (Meilenstein B, praktische Form)

Ein ruhiges Einzelspieler-Spiel verwenden. Tastenkürzel vorher setzen (INSTALL-DE §6).

| # | Schritt | Erwartet |
| --- | --- | --- |
| B1 | Spiel starten | läuft |
| B2 | L2 + R3 gedrückt halten | Einstellungen öffnen sich auf der OnionHEN-Toolbox |
| B3 | Prüfen, ob das Spiel noch läuft (PS-Taste) | Spiel pausiert im Hintergrund, **nicht beendet** |
| B4 | AKENO NETWORK → Friends → Messages → eine Antwort senden | wie S2–S7, mit Spiel im Hintergrund |
| B5 | Einstellungen verlassen / zum Spiel wechseln | Spiel läuft weiter |
| B6 | Spielton und Steuerung | normal |

## R – Robustheit

| # | Schritt | Erwartet |
| --- | --- | --- |
| R1 | Daemon beenden (falls möglich) oder Konsole ohne Daemon starten; AKENO öffnen | Hauptseite: „The AKENO daemon is not running …“ + Hinweis + Refresh; keine leeren Felder |
| R2 | Daemon wieder starten; AKENO neu öffnen | innerhalb ~10 s wieder Daten, ohne ShellUI-Neustart |
| R3 | Internet/Server kurz trennen | Hauptseite: „AKENO server: connecting (live updates paused)“ |
| R4 | Ruhemodus, aufwachen, AKENO öffnen | Seiten kommen zurück (Plugin verbindet sich neu); im Log kein `Detour failed` |
| R5 | Toolbox → Plugins → AKENO „Läuft“ aus und an | Seite verschwindet und erscheint wieder |

## Nicht Teil dieses Tests

- Party-Sprache im Spiel (eigener Voice-Lab-Test, `docs/PS5_BACKGROUND_VOICE.md`).
- Kontrollzentrum / Game Base (nicht implementiert, siehe [DIAGNOSE-DE.md](DIAGNOSE-DE.md)).

## Rückmeldung

Bitte schicken: diese Tabelle ausgefüllt, Fotos von S1, S6, B3, die Zeilen aus
`/data/OnionHEN/OnionHEN.log` rund um den Start (A2–A4) und ggf. `/data/akeno/akeno.log`.
