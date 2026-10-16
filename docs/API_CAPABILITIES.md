# HTTP API

Reference for scripts and agents. Derived from `src/api.cpp`; the host tests exercise every route.

## Connection

| Item | Value |
|---|---|
| Base URL | `http://<device>/`, normally `http://<subnet>.250/`; `http://192.168.4.1/` in fallback-AP mode |
| Protocol | HTTP, port 80, no TLS |
| Authentication | `X-API-Key: <token>` header on every `/api/*` request |
| CORS | None. Browsers on other origins cannot call the API; servers and `curl` can |
| Concurrency | Single-threaded. Send requests one at a time, with 2-3 s timeouts |

The dashboard (`GET /`) is public and contains no secrets; it asks for the token and sends it as a header.

## Authentication and failure modes

| Status | Body | Meaning |
|---|---|---|
| `401` | `unauthorized` | Missing or wrong token |
| `503` | `device not provisioned` | No valid token stored on the device (shorter than 16 characters or a placeholder). All API calls fail closed |
| `404` | `not found` | Unknown path, or wrong method on a POST-only route |

The token is sent in clear over HTTP; it stops accidental, cross-site and casual access, not an attacker who can
sniff your network. See [SECURITY.md](SECURITY.md).

## Data model

Four relays, **0-indexed**. Relay 0 is IN1 (GPIO 27), 1 is IN2 (26), 2 is IN3 (14), 3 is IN4 (25).

- `state: true` means the relay is **commanded on**, regardless of the board's active-LOW wiring.
- Switch-**on** is staggered: relays physically close one at a time, 400 ms apart, so after `/api/all?state=1`
  the last relay closes about 1.2 s later. `/api/status` reports the commanded state immediately. If you need
  to confirm the physical result, wait at least 1.5 s.
- Switch-off is immediate.
- State, names and schedules persist across power loss.

## Parameter rules

| Parameter | Accepted | Rejected with `400` |
|---|---|---|
| `relay` | `0`, `1`, `2`, `3` (leading zero allowed) | anything else, including `abc`, `-1`, `4`, empty |
| `state`, `enabled` | `1`, `0`, `true`, `false`, `on`, `off` (case-insensitive) | anything else |
| `on`, `off` (times) | `HH:MM`, exactly 5 characters, `00:00`-`23:59` | `7:05`, `24:00`, `12:60`, seconds |
| `name` | 1-24 **bytes** after trimming, valid UTF-8, no control characters | empty, too long, invalid UTF-8, control characters |

All state-changing relay endpoints accept GET or POST with query-string parameters. They are safe from
cross-site request forgery because every call needs the custom `X-API-Key` header, which a browser will not send
cross-origin without a preflight the device does not answer.

## Endpoints

### `GET /api/status`

```json
{
  "relays": [
    { "name": "Relay 1", "state": false, "schedEnabled": false, "onTime": "07:00", "offTime": "22:00" }
  ]
}
```

Always four entries. Names are JSON-escaped.

### `GET /api/set?relay=<0-3>&state=<bool>`

Sets one relay. `200 ok`. Errors (`400`): `missing relay`, `relay must be 0-3`, `missing state`,
`state must be 1, 0, true, false, on or off`.

### `GET /api/all?state=<bool>`

Sets all four relays. `200 ok`. Errors: `missing state`, `state must be ...`. Switch-on is staggered.

### `GET /api/rename?relay=<0-3>&name=<text>`

Renames a relay (URL-encode the name). `200 ok`. Errors: relay errors above, `missing name`,
`name must be 1-24 bytes of valid UTF-8 without control characters`.

### `GET /api/schedule?relay=<0-3>&enabled=<bool>&on=<HH:MM>&off=<HH:MM>`

Sets a relay's daily schedule. **All four parameters are required every time**; to change one field, read
`/api/status`, modify, and send all of them back. `200 ok`. Errors: relay errors, `missing enabled`,
`enabled must be ...`, `missing on or off`, `on and off must be HH:MM, 00:00-23:59`.

