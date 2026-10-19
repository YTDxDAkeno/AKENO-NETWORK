# VPS-Diagnose vor dem Sprach-Update (nur lesen)

Diese Schritte **ändern nichts** auf deinem Server: kein `git fetch`, kein Neustart, keine
Konfigurationsänderung, kein Datenbankzugriff, keine Firewall. Sie beantworten, welcher Code
wirklich läuft, ob es Commits nur auf dem VPS gibt und ob Browser-Sprache (LiveKit) schon aktiv
ist. Danach entscheide ich, welcher Update-Weg sicher ist
([SERVER_VOICE_UPGRADE.md](../SERVER_VOICE_UPGRADE.md), Abschnitt 4).

**Schicke niemals Passwörter, private SSH-Schlüssel, Datenbank-Zugangsdaten, API-Secrets oder den
Inhalt deiner `.env` in den Chat.** Das Skript zeigt aus `.env` nur die Namen der Einträge und die
Werte einiger öffentlicher Einstellungen (Domain, öffentliche URLs, LiveKit-IP).

## Variante 1: das Diagnose-Skript (empfohlen)

Auf dem VPS (per SSH, wie gewohnt):

```sh
cd /opt/akeno
curl -fsSL -o ~/vps-voice-diagnostics.sh \
  https://raw.githubusercontent.com/YTDxDAkeno/AKENO-NETWORK/claude/gracious-galileo-c5dxv8/infrastructure/scripts/vps-voice-diagnostics.sh
less ~/vps-voice-diagnostics.sh          # lies es durch, bevor du es startest (q beendet)
bash ~/vps-voice-diagnostics.sh > ~/akeno-voice-diagnose.txt 2>&1
less ~/akeno-voice-diagnose.txt          # prüfe den Bericht selbst
```

Falls der VPS GitHub nicht erreicht: die Datei auf dem PC herunterladen und mit
`scp vps-voice-diagnostics.sh administrator@vps-7971.onecom-cloud.one:~/` kopieren.

Das Skript legt nur die eine Berichtsdatei in deinem Home-Verzeichnis an. Prüfe sie, bevor du sie
teilst, und schicke dann den Inhalt (oder die Abschnitte 1, 2, 4, 5 und 6).

## Variante 2: dieselben Befehle einzeln

```sh
cd /opt/akeno
git rev-parse HEAD
git log -1 --format='%H %ci %s'
git branch -vv
git log --oneline -25
git log --oneline HEAD --not --remotes      # Commits, die keinem bekannten GitHub-Branch gehören
git status --short
git diff --stat
grep -c 'voice/device-token' apps/server/src/modules/voice/routes.ts   # 0 oder Fehler = Route fehlt
ls apps/voice-gateway 2>/dev/null || echo "kein Gateway im Checkout"
grep -nE 'handle(_path)? ' infrastructure/caddy/Caddyfile
cut -d= -f1 .env                           # nur die NAMEN der Einträge
grep -E '^(AKENO_DOMAIN|LIVEKIT_URL|LIVEKIT_NODE_IP|LIVEKIT_USE_EXTERNAL_IP|VOICE_GATEWAY_URL)=' .env
docker compose --profile https --profile voice ps
docker image ls akeno-network
nproc; free -m; df -h / /var/lib/docker
ip -4 -o addr show scope global
ls -lt backups | head -5
```

Und die zwei harmlosen Proben von außen (keine Daten, ein ungültiges Test-Token):

```sh
D=85.190.112.10.sslip.io     # deine AKENO_DOMAIN
curl -s -X POST -H 'Authorization: Bearer akd_invalid-diagnostics-probe' -H 'Content-Type: application/json' -d '{}' \
  https://$D/api/v1/parties/00000000-0000-4000-8000-000000000000/voice/device-token; echo
curl -s -o /dev/null -w '%{http_code}\n' https://$D/livekit/
```

## Was die Antworten bedeuten

| Ergebnis | Bedeutung |
| --- | --- |
| Probe antwortet `{"error":{"code":"NOT_FOUND",…}}` | alter Server: die Konsolen-Route fehlt (genau dein 404) |
| Probe antwortet `{"error":{"code":"UNAUTHENTICATED",…}}` | die Route existiert bereits |
| `git log … --not --remotes` listet Commits | es gibt Arbeit nur auf dem VPS → sie wird zuerst gesichert und veröffentlicht, nie überschrieben |
| `git status` zeigt geänderte Dateien | lokale Änderungen → werden vor dem Update geklärt |
| `livekit` läuft in `docker compose ps` | Browser-Sprache ist eingerichtet (Voraussetzung für die PS5) |
| `livekit` fehlt / `LIVEKIT_*` leer | Browser-Sprache muss zuerst eingerichtet werden (eigene Freigabe, Firewall 7881/tcp + 7882/udp) |
| `LIVEKIT_NODE_IP` steht in der `ip -4`-Liste | das Gateway erreicht LiveKit auf dem Server selbst (wie im Staging getestet) |

Es wird **nichts** veröffentlicht, gepusht oder geändert, bevor du es ausdrücklich freigibst.
