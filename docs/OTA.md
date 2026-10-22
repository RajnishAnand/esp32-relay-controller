# Over-the-air updates

Updates come from GitHub releases. Nothing installs by itself: the device **checks** (every 6 hours and on
demand), the dashboard shows "Update available", and the device installs only after you tap **Install**.

## How it works

1. `tools/release.sh X.Y.Z` builds the credential-free `release` firmware, writes `manifest.txt`
   (`version`, `size`, `sha256`), signs it with your private ECDSA key, and publishes `firmware.bin`,
   `manifest.txt` and `manifest.sig` as release `vX.Y.Z`.
2. The device downloads `manifest.txt` and `manifest.sig` from
   `github.com/<owner>/<repo>/releases/latest/download/`, verifies the signature with the public key compiled
   into the firmware, and requires the version to be strictly newer (`MAJOR.MINOR.PATCH`).
3. On **Install** it downloads `releases/download/v<version>/firmware.bin`, streams it into the inactive OTA
   slot while hashing it, and compares size and SHA-256 with the signed manifest. Only on a match does it
   activate the slot and reboot. On any mismatch the download is discarded and the running firmware is
   untouched.

## One-time setup

**1. Generate signing keys** on your computer (never commit the private key):

```bash
mkdir -p ~/.relay-ota && chmod 700 ~/.relay-ota
openssl ecparam -name prime256v1 -genkey -noout -out ~/.relay-ota/ota_private.pem
openssl ec -in ~/.relay-ota/ota_private.pem -pubout -out ~/.relay-ota/ota_public.pem
chmod 600 ~/.relay-ota/ota_private.pem
```

Back the private key up offline. **Lose it** and deployed devices can never be updated over the air again (you
would USB-flash them with a new public key). **Leak it** and anyone can push firmware to every device.

**2. Edit `include/ota_config.h`:** set `OTA_GITHUB_OWNER`, `OTA_GITHUB_REPO` (the repository must be public) and
paste `ota_public.pem` as `OTA_PUBLIC_KEY_PEM`, one C string per line:

```cpp
#define OTA_PUBLIC_KEY_PEM                  \
  "-----BEGIN PUBLIC KEY-----\n"            \
  "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...\n" \
  "...\n"                                   \
  "-----END PUBLIC KEY-----\n"
```

**3. Flash once over USB** with a development build (see [CONFIGURATION.md](CONFIGURATION.md)). Check that the
firmware size printed by `pio run` fits one app slot (1,310,720 bytes).

**4. Publish releases** with `tools/release.sh 1.0.1`. It needs `pio`, `gh` (logged in), `openssl`, `python3`,
`git` and a clean working tree. It refuses to continue if `ota_config.h` still has placeholders or if the built
firmware contains any value from `include/secrets.h`. It shows the manifest and asks before committing, tagging,
pushing and publishing. Tags must be `v` + the version.

## Why published firmware is safe to make public

Release binaries are built with `-DRELEASE_BUILD` and contain **no credentials**. WiFi and API credentials live
in the device's flash, put there by the USB-flashed development build; an OTA update leaves them alone.

## Trust model

- **Authenticity comes from the signature, not TLS.** By default TLS certificates are not validated
  (`OTA_ROOT_CA_PEM` is undefined). A wrong or expired pinned root silently disables updates, and the signature
  already covers what matters: a network attacker cannot install their own firmware, cannot make the device
  accept an older release (version must be newer), and cannot alter a download without breaking the hash. They
  can block updates and see that you contact GitHub. Nothing secret is ever sent. To add TLS validation, define
  `OTA_ROOT_CA_PEM` in `ota_config.h`; the PEM must cover both `github.com` and the host that release assets
  redirect to.
- **A compromised GitHub account cannot push firmware,** because the private key is not on GitHub.
- **A compromised computer that holds the private key can.** Protect it like a password.
- **Replay:** an attacker can re-serve an older genuinely signed release only if its version is newer than the
  one running. Do not leave a known-bad release published; delete it.
- **No Secure Boot or flash encryption.** Stock Arduino builds do not enable them, so physical access still
  allows reflashing and reading the credentials.
- **Triggering:** anyone with the admin token can trigger an install (guests cannot) of the latest genuine release.

## Rollback

A new image must stay up for 90 s (`kOtaConfirmAfterMs`) to confirm itself. The boot counter increments at the
very start of `setup()`. If an unconfirmed image boots more than 3 times (`kOtaMaxUnconfirmedBoots`), the
previous slot is re-activated, the bad version is recorded, and that version is not offered again.

This is a software mechanism. The stock Arduino-ESP32 2.0.14 bootloader may not have app-rollback enabled, and the
bootloader is not updated over the air, so the firmware does not depend on it.

It does **not** catch:

- a crash before `setup()` starts, which boot-loops until you reflash over USB;
- firmware that runs but misbehaves (inverted polarity, broken schedules): test every release on one device first;
- three power cuts within 90 s of each other, which cause a harmless but unnecessary rollback.

## What an update does to your loads

The device reboots. GPIOs drop, relays release, then saved state is restored (1 s settle plus the stagger).
Expect loads to be off for several seconds. Do not install while something sensitive is running.

Checks and downloads run on a background task, so the dashboard, relays and schedules keep working while they
run. Changing the WiFi network is refused while a download is in progress.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Could not download the manifest (HTTP 404 ...)" | No release published yet, repo not public, or wrong owner/repo |
| "...(connection refused ...)", "...(read Timeout ...)" | No internet, DNS or TLS trouble, or the device is short of memory. The message ends with the largest free memory block; a TLS connection needs tens of kilobytes in one piece, so a small number there points to memory |
| "Could not start the update task (low memory?)" | Not enough free memory for the 16 KB task stack. Reboot and try again; check `maxAllocHeap` in `/api/info` |
| "Manifest signature INVALID" | Public key in `ota_config.h` does not match the signing key, or assets were edited |
| "Firmware does not fit in the update slot" | Binary larger than one app slot |
| "Firmware size does not match the signed manifest" | Server sent no Content-Length, or the asset was replaced after signing |
| Reset during download with a stack-canary message | TLS plus HTTP on the default 8 KB loop stack; raise the loop task stack size |
| Never offers an update | Running version is not older than the release, or that version previously rolled back |

## Verification status

Automated host tests cover the signature chain (valid, tampered manifest, tampered signature, wrong key,
unparseable key), version rules including replay of an older manifest, a good install, a corrupted image, a
wrong-size image, the boot counter and rollback, and confirmation after uptime. **Not verified on hardware:**
the real Arduino-ESP32 `HTTPClient`, `WiFiClientSecure`, `Update` and redirect handling against live GitHub
URLs, flash writes, and the rollback boot path.
