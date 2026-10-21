# HTTP API

Reference for scripts and agents. Derived from `src/api.cpp`; the host tests exercise every route and every role.

## Connection

| Item | Value |
|---|---|
| Base URL | `http://<device>/`, normally `http://<subnet>.250/`; `http://192.168.4.1/` in fallback-AP mode |
| Protocol | HTTP, port 80, no TLS |
| Authentication | `X-API-Key: <admin token or guest password>` header on every `/api/*` request |
| Request bodies | `POST` endpoints read `application/x-www-form-urlencoded` (what `curl -d` and `URLSearchParams` send). JSON bodies are not supported |
| CORS | None. Browsers on other origins cannot call the API; servers and `curl` can |
| Concurrency | Single-threaded. Send requests one at a time, with 2-3 s timeouts |

The dashboard (`GET /`) is public and contains no secrets; it asks for the credential and sends it as a header.

## Roles

One header carries either credential; the device decides the role from the value.

| Role | Credential | Where it comes from | Can do |
|---|---|---|---|
| **admin** | Admin token | Flashed from `include/secrets.h`. Cannot be changed over the API | Everything |
| **guest** | Guest password | Set, changed and disabled by an admin | View status, switch relays (`/api/set`, `/api/all`) |

Endpoint access:

| Endpoint | Guest | Admin |
|---|---|---|
| `GET /api/status` | yes | yes |
| `/api/set`, `/api/all` | yes | yes |
| `/api/rename`, `/api/schedule` | 403 | yes |
| `GET /api/info` | 403 | yes |
| `/api/guest/*` | 403 | yes |
| `/api/wifi/*` | 403 | yes |
| `/api/update/*` | 403 | yes |

Guests see relay names, state and schedule times. They cannot change anything but relay state. Giving an
automation or agent the **guest password** limits what a mistake or a leaked credential can do.

## Authentication responses

| Status | Body | Meaning |
|---|---|---|
| `401` | `unauthorized` | Missing or wrong credential |
| `403` | `admin access required` | Valid guest credential on an admin endpoint |
| `429` | `too many failed attempts, try again later` | Guest login is paused (see below) |
| `503` | `device not provisioned` | No valid admin token stored on the device. All API calls fail closed |
| `404` | `not found` | Unknown path, or wrong method on a POST-only route |

**Guest lockout.** After 8 failed guest-password attempts, guest login is paused for 60 s; requests that carry
a wrong credential get `429`, and so does the correct guest password. The admin token is unaffected. Requests
with no header do not count as failures. The counter is global, not per client, so an attacker who can reach the
device can keep guests locked out; admin access still works. Choose a strong guest password.

The credential travels in clear over HTTP; it stops accidental, cross-site and casual access, not an attacker
who can sniff your network. See [SECURITY.md](SECURITY.md).

## Data model

Four relays, **0-indexed**. Relay 0 is IN1 (GPIO 27), 1 is IN2 (26), 2 is IN3 (14), 3 is IN4 (25).

- `state: true` means the relay is **commanded on**, regardless of the board's active-LOW wiring.
- Switch-**on** is staggered: relays physically close one at a time, 400 ms apart, so after `/api/all?state=1`
  the last relay closes about 1.2 s later. `/api/status` reports the commanded state immediately. Wait at least
  1.5 s to confirm a physical result.
- Switch-off is immediate.
- State, names, schedules, the saved WiFi network and the guest password persist across power loss.

## Parameter rules

| Parameter | Accepted | Rejected with `400` |
|---|---|---|
| `relay` | `0`-`3` (leading zero allowed) | anything else |
| `state`, `enabled` | `1`, `0`, `true`, `false`, `on`, `off` (case-insensitive) | anything else |
| `on`, `off` (times) | `HH:MM`, exactly 5 characters, `00:00`-`23:59` | `7:05`, `24:00`, `12:60` |
| `name` | 1-24 **bytes** after trimming, valid UTF-8, no control characters | empty, too long, invalid UTF-8, control characters |
| `ssid` | 1-32 bytes, valid UTF-8, no control characters | anything else |
| WiFi `password` | empty (open network) or 8-63 bytes, no control characters | 1-7 bytes, 64+ bytes |
| guest `password` | 8-64 bytes, valid UTF-8, no control characters, no leading or trailing space, not equal to the admin token | anything else |

Relay endpoints (`set`, `all`, `rename`, `schedule`) accept GET or POST with query-string parameters; the
custom header requirement protects them from cross-site request forgery. **Send secrets (`password`) with
`POST` in the form body**, never in a URL.

## Relay and schedule endpoints

### `GET /api/status`  (guest, admin)

```json
{
  "role": "admin",
  "relays": [
    { "name": "Relay 1", "state": false, "schedEnabled": false, "onTime": "07:00", "offTime": "22:00" }
  ]
}
```

Always four entries. `role` is the caller's role. Names are JSON-escaped.

### `GET /api/set?relay=<0-3>&state=<bool>`  (guest, admin)

`200 ok`. Errors (`400`): `missing relay`, `relay must be 0-3`, `missing state`,
`state must be 1, 0, true, false, on or off`.

