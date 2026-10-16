# OTA updates — GitHub releases, approved from the dashboard

Nothing installs by itself. The device **checks** for a new release (every 6 h and on demand), the
dashboard shows *"Update available: vX.Y.Z"*, and it only installs when you tap **Install**.

## How it works

1. `tools/release.sh X.Y.Z` (on your computer) builds the firmware, writes `manifest.txt`
   (`version`, `size`, `sha256`), signs the manifest with your **private** ECDSA key, and publishes
   `firmware.bin`, `manifest.txt`, `manifest.sig` as a GitHub release tagged `vX.Y.Z`.
2. The device downloads `manifest.txt` + `manifest.sig` from
   `github.com/<owner>/<repo>/releases/latest/download/`, verifies the signature with the public key
   compiled into the firmware, and compares versions (strict `MAJOR.MINOR.PATCH`, must be *newer*).
3. On **Install** it downloads `releases/download/v<version>/firmware.bin`, streams it into the
   inactive OTA slot while computing SHA-256, and compares that hash and the size to the signed
   manifest. Only if they match is the new slot activated and the device rebooted. On any mismatch the
   download is discarded and the running firmware is untouched.

## One-time setup

**1. Generate the signing keys** (on your computer; never commit the private key):

```bash
mkdir -p ~/.relay-ota && chmod 700 ~/.relay-ota
openssl ecparam -name prime256v1 -genkey -noout -out ~/.relay-ota/ota_private.pem
openssl ec -in ~/.relay-ota/ota_private.pem -pubout -out ~/.relay-ota/ota_public.pem
chmod 600 ~/.relay-ota/ota_private.pem
```

Back the private key up somewhere offline. **Lose it** and deployed devices can never be OTA-updated
again (you would USB-flash them with a new public key). **Leak it** and anyone can push firmware to
every device.

**2. Configure `src/main.cpp`:** set `OTA_GITHUB_OWNER`, `OTA_GITHUB_REPO` (the repo must be public),
and paste the contents of `ota_public.pem` into `OTA_PUBLIC_KEY_PEM`, one C string per line:

```cpp
const char* OTA_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE....\n"
"....\n"
"-----END PUBLIC KEY-----\n";
```

Until all three are set, the dashboard reports "Updates are not configured" and nothing is fetched.

**3. Flash once over USB.** The first OTA-capable build has to go on with a cable. Also check the
firmware size printed at the end of `pio run`: it must fit one app slot (1,310,720 bytes with
`default.csv`).

**4. Publish releases** with `tools/release.sh 1.0.1`. It needs `pio`, `gh` (logged in), `openssl`,
`python3`, `git` and a clean working tree, shows you the manifest, and asks before it commits, tags,
pushes and publishes. Release tags must be `v` + the version.

## Security model, and where it stops

- **Authenticity comes from the signature, not TLS.** By default TLS certificates are *not* validated
  (`OTA_ROOT_CA_PEM = nullptr`). I chose this because a wrong or expired pinned root silently disables
  updates, and the signature already covers what matters. A network attacker still cannot install
  their own firmware, cannot make the device accept an older release (version must be newer), and
  cannot alter a download without breaking the hash. They *can* block updates, and see that you
  fetch from GitHub. Nothing secret is ever sent. If you want TLS validation too, set `OTA_ROOT_CA_PEM`,
  but the PEM must cover both `github.com` and the host it redirects to for release assets.
- **A compromised GitHub account is not enough** to push firmware, because the private key is not on GitHub.
- **A compromised computer holding the private key is enough.** Protect that key accordingly.
- **A replay of an older, genuinely signed release is possible only for versions newer than the one you run.**
  Don't leave a known-bad release published.
- **No Secure Boot / flash encryption.** Stock Arduino cores ship a bootloader without them, so anyone with
  physical access can still reflash the board.
- **Anyone on your LAN can trigger an install of the latest genuine release** (`POST /api/update/apply`). There
  is no authentication on this device. It causes a reboot (relays drop and restore), not arbitrary code execution.

## Rollback

A new image must stay up for 90 s (`OTA_CONFIRM_AFTER_MS`) to confirm itself. The boot counter is
incremented at the start of `setup()`. If an unconfirmed image boots more than 3 times
(`OTA_MAX_UNCONFIRMED_BOOTS`), it re-activates the previous slot, records the bad version in flash,
and will not offer that version again (a newer release is needed).

This is a software mechanism, not the bootloader's: I could not confirm the stock Arduino-ESP32 2.0.14
bootloader has app-rollback enabled, and the bootloader is not updated by OTA, so I did not rely on it.

**It does NOT catch:**
- a crash before `setup()` starts (static initialisers, bootloader-level faults), which boot-loops forever and needs a USB reflash;
- firmware that boots and stays up but is wrong (inverted polarity, broken schedules). Test releases on one device first;
- three power cuts within 90 s of each other, which cause a harmless but unnecessary rollback.

## What an update does to your loads

The device reboots. The GPIOs drop, the relays release, then the saved state is restored (1 s settle +
400 ms stagger). Expect the loads to be off for several seconds. Don't install while something sensitive is running.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Could not download manifest" | No release published yet, repo not public, wrong owner/repo, or no internet |
| "Manifest signature INVALID" | Public key in firmware doesn't match the private key used by `release.sh`, or assets were edited |
| "Firmware does not fit in the update slot" | Binary larger than one app slot; shrink it or use a larger partition table |
| "Firmware size does not match the signed manifest" | Server didn't send a Content-Length, or the asset was replaced after signing |
| Device resets during download with a stack-canary message | TLS + HTTP on the default 8 KB loop stack; raise the loop task stack size |
| Never offers an update | `FIRMWARE_VERSION` ≥ release version, or that version previously rolled back |

## Verification status

The OTA logic was tested on a host machine against stub Arduino headers and real mbedtls 2.28: signature
verification with `openssl`-signed data (valid, tampered manifest, tampered signature, wrong key, bad key),
version comparison, replay of an older manifest, a good install, a corrupted image, a wrong-size image, and
the rollback counter. **It has not been run on an ESP32**, and the real Arduino-ESP32 `HTTPClient`,
`WiFiClientSecure`, `Update` and redirect handling against live GitHub URLs are untested.
