# Bench session — 2026-09-09

Where Phase 0 stands after the first real hardware session, and exactly what
to pick up next time.

## What's confirmed

- **Stock backup taken and verified.** `stock-panda-pwr-backup.bin` (repo
  root, gitignored) is a full 4 MB read from the actual unit, checked with
  `analysis/verify_backup.py`: bootloader present, partition table matches
  `HARDWARE_ANALYSIS.md` exactly, both `app0`/`app1` read as
  `panda_pwr 08a40b2-dirty`, IDF v5.1.1-dirty, built Jan 13 2025. **Restorable
  — this is the way back to stock if anything below goes sideways.**
  - It contains the stock NVS partition, which had this unit's real home
    Wi-Fi credentials in plaintext (ESP-IDF doesn't encrypt NVS unless flash
    encryption is on, and it isn't). Keep this file off any machine or
    service you don't trust with that.
- **Phase 1 firmware builds and boots clean on real hardware for the first
  time.** `idf.py -D IDF_TARGET=esp32c2 build` (ESP-IDF v5.3.5) produces a
  784 KB image against the 1280 K slot. Flashed over the Type-C port at
  `0x0`/bootloader, `0x8000`/partition table, `0xe000`/fresh otadata,
  `0x10000`/app — no case-opening needed, the CH340 bridge on the Prog port
  auto-resets into download mode.
- **Serial boot log is clean end to end**, repeatedly, across multiple fresh
  resets: on-device partition table print matches stock, `dp_relay` runs its
  safe-boot sequence (`mains -> off`, `usb1 -> off`, `outputs up, restore
  policy=off`), GPIO7/GPIO18 configured `InputEn+OutputEn` as predicted,
  `dc_wifi`/`dc_portal` reach steady state and return from `app_main()` with
  no crash, ever, in any capture (some as long as 40 s post-boot).
- **GPIO7 → relay, confirmed physically.** A reset pulse produced an audible
  relay click at exactly the moment `dp_relay_init` re-drove GPIO7 high
  (off). GPIO7 is genuinely wired to the relay coil and the safe-boot state
  does what the code says.

## What's blocked, and the working theory

**The softAP (`DragonPWR_BBF9`) is invisible over the air — not in any scan,
not from 4 inches away, right after a confirmed-clean boot that logs `esp_wifi_start`
succeeding.** This isn't a range problem (ruled out at 4 inches) and doesn't
look like a firmware crash (no panic, ever, in any capture).

Working theory: **the flash procedure never erased the NVS partition
(`0x9000`, 20 K).** It still held the stock firmware's old data — including
whatever Wi-Fi PHY calibration blob ESP-IDF v5.1.1 wrote there. If v5.3.5's
radio driver read that as valid calibration instead of doing a fresh
calibration, `esp_wifi_start()` returns `ESP_OK` while the radio transmits on
garbage power/frequency — a silent failure, not a crash.

**Fix attempted, not yet verified:** erased NVS with
`esptool erase_region 0x9000 0x5000` and hard-reset the device. Session ended
immediately after — **next step is checking whether the AP is now visible.**

## Next session, in order

1. Power the unit (Type-C only, mains still unplugged) and check the serial
   log for a fresh `dc_wifi: no saved WiFi credentials` / `starting AP` — NVS
   being blank should also mean no leftover `power`/`usb1` restore-policy
   state, so don't be surprised if that line's absent too.
2. Rescan for `DragonPWR_BBF9` (`netsh wlan show networks interface="Wi-Fi"
   mode=Bssid`, or just try connecting). If the NVS theory is right, it
   should show up immediately.
3. If it's still invisible: this is the point to suspect the module's
   antenna/RF path itself rather than software. The stock NVS's old
   `wifi_ap`/credential strings are evidence this unit's Wi-Fi worked at some
   point under stock firmware, so a hardware fault would be a regression from
   something we did, not a pre-existing dead radio — worth re-flashing the
   verified stock backup temporarily to confirm stock Wi-Fi still comes up on
   this exact unit, before spending more time on the DragonPWR side.
4. Once connected: bench-confirm GPIO18/USB1 the same way GPIO7 was
   confirmed, this time live over the API rather than incidentally at boot —
   `POST http://192.168.4.1/set` with body `usb=1` / `usb=0` (stock-compatible
   route) or `POST /api/v2/command` with `{"output":"usb1","on":true}`, while
   physically watching the port (a phone charging cable is enough to tell).
   Do the same for `power=1`/`power=0` on the mains relay for a second,
   deliberate confirmation (not just the incidental boot-time click).
5. Remaining Phase 0 items after that: relay latching-vs-momentary
   (unresolvable without watching it under repeated toggling), GPIO6's
   physical identity, whether the stock web UI's OTA accepts a foreign
   image. Metering-IC photo stays blocked — the case can't be opened
   non-destructively on this unit.

## Housekeeping already done

- `docs/ROADMAP.md` Phase 0 backup checkbox checked off.
- `.gitignore` now excludes `stock-*-backup.bin` explicitly (the README
  procedure said to keep it out of git; the ignore rule didn't actually exist
  yet).
