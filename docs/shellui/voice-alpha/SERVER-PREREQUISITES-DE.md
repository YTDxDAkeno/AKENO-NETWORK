# AKENO Party-Voice Alpha – Voraussetzungen auf dem Server

**Kurz:** Dein Produktionsserver kann **noch keine Konsolen-Sprache**. Am 18.10.2026 antwortete er
auf `POST /api/v1/parties/:id/voice/device-token` mit **HTTP 404**. Ohne die Server-Seite kann die
PS5 der Sprach-Party nicht beitreten – der Sprachdienst zeigt dann
*„The AKENO server has no console voice yet (voice pass: HTTP 404)"*. **Ein PS5-Sprachtest gegen
den heutigen Produktionsserver kann daher nichts über den Ton aussagen.** Zuerst muss die
Server-Seite auf einer Testinstanz laufen, dann (mit deiner Freigabe) auf der Produktion.

**Nichts davon wurde ausgeführt.** Diese Arbeit hat keinen Server verändert.

## Was der Server braucht

| Teil | Wo im Repository | Zustand |
| --- | --- | --- |
| Sprachpass für Konsolen `POST /parties/:id/voice/device-token` (nur gekoppelte Geräte mit `parties:write`; kurzlebig, an einen Raum und das Konto gebunden; geprüft wie im Browser: Mitglied, keine Blockierung, Raumgröße) | `apps/server/src/modules/voice` | im Code dieser Branch-Linie, **nicht** in Produktion |
| Entzug: Entkoppeln, `parties:write` entfernen, Passwortänderung, Party verlassen/entfernt, Blockierung → Konsole fliegt sofort aus dem Raum (Gateway schließt mit 4003) | `apps/server/src/modules/voice`, `devices`, `auth` | im Code; Tests: `apps/server/test/voice.test.ts` (43/43 grün mit `devices.test.ts`) |
| Voice-Gateway (Opus über TLS-WebSocket ↔ LiveKit-Teilnehmer) | `apps/voice-gateway`, `infrastructure/docker/voice-gateway.Dockerfile` | im Code; Tests gegen echtes LiveKit 1.12 grün |
| Caddy-Route `wss://<Domain>/voice-gateway` → `voice-gateway:7890` | `infrastructure/caddy/Caddyfile` | im Code |
| Compose: Dienst `voice-gateway` im Profil `voice`, `VOICE_GATEWAY_URL` an die App | `compose.yaml`, `.env.example` | im Code |
| LiveKit (das bestehende Sprachsystem der Browser) | `compose.yaml` Profil `voice` | muss laufen (Browser-Sprache) |

**Keine Datenbank-Migration**, **kein neuer Firewall-Port** (Konsolen nutzen 443 über Caddy).
Browser- und Handy-Sprache bleiben unverändert: sie sprechen weiter direkt mit LiveKit; die PS5
erscheint dort als normaler Teilnehmer desselben Raums (`akeno-party-<Party-ID>`).

Token-Erneuerung: Der Sprachpass gilt 120 s (`VOICE_TOKEN_TTL_SECONDS`) und wird nur zum Beitreten
gebraucht; bei jeder Wiederverbindung holt der Sprachdienst über den Daemon einen neuen.

## Vorbedingung: welcher Code läuft in Produktion?