### `GET /api/all?state=<bool>`  (guest, admin)

Sets all four relays. `200 ok`. Switch-on is staggered.

### `GET /api/rename?relay=<0-3>&name=<text>`  (admin)

URL-encode the name. `200 ok`. Errors: relay errors, `missing name`,
`name must be 1-24 bytes of valid UTF-8 without control characters`.

### `GET /api/schedule?relay=<0-3>&enabled=<bool>&on=<HH:MM>&off=<HH:MM>`  (admin)

**All four parameters are required every time**; to change one field, read `/api/status`, modify, and send all
of them back. `200 ok`. Errors: relay errors, `missing enabled`, `enabled must be ...`, `missing on or off`,
`on and off must be HH:MM, 00:00-23:59`. Schedule behaviour is described in
[ARCHITECTURE.md](ARCHITECTURE.md#scheduling).

### `GET /api/info`  (admin)

```json
{
  "firmware": "1.0.0", "uptimeSeconds": 4210, "freeHeap": 168000, "resetReason": "power_on", "relayCount": 4,
  "wifi": { "connected": true, "apActive": false, "ip": "192.168.1.250", "rssi": -58 },
  "time": { "synced": true, "local": "2026-01-05 14:03" }
}
```

`time.local` appears only when synced. Use `time.synced` to know whether schedules are live.

## Guest access endpoints  (admin)

| Endpoint | Method | Behaviour |
|---|---|---|
| `/api/guest` | GET | `{"enabled": true}`. The password itself is never returned |
| `/api/guest/password` | POST, form field `password` | Sets or replaces the guest password. `200 ok`. Errors: `missing password`, `password must be 8-64 bytes of valid UTF-8 without control characters or edge spaces`, `password must differ from the admin token` |
| `/api/guest/disable` | POST | Removes the guest password; guest login stops working. `200 ok` |

```bash
curl -s -H "X-API-Key: $ADMIN" -d "password=living-room-guest" "$BASE/api/guest/password"
```

Changing the guest password takes effect immediately; the old one stops working.

## WiFi endpoints  (admin)

The device can be pointed at a different network, or given its first network, entirely over the API. If it has
no network (or cannot reach it) it runs its fallback access point; join that, open `http://192.168.4.1/`, and
use these endpoints.

### `POST /api/wifi/scan` then `GET /api/wifi/scan`

Scanning is asynchronous. `POST` starts a scan (`200 ok`; `409 busy` while a network change is in progress;
starting again while one runs is harmless). `GET` returns:

```json
{ "state": "done", "networks": [ { "ssid": "Home", "rssi": -48, "secure": true, "channel": 11 } ] }
```

`state` is `idle`, `running` or `done`. Poll every 1-2 s until `done` (typically a few seconds). Networks are
sorted strongest first, deduplicated by name (strongest copy kept), capped at 20. Networks with empty or
non-UTF-8 names are omitted. Scanning while joined to the device's own access point can briefly disturb that
connection.

### `GET /api/wifi/status`

```json
{
  "configured": true, "ssid": "home", "connected": true, "ip": "192.168.1.250", "rssi": -55, "apActive": false,
  "change": { "state": "idle", "message": "" }
}
```

`change.state` is `idle`, `trying`, `succeeded` or `failed`; `message` explains a failure. The WiFi password is
never returned.

### `POST /api/wifi/connect`

Form fields `ssid` (required) and `password` (omit or leave empty for an open network). Returns `200 ok`
immediately; the switch happens afterwards. Errors: `missing ssid`, `ssid must be 1-32 bytes ...`,
`password must be empty (open network) or 8-63 bytes without control characters`, `409 busy`.

```bash
curl -s -H "X-API-Key: $ADMIN" -d "ssid=Home" -d "password=correct-horse" "$BASE/api/wifi/connect"
```

What happens:

1. The device leaves its current network and tries the new one, then takes the usual `.250` host address.
   The fallback access point is started during the attempt so there is a way back in.
2. **On success** the new credentials are saved, the access point stops, and the device is on the new network.
   Its address is now `<new-subnet>.250`, so your connection is gone: reconnect to the new network and use
   the new address.
3. **On failure** (wrong password, out of range, timeout) the new credentials are **not saved**, and the
   device reconnects to the previous network. `GET /api/wifi/status` then shows `change.state: "failed"`.

While switching (up to about a minute in the worst case) the web server stays responsive to clients on the
access point. A client on the old network will see the device disappear. Keep a way to reach the device on the
new network, and test with a network you can reach.

## Firmware update endpoints  (admin)

OTA is approval-gated and documented in [OTA.md](OTA.md). **An agent must not call `apply` without explicit
user confirmation: it reboots the device and every relay drops for several seconds.**

| Endpoint | Method | Behaviour |
|---|---|---|
| `/api/update/status` | GET | State, versions, progress |
| `/api/update/check` | POST | Starts a check. `200 ok`, or `409 busy` |
| `/api/update/apply` | POST | Installs the staged update. `200 ok`, or `409 no update available` |

```json
{
  "current": "1.0.0", "configured": true, "state": "available", "latest": "1.0.1",
  "progress": 0, "message": "", "pendingConfirm": false, "lastCheckSecondsAgo": 312
}
```

`state` is one of `idle`, `checking`, `up_to_date`, `available`, `downloading`, `verifying`, `rebooting`,
`error`. `lastCheckSecondsAgo` is `-1` until a check succeeds.

## Notes for agents

1. **Prefer the guest password for agents.** An agent that only toggles relays needs nothing more, and a leak
   or a mistake cannot rename switches, change schedules, repoint the WiFi or install firmware.
2. **Confirm before disruptive actions:** `apply`, `wifi/connect`, `guest/disable`, and `all?state=1` for heavy
   loads. `wifi/connect` can make the device unreachable from where you are.
3. **Do not assume schedules are running.** Check `/api/info` `time.synced`; say "configured", not "active".
4. **Verify after writes,** waiting about 1.5 s after any switch-on.
5. **Prefer indexes to names.** Names are user-editable and not unique.
6. **Back off on errors.** The device is briefly unreachable during boot and while it switches networks or runs an
   update check; retry with increasing delay. On `429`, wait a minute.
7. **Treat both credentials as secrets.** Do not log them or put them in URLs.

## Examples

```bash
BASE=http://192.168.1.250
ADMIN=...        # admin token
GUEST=...        # guest password

curl -s -H "X-API-Key: $GUEST" "$BASE/api/status"
curl -s -H "X-API-Key: $GUEST" "$BASE/api/set?relay=0&state=on"
curl -s -H "X-API-Key: $ADMIN" "$BASE/api/rename?relay=3&name=Water%20Pump"
curl -s -H "X-API-Key: $ADMIN" "$BASE/api/schedule?relay=1&enabled=1&on=18:30&off=23:00"
curl -s -H "X-API-Key: $ADMIN" -X POST "$BASE/api/wifi/scan"; sleep 4
curl -s -H "X-API-Key: $ADMIN" "$BASE/api/wifi/scan"
curl -s -H "X-API-Key: $ADMIN" -d "ssid=Home" -d "password=correct-horse" "$BASE/api/wifi/connect"
curl -s -H "X-API-Key: $ADMIN" -d "password=living-room-guest" "$BASE/api/guest/password"
```

## Tool definitions (function calling)

These are safe to give an agent that holds the **guest password**; the `relay_set_*` and `relay_rename`
tools need the admin token.

```json
[
  {
    "name": "relay_get_status",
    "description": "Read state, names and schedules of all 4 relays (0-indexed).",
    "input_schema": { "type": "object", "properties": {}, "required": [] }
  },
  {
    "name": "relay_set",
    "description": "Turn one relay on or off. Switch-on is staggered, so wait about 1.5 s before verifying.",
    "input_schema": {
      "type": "object",
      "properties": {
        "relay": { "type": "integer", "minimum": 0, "maximum": 3 },
        "on": { "type": "boolean" }
      },
      "required": ["relay", "on"]
    }
  },
  {
    "name": "relay_set_all",
    "description": "Set all 4 relays. Confirm with the user before turning everything on.",
    "input_schema": {
      "type": "object",
      "properties": { "on": { "type": "boolean" } },
      "required": ["on"]
    }
  },
  {
    "name": "relay_get_info",
    "description": "ADMIN ONLY. Firmware version, uptime, WiFi state, and whether the clock is synced (schedules only run when time.synced is true).",
    "input_schema": { "type": "object", "properties": {}, "required": [] }
  },
  {
    "name": "relay_rename",
    "description": "ADMIN ONLY. Rename a relay. 1-24 bytes, valid UTF-8, no control characters.",
    "input_schema": {
      "type": "object",
      "properties": {
        "relay": { "type": "integer", "minimum": 0, "maximum": 3 },
        "name": { "type": "string", "minLength": 1, "maxLength": 24 }
      },
      "required": ["relay", "name"]
    }
  },
  {
    "name": "relay_set_schedule",
    "description": "ADMIN ONLY. Set a daily on/off schedule (device local time, every day). All fields required; read the current values first to change one. Runs only when the device clock is synced.",
    "input_schema": {
      "type": "object",
      "properties": {
        "relay": { "type": "integer", "minimum": 0, "maximum": 3 },
        "enabled": { "type": "boolean" },
        "on": { "type": "string", "pattern": "^([01][0-9]|2[0-3]):[0-5][0-9]$" },
        "off": { "type": "string", "pattern": "^([01][0-9]|2[0-3]):[0-5][0-9]$" }
      },
      "required": ["relay", "enabled", "on", "off"]
    }
  }
]
```

Mapping: `relay_set` is `GET /api/set?relay={relay}&state={on ? 1 : 0}`; `relay_set_all` is
`GET /api/all?state={on ? 1 : 0}`; `relay_set_schedule` is
`GET /api/schedule?relay=..&enabled={1|0}&on=..&off=..`. Every call carries the `X-API-Key` header. The WiFi,
guest-password and update endpoints are deliberately not exposed as agent tools.
