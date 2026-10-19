# Device pairing

**Status: implemented and tested** (server, web UI, C client). See [TESTING.md](TESTING.md).

Devices such as the AKENO console client cannot safely handle an account password, so they are
**paired** instead. The flow follows OAuth 2.0 Device Authorization (RFC 8628) semantics, using
AKENO's own error envelope.

```
Device (no account yet)                 AKENO server                     Website (signed in)
  |  POST /devices/pairings  ----------->|                                  |
  |<--- deviceCode, userCode "ABCD-EFGH",|                                  |
  |     verificationUri (/pair)          |                                  |
  |  shows code + URL                    |                                  |
  |                                      |<-- POST /devices/pairings/lookup |  user types the code
  |                                      |<-- POST /devices/pairings/approve|  user picks permissions
  |  POST /devices/pairings/token  ----->|  (every >= 5 s)                  |
  |<--- accessToken (once) --------------|                                  |
  |  Authorization: Bearer akd_...  ---->|  API + WSS, limited to scopes    |
```

## Endpoints (`/api/v1`)

| Endpoint | Caller | Notes |
| --- | --- | --- |
| `POST /devices/pairings` | device, no auth | `{ deviceName, platform?, clientVersion?, scopes? }` → `201 { deviceCode, userCode, verificationUri, verificationUriComplete, expiresIn: 600, interval: 5 }`. 10 per 15 min per IP; at most 500 outstanding requests server-wide. |
| `POST /devices/pairings/token` | device, no auth | `{ deviceCode }` → `200 DeviceTokenResponse` exactly once. Otherwise `400` with `AUTHORIZATION_PENDING`, `SLOW_DOWN` (polled faster than `interval`; add 5 s), `ACCESS_DENIED`, `EXPIRED_TOKEN` or `PAIRING_INVALID` (unknown, already redeemed, or the account is no longer active). 30 per minute per IP. |
| `POST /devices/pairings/lookup` | website session + CSRF | `{ userCode }` → device name, platform, version, requested scopes, request IP. 10/min per user. |
| `POST /devices/pairings/approve` | website session + CSRF | `{ userCode, scopes? }` → `204`. Granted scopes must be a subset of the requested ones. |
| `POST /devices/pairings/deny` | website session + CSRF | `{ userCode }` → `204`. |
| `GET /devices` | website session | Paired devices of the account. |
| `DELETE /devices/:id` | website session + CSRF | Unpairs a device; its live sockets close with `4401`. |
| `GET /devices/current` | device token | The device, its scopes, the account, `apiVersion`, `realtimeProtocolVersion`, `serverTime`. |
| `DELETE /devices/current` | device token | Device sign-out. |

User codes are 8 characters from a 31-character alphabet without look-alikes, shown as
`XXXX-XXXX`; case, spaces and hyphens are ignored when typed. The web page is `/pair` and accepts
`?code=` for QR codes.

## Scopes

| Scope | Allows |
| --- | --- |
| `profile:read` | `GET /users/me`, `/users/search`, `/users/:handle` |
| `presence:write` | `PUT /presence` |
| `friends:read` / `friends:write` | Friends list and requests / send, accept, decline, cancel requests |
| `messages:read` / `messages:write` | Direct, group and party conversations and messages / send, mark read, typing |
| `parties:read` / `parties:write` | Current and joinable parties / create, join, leave, invite; with `parties:write` also the console voice pass (`POST /parties/:id/voice/device-token`) |
| `sessions:read` / `sessions:write` | Game sessions / create, update, join, leave, close, invite |
| `invitations:read` / `invitations:write` | Invitations and notifications / accept, decline, mark read |
| `realtime` | The `/ws` WebSocket (the device then counts as online, like a browser tab) |

The table lives in `apps/server/src/modules/devices/scopes.ts` and is **default deny**: any route
not listed is refused with `403 DEVICE_SCOPE_DENIED`. Devices can never reach authentication,
password, account deletion, profile editing, privacy, block list, device management, pairing
approval or administration routes, and device tokens never carry the admin role, even for
administrator accounts. A test checks that every listed route exists and that no sensitive route
family is listed.

## Security properties

- **No password on the device.** The user signs in on a trusted browser only.
- **Hashes only.** Device codes, user codes and tokens are stored as SHA-256 hashes; raw values
  exist only in responses and on the device.
- **One-time redemption.** A conditional update in the issuing transaction guarantees a device
  code yields at most one token; replays return `PAIRING_INVALID`.
- **Expiry and rate limits.** Pairing requests live 10 minutes; tokens expire after
  `DEVICE_TOKEN_TTL_DAYS` (default 180) or `DEVICE_TOKEN_IDLE_DAYS` without use (default 30).
  At most 10 devices per account (least recently used is unpaired).
- **Revocation.** Unpairing on the website, device sign-out, a password change or CLI reset,
  suspension and account deletion all delete device credentials; live sockets close with `4401`.
- **CSRF.** Bearer requests ignore cookies entirely, so a browser cannot be tricked into using a
  device token; they therefore need no Origin or CSRF token. `Basic` authorization headers (for
  example from a protecting reverse proxy) do not affect cookie sessions.
- **Phishing warning.** The approval page tells users to enter only codes shown on a device they
  are holding, and shows the requesting device's name, platform and IP before approval.
- **Audit log.** `device.pairing_approved`, `device.pairing_denied`, `device.paired`,
  `device.revoked`.

## Data model

Migration `0001_devices.sql` only **adds** two tables (`device_pairings`, `device_credentials`);
no existing table changes. It was verified to apply on top of a database created by the previous
revision with existing users preserved.