`docs/PROJECT_STATUS.md` nennt `af571eb` als Produktionsstand – ein Commit, der **nicht auf GitHub**
ist. Die Sprach-Server-Änderungen liegen auf der Native-Linie (`claude/native-voice` und Nachfolger,
PR #9 ff.). **Diesen Branch nicht ungeprüft auf die Produktion spielen.** Erst:

1. die VPS-eigenen Commits veröffentlichen (Ablauf in `docs/PROJECT_STATUS.md`, „Reconciling the
   VPS-only commits");
2. den Produktionsstand mit den Server-Dateien dieser Linie vergleichen
   (`git diff 34c2485..claude/native-voice -- apps/server packages/shared compose.yaml infrastructure .env.example`)
   und die Sprachänderungen **in einem geprüften PR** in die Produktionslinie mergen;
3. die volle CI auf dem Merge laufen lassen (`ci.yml`, `ps5-native.yml`, `ps5-voice.yml`).

## Schritt 1 – Testinstanz (Staging, wegwerfbar)

1. Test-VPS oder lokaler Rechner mit dem gemergten Stand, **frische** Datenbank (keine Kopie der
   Produktion), eigene Domain und Zertifikate.
2. `.env`: LiveKit wie in `docs/VOICE.md`, plus
   `VOICE_GATEWAY_URL=wss://<staging-domain>/voice-gateway`.
3. `docker compose --profile voice up -d --build`
4. Prüfen: `docker compose ps` (alles healthy); im Netz:
   `docker compose exec voice-gateway wget -qO- http://127.0.0.1:7890/healthz` → `{"ok":true,"sessions":0}`.
5. Zwei Browser in einer Party: Sprache geht weiter wie bisher.
6. Eine Test-Konsole (oder der PC-Test `clients/ps5-partyvoice/tests/e2e/run.mjs`) gegen Staging
   koppeln: `POST …/voice/device-token` gibt 200, der Sprachdienst meldet *Connected*.

Die CI macht dasselbe automatisch bei jedem Push (Job „End to end" in `ps5-voice.yml`: echter Server,
LiveKit, Gateway hinter TLS, Daemon, Sprachdienst, Plugin, ein PC-Teilnehmer).

## Schritt 2 – Produktion (nur nach Staging und deiner ausdrücklichen Freigabe)

1. **Backup:** `infrastructure/scripts/backup.sh` (Datenbank und Medien; Sprache speichert nichts).
2. Den gemergten Server-Stand ausrollen (keine Migration).
3. Gateway bauen und starten: `docker compose --profile voice up -d --build voice-gateway`,
   dann Caddy neu laden (`docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile`).
4. In `.env`: `VOICE_GATEWAY_URL=wss://<deine-domain>/voice-gateway`; nur den App-Container neu
   starten (`docker compose up -d app`).
5. Prüfen: Browser-Sprache mit zwei Browsern; `GET /healthz` des Gateways; im Gateway-Log
   `console voice session live` / `ended`, sobald die PS5 beitritt.
6. Firewall: **nichts Neues**. LiveKit-Ports bleiben wie sie sind.
7. Konsole: auf der Website unter Geräte prüfen, dass die PS5 die Berechtigung **„parties"** hat.

Alle Benutzerkonten, Nachrichten und Partys bleiben unverändert (keine Migration, kein Datenumbau).

## Zurück (Server)

| Schritt | Wirkung |
| --- | --- |
| `VOICE_GATEWAY_URL=` (leer) und App neu starten | Konsolen bekommen „Console voice is not set up"; Browser unbeeinflusst |
| `docker compose stop voice-gateway` | verbundene Konsolen fliegen aus der Sprache; sonst ändert sich nichts |
| vorherigen Server-Stand neu ausrollen | Konsolen-Route verschwindet (404); keine Daten zurückzumigrieren |

Der Gateway hält keine Daten; Stoppen verliert nichts.

## Sicherheits-Checkliste vor der Produktion

- [ ] Gateway nimmt nur mit dem LiveKit-Secret signierte Pässe für `akeno-party-*`-Räume mit
      `akeno.client=console-gateway` an (Tests: Browser-Pass, fremde Signatur, Müll werden abgelehnt).
- [ ] Anfragen mit `Origin`-Header (Browser) werden abgelehnt.
- [ ] Eine Sitzung pro Konto; Größen-, Raten- und Pufferlimits; Leerlauf-Timeouts.
- [ ] Pass ist an das Gerät gebunden; Entkoppeln / Berechtigung entfernen / Passwortänderung beenden die Sprache.
- [ ] Die Konsole verbindet sich nur mit `wss://` auf dem Host ihres eigenen AKENO-Servers.
- [ ] Logs: Benutzer-ID, Raum, Zähler – nie Pässe oder Audio. Nichts wird aufgezeichnet.
