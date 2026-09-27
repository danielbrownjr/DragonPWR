# Releasing DragonPWR

DragonPWR firmware versions come from `git describe --tags --always --dirty`,
so the release tag is also the firmware version embedded in the ESP-IDF app
description.

## Release gate

A firmware release must:

1. be cut from `main`,
2. have a source-controlled release note at `docs/releases/<version>.md`,
3. pass the ESP32-C2 build workflow using ESP-IDF 5.3.5,
4. produce an OTA app image and a merged full-flash image,
5. verify that the built app contains the exact release version,
6. publish SHA-256 hashes with the binaries.

Warnings or failed build steps are release blockers.

## Creating a release

Use **Actions → Create firmware release → Run workflow** on `main`.

Enter the exact version, for example:

```text
0.0.1rc1
```

The workflow validates the version and release notes, creates the tag locally so
the build embeds that exact version, builds for ESP32-C2, creates both flash
images, verifies the embedded version, pushes the tag only after a successful
build, then creates or refreshes the GitHub release.

Versions containing `rc` are published as GitHub prereleases.

## Artifacts

- `DragonPWR-<version>-ota.bin` — app image for browser OTA on an existing DragonPWR installation
- `DragonPWR-<version>-full.bin` — merged image flashed at address `0x0` for first install/recovery/bootloader refresh
- `SHA256SUMS.txt` — integrity hashes

The full image is not a replacement for the user's original stock backup.