Schedule behaviour (edge-triggered, catch-up after outages, needs NTP) is described in
[ARCHITECTURE.md](ARCHITECTURE.md#scheduling).

### `GET /api/info`

```json
{
  "firmware": "1.0.0",
  "uptimeSeconds": 4210,
  "freeHeap": 168000,
  "resetReason": "power_on",
  "relayCount": 4,
  "wifi": { "connected": true, "apActive": false, "ip": "192.168.1.250", "rssi": -58 },
  "time": { "synced": true, "local": "2026-01-05 14:03" }
}
```

`time.local` appears only when synced. Use `time.synced` to know whether schedules are live.
`resetReason` is one of `power_on`, `software`, `panic`, `watchdog`, `brownout`, `other`.

### Firmware update

OTA is approval-gated and documented in [OTA.md](OTA.md). **An agent must not call `apply` without explicit
user confirmation: it reboots the device and every relay drops for several seconds.**

| Endpoint | Method | Behaviour |
|---|---|---|
| `/api/update/status` | GET | State, versions, progress |
| `/api/update/check` | POST | Starts a check. `200 ok`, or `409 busy` |
| `/api/update/apply` | POST | Installs the staged update. `200 ok`, or `409 no update available` |

`GET /api/update/status`:

```json
{
  "current": "1.0.0", "configured": true, "state": "available", "latest": "1.0.1",
  "progress": 0, "message": "", "pendingConfirm": false, "lastCheckSecondsAgo": 312
}
```

`state` is one of `idle`, `checking`, `up_to_date`, `available`, `downloading`, `verifying`, `rebooting`,
`error`. `lastCheckSecondsAgo` is `-1` until a check succeeds. `pendingConfirm` is true for about 90 s after
booting a new image. During `downloading` the API stays responsive and `progress` rises; the connection drops
when the device reboots.

## Notes for agents

1. **Confirm before disruptive actions:** `apply`, and `all?state=1` for heavy loads.
2. **Do not assume schedules are running.** Check `/api/info` `time.synced`; say "configured", not "active".
3. **Verify after writes,** waiting about 1.5 s after any switch-on.
4. **Prefer indexes to names.** Names are user-editable and not unique.
5. **Back off on errors.** The device is briefly unreachable during boot (roughly 1-60 s) and while an
   OTA check runs; retry with increasing delay.
6. **Treat the token as a secret.** Do not log it or put it in URLs.

## Examples

```bash
BASE=http://192.168.1.250
H="X-API-Key: $KEY"

curl -s -H "$H" "$BASE/api/status"
curl -s -H "$H" "$BASE/api/set?relay=0&state=on"
curl -s -H "$H" "$BASE/api/rename?relay=3&name=Water%20Pump"
curl -s -H "$H" "$BASE/api/schedule?relay=1&enabled=1&on=18:30&off=23:00"
curl -s -H "$H" -X POST "$BASE/api/update/check"
curl -s -H "$H" "$BASE/api/update/status"
```

## Tool definitions (function calling)

```json
[
  {
    "name": "relay_get_status",
    "description": "Read state, names and schedules of all 4 relays (0-indexed).",
    "input_schema": { "type": "object", "properties": {}, "required": [] }
  },
  {
    "name": "relay_get_info",
    "description": "Read firmware version, uptime, WiFi state and whether the clock is synced (schedules only run when time.synced is true).",
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
    "name": "relay_rename",
    "description": "Rename a relay. 1-24 bytes, valid UTF-8, no control characters.",
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
    "description": "Set a daily on/off schedule (device local time, every day). All fields required; read the current values first to change one. Runs only when the device clock is synced.",
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
`GET /api/schedule?relay=..&enabled={1|0}&on=..&off=..`. Every call carries the `X-API-Key` header. The update
endpoints are deliberately not exposed as agent tools.
