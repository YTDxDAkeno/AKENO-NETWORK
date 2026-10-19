# Server-Update für PS5-Sprache – Ablauf und Rückweg

> **Nur nach deiner ausdrücklichen Freigabe ausführen.** Bis dahin ist nichts davon passiert.
> Vorher: die [Diagnose](VPS-DIAGNOSE-DE.md) – sie entscheidet, ob Weg A direkt geht.

Was sich ändert: die AKENO-App bekommt die Route für den Konsolen-Sprachpass, der neue Dienst
`voice-gateway` startet (im bestehenden `voice`-Profil), Caddy leitet `wss://<Domain>/voice-gateway`
dorthin. **Keine Datenbank-Migration, kein neuer Firewall-Port, PostgreSQL und LiveKit werden nicht
neu gestartet.** Browser- und Handy-Sprache bleiben, wie sie sind.

Dauer: etwa 15–30 Minuten (der Image-Build ist das Längste; die Seite läuft dabei weiter).
Unterbrechungen: ~10 s API (App-Neustart; Seite, Realtime und PS5-Daemon verbinden sich selbst neu),
~2 s HTTPS (Caddy-Neustart). **Mach das Update, wenn niemand im Sprachchat ist**: wer während des
Updates im Voice ist, wird beim ersten Abgleich einmalig getrennt und muss erneut „Join voice"
drücken (Sicherheitsregel: alte Pässe ohne versiegelte Berechtigung werden entfernt).

Voraussetzungen aus der Diagnose (sonst erst klären):

- Weg A: dein VPS-Commit ist auf GitHub enthalten, `git status` ist sauber;
- Browser-Sprache läuft (`livekit` in `docker compose ps`, `LIVEKIT_*` gesetzt);
- mindestens 2 GB frei auf der Platte, ~300 MB freier Arbeitsspeicher.

## 1. Backup (immer zuerst)

```sh
cd /opt/akeno
./infrastructure/scripts/backup.sh
ls -lt backups | head -3
```

Prüfen, dass das Backup wirklich lesbar ist (beide Befehle müssen ohne Fehler laufen; ersetze
`<stamp>` durch den Zeitstempel aus der Liste):

```sh
docker compose exec -T db pg_restore --list < backups/akeno-<stamp>.dump | grep -c 'TABLE DATA'   # Zahl > 20
gzip -t backups/akeno-media-<stamp>.tar.gz && echo "Medien-Archiv ok"
```

Dann vom PC aus kopieren (ein Backup nur auf dem VPS schützt nicht vor dem Verlust des VPS):

```powershell
scp "administrator@vps-7971.onecom-cloud.one:/opt/akeno/backups/akeno-*<stamp>*" .
```

Wiederherstellen würde `./infrastructure/scripts/restore.sh backups/akeno-<stamp>.dump backups/akeno-media-<stamp>.tar.gz`
(fragt nach, stoppt die App, spielt die Datenbank in einer Transaktion ein). Das ist nur für
Datenschäden gedacht – kein Schritt dieses Updates ändert die Datenbank. Im Staging wurde es geprobt.

## 2. Rückweg vorbereiten

```sh
docker tag akeno-network:latest akeno-network:pre-voice   # das jetzt laufende Image bleibt erhalten
cp -p .env .env.pre-voice                                  # die jetzige Konfiguration
git rev-parse HEAD > .pre-voice-commit                     # der jetzige Code-Stand
```

## 3. Code aktualisieren

```sh
git status --short --untracked-files=no      # muss leer sein
git fetch origin claude/gracious-galileo-c5dxv8
git merge --ff-only <geprüfter Commit>      # den genauen Commit nenne ich dir mit der Freigabe
```

`--ff-only` **bricht von selbst ab**, wenn dein VPS Commits hat, die nicht im geprüften Stand
enthalten sind – dann nichts erzwingen, sondern melden; die VPS-Arbeit wird zuerst gesichert.

Ohne GitHub-Zugang auf dem VPS (privates Repository): auf dem PC ein Bundle bauen und kopieren:

