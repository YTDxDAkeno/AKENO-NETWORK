# Installing the AKENO device client

The client in `clients/ps5` currently builds and runs on **Linux** (and WSL on Windows). It is the
tested reference for the console client; there is **no console build yet** (see
[FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md)).

## Server side (once)

Deploy the server revision that contains device pairing; its migration (`0001_devices.sql`) only
adds two tables. Take a backup first (`infrastructure/scripts/backup.sh`), then follow your usual
deployment ([VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md)). Optional settings in `.env`:

```
DEVICE_TOKEN_TTL_DAYS=180
DEVICE_TOKEN_IDLE_DAYS=30
```

## Build the client (Linux / WSL)

```bash
sudo apt-get install -y build-essential cmake libssl-dev
cmake -S clients/ps5 -B clients/ps5/build -DCMAKE_BUILD_TYPE=Release
cmake --build clients/ps5/build -j
clients/ps5/build/akeno_unit_tests          # 243 checks, must print "0 failures"
```

## Pair and use it

```bash
export AKENO_SERVER=https://85.190.112.10.sslip.io
./clients/ps5/build/akeno-cli --name "My test client" pair
# Open the printed URL on your phone or PC, sign in, enter the code, choose permissions.

./clients/ps5/build/akeno-cli friends
./clients/ps5/build/akeno-cli send <onlineId> "Hello from the device client"
./clients/ps5/build/akeno-cli invitations
./clients/ps5/build/akeno-cli listen          # live events as JSON lines; Ctrl+C to stop
./clients/ps5/build/akeno-cli caps            # what is enabled on this system and why
./clients/ps5/build/akeno-cli logout          # unpair and delete the local credential
```

The credential is stored at `~/.config/akeno/device.json` (mode 0600, bound to that server).
Unpair devices any time in **Settings → Account & security → Paired devices**.

Exit codes: `0` ok, `2` usage, `3` not paired, `4` unpaired by the server, `5` permission not
granted, `6` network/TLS/timeout, `7` other API or protocol error, `8` pairing denied or
expired, `9` reconnect attempts exhausted.

## Run the full client test suite

```bash
pnpm test:client   # builds server + sanitizer client, runs unit and end-to-end tests
```

Needs PostgreSQL with a role allowed to create databases (`TEST_DATABASE_ADMIN_URL`), and
`openssl` on the PATH. It never touches existing databases.
