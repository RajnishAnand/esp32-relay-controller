# Security

This device switches real loads and exposes an HTTP interface. This page states what is protected, what is not,
and how to deploy it sensibly.

## What is protected

| Threat | Protection |
|---|---|
| Someone on the network calling the API or dashboard actions | Every `/api/*` call needs an `X-API-Key` credential, compared in constant time. The API fails closed (`503`) until a valid admin token is stored |
| A leaked or over-trusted credential doing too much | Two roles. The **guest password** can only view and switch relays; renaming, schedules, WiFi, guest password management and firmware updates need the **admin token** |
| Guessing the (human-chosen) guest password | After 8 failures guest login pauses for 60 s. The admin token is never throttled and is long (16+ characters) |
| Typing a WiFi password wrong and stranding the device | A network change saves the new credentials only after a successful connection, and reverts to the previous network on failure |
| Cross-site request forgery and link prefetchers | The custom header requirement: browsers do not send it cross-origin without a preflight, which the device does not answer |
| Clickjacking and injected script in the dashboard | `Content-Security-Policy` with `frame-ancestors 'none'`; all dynamic values are escaped in the page |
| Malformed or hostile input | Strict parsing of every parameter; names must be valid UTF-8 without control characters; JSON output is escaped |
| Malicious or tampered firmware | Updates must carry an ECDSA-signed manifest; the image hash is checked before activation; only newer versions are accepted |
| Credentials leaking through published firmware | Release builds contain no credentials; `tools/release.sh` refuses to publish a binary that does |

## What is not protected

- **No TLS on the device.** The token and all traffic are visible to anyone who can sniff the network. The token
  protects against casual, accidental and cross-site access, not against a passive eavesdropper on your LAN.
- **No Secure Boot and no flash encryption.** Anyone with physical access can reflash the device and read the
  credentials from flash (NVS is not encrypted).
- **Only the guest login is throttled, and the throttle is global.** An attacker who can reach the device can
  keep guests locked out (admin still works). There is no per-client tracking. Use a strong guest password and a
  long random admin token (the firmware requires 16+ characters; 32 hex characters from `openssl rand -hex 16`
  is good).
- **No per-user accounts or audit log.** One shared admin token and one shared guest password.
- **The guest password and saved WiFi password are stored in flash in clear text,** like the admin token.
- **The admin token cannot be changed remotely.** Rotate it by editing `include/secrets.h` and flashing over USB.
- **Secrets sent over HTTP** (the credential header, the WiFi password you submit, the guest password you set)
  are visible to anyone who can sniff the network you are on.
- **Denial of service.** A single-threaded server can be overwhelmed by anyone who can reach it.
- **The token is stored in the browser** (`localStorage`) after you enter it on the dashboard. Use "Forget access
  token" on shared computers.

## Deployment recommendations

1. Put the device on a trusted or isolated (IoT/VLAN) network. Do not expose port 80 to the internet and do not
   port-forward it.
2. For remote access, use a VPN, or a reverse proxy that terminates TLS and adds its own authentication.
3. Keep the fallback AP password strong; it is the only thing between a nearby attacker and the AP network.
4. Treat the OTA **private key** like a password: store it offline, back it up, never commit it.
5. Rotate the admin token by editing `include/secrets.h` and flashing a development build over USB. There is no
   remote rotation. Change the guest password from the dashboard whenever someone who knew it should lose access.
6. Give automations and agents the guest password, not the admin token.
7. Change the WiFi from a device you can still reach afterwards. A switch to a network you cannot reach leaves the
   device on the fallback access point (if the switch fails) or unreachable from your current network (if it
   succeeds).

## Reporting a vulnerability

Open a private security advisory on the repository, or contact the maintainer listed in the repository
profile. Please do not file public issues for undisclosed vulnerabilities.