```powershell
git fetch origin
git bundle create akeno-voice.bundle <VPS-Commit>..origin/claude/gracious-galileo-c5dxv8
scp akeno-voice.bundle administrator@vps-7971.onecom-cloud.one:/opt/akeno/
```

```sh
git bundle verify akeno-voice.bundle
git fetch ./akeno-voice.bundle claude/gracious-galileo-c5dxv8
git merge --ff-only <geprüfter Commit>
```

## 4. Update ausführen

Jeder verändernde Schritt fragt vorher nach `YES`.

```sh
./infrastructure/scripts/voice-upgrade.sh preflight   # nur prüfen; meldet Blocker
./infrastructure/scripts/voice-upgrade.sh build       # baut App- und Gateway-Image; Seite läuft weiter
./infrastructure/scripts/voice-upgrade.sh apply       # Gateway starten, VOICE_GATEWAY_URL setzen,
                                                      # App und Caddy neu erstellen, danach prüfen
```

`apply` trägt in `.env` ein: `VOICE_GATEWAY_URL=wss://<deine Domain>/voice-gateway` (vorher wird
eine Kopie `.env.before-VOICE_GATEWAY_URL-<Zeit>` angelegt). Caddy wird bewusst **neu erstellt** und
nicht nur neu geladen: die Caddyfile ist als einzelne Datei eingebunden, und nach `git merge` sieht
ein `caddy reload` noch die alte Datei (im Staging nachgewiesen).

## 5. Prüfen

`apply` endet mit `verify`. Erwartet:

| Prüfung | Erwartet |
| --- | --- |
| `/health/ready` | 200 |
| Sprachpass-Route mit ungültigem Geräte-Token | **401** (404 = noch der alte Server) |
| `wss://<Domain>/voice-gateway` | **101** (WebSocket angenommen) |
| `/livekit/` | nicht 502 |
| Gateway `/healthz` | `{"ok":true,"sessions":0}` |
| Server-Fehler der letzten 10 Minuten | keine |

Dann von Hand:

1. Website: anmelden, Freunde, Nachrichten, eine Party öffnen.
2. **Browser-Sprache mit zwei Browsern** (z. B. PC + Handy) in derselben Party: hören sich beide?
3. Website → *Settings → Security → Paired devices*: deine PS5 hat die Berechtigung
   **„Create, join and leave parties and invite friends"** (`parties:write`; sonst bekommt die
   Konsole `403 DEVICE_SCOPE_DENIED` – dort die Berechtigungen der PS5 ändern).
4. Während des PS5-Tests mitlesen: `docker compose logs -f voice-gateway`
   (`console voice session connecting` → `live` → `ended`).

Später jederzeit erneut: `./infrastructure/scripts/voice-upgrade.sh verify` (ändert nichts).

## 6. Rückweg (falls etwas nicht stimmt)

```sh
./infrastructure/scripts/voice-upgrade.sh rollback 1   # Konsolen-Sprache aus (503), Browser unverändert
./infrastructure/scripts/voice-upgrade.sh rollback 2   # Gateway stoppen
./infrastructure/scripts/voice-upgrade.sh rollback 3   # kompletter alter Stand: Commit, Image, .env, Caddyfile
```

| Problem | Stufe |
| --- | --- |
| Website geht nicht / Fehler, Browser-Sprache kaputt | 3 |
| Gateway fehlerhaft, Speicher/CPU-Probleme | 2 |
| Konsolen-Berechtigung verhält sich falsch | 1 |

Stufe 3 setzt den Checkout „detached" auf den alten Commit (kein Branch wird umgeschrieben), taggt
`akeno-network:pre-voice` wieder als `latest`, stellt `.env.pre-voice` her, entfernt das Gateway und
erstellt App und Caddy neu. Die Datenbank bleibt unberührt; was nach dem Update geschrieben wurde,
bleibt erhalten (im Staging geprüft). Danach antwortet die Konsolen-Route wieder 404 und die Website
läuft wie vorher.

Nach einem Rückweg die Ausgabe von `docker compose ps` und `./infrastructure/scripts/voice-upgrade.sh verify`
(bzw. `curl -s -o /dev/null -w '%{http_code}\n' https://<Domain>/health/ready`) melden.
