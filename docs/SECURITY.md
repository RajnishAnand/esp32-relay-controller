# Security

This device switches real loads and exposes an HTTP interface. This page states what is protected, what is not,
and how to deploy it sensibly.

## What is protected

| Threat | Protection |
|---|---|
| Someone on the network calling the API or dashboard actions | Every `/api/*` call needs the `X-API-Key` token, compared in constant time. The API fails closed (`503`) until a valid token is stored |
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
- **No rate limiting or lockout.** Use a long random token (the firmware requires 16+ characters; 32 hex
  characters from `openssl rand -hex 16` is good).
- **No per-user accounts or audit log.** One shared token.
- **Denial of service.** A single-threaded server can be overwhelmed by anyone who can reach it.
- **The token is stored in the browser** (`localStorage`) after you enter it on the dashboard. Use "Forget access
  token" on shared computers.

## Deployment recommendations

1. Put the device on a trusted or isolated (IoT/VLAN) network. Do not expose port 80 to the internet and do not
   port-forward it.
2. For remote access, use a VPN, or a reverse proxy that terminates TLS and adds its own authentication.
3. Keep the fallback AP password strong; it is the only thing between a nearby attacker and the AP network.
4. Treat the OTA **private key** like a password: store it offline, back it up, never commit it.
5. Rotate the token by editing `include/secrets.h` and flashing a development build over USB. There is no
   remote rotation.

## Reporting a vulnerability

Open a private security advisory on the repository, or contact the maintainer listed in the repository
profile. Please do not file public issues for undisclosed vulnerabilities.
