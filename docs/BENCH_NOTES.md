# Bench sessions

Where Phase 0 stands after each real hardware session, and exactly what to
pick up next time.

## 2026-09-14 — softAP fixed: XTAL_FREQ mismatch, not stale NVS

Picking up from the 09-09 session's blocker: the softAP was invisible over
the air despite a clean boot log and `esp_wifi_start()` returning `ESP_OK`.
The working theory going in was stale NVS/PHY-cal data; the NVS erase from
last session was confirmed to have taken (`dc_wifi: no saved WiFi
credentials` on every fresh boot this session), but the AP was **still**
invisible — an over-the-air scan from the dev machine's own Wi-Fi adapter
found 12 real networks nearby and no `DragonPWR_BBF9` among them. So the
NVS theory was wrong, or at least incomplete.

**Root cause found:** `sdkconfig` had `CONFIG_XTAL_FREQ_40=y`, but
`esptool`'s hardware-measured chip detection reports this board's actual
crystal as 26MHz (`esptool --port COM6 chip-id`: "Crystal frequency:
26MHz", chip is ESP8684H rev v1.2). With the wrong assumed crystal,
clock-derived peripherals run scaled by the true/assumed ratio (26/40 =
0.65×) — confirmed directly on the console UART, which only reads cleanly
at 74880 baud instead of the configured 115200 (74880 = 115200 × 0.65,
exact). The same boot log showed `dc_wifi: scan done: 0 networks` on the
STA side — the radio wasn't seeing *any* real traffic, not just failing to
be seen.

**Fix:** flipped `sdkconfig` to `CONFIG_XTAL_FREQ_26=y` / `CONFIG_XTAL_FREQ=26`
(matching the measured hardware), rebuilt with `idf.py -D IDF_TARGET=esp32c2
build` (ESP-IDF v5.3.5 at `C:\esp\v5.3.5\esp-idf`), reflashed over COM6.

**Result, verified two ways:**
- On-device STA scan now reports `dc_wifi: scan done: 12 networks` — same
  count as the dev machine's independent scan of the same environment.
- **`DragonPWR_BBF9` is now visible over the air** (`netsh wlan show
  networks`): 100% signal, `BSSID e8:6b:ea:95:bb:f9`, channel 1, matching
  the softAP MAC printed in the boot log. This is the actual practical
  test, not just a log reading.

**Open oddity, not yet explained:** the console UART still only reads
cleanly at 74880, not 115200, even after the `CONFIG_XTAL_FREQ` fix — so
whatever governs this specific UART's clock source doesn't fully track
that Kconfig setting the way the Wi-Fi radio's frequency synthesis
apparently does. Harmless for now (just remember to open the monitor at
74880 on this unit), but worth understanding before assuming any other
clock-sensitive peripheral is unaffected.

**Housekeeping:** `docs/ROADMAP.md`'s softAP-adjacent Phase 1 notes should
get a line about the 26MHz crystal once someone reconfirms this is
consistent (not a one-off bad reading) — worth checking whether BTT's
other Panda PWR units are also 26MHz-crystal parts or if this is
unit-specific.

**GPIO18/USB1 and the mains relay, confirmed live over the API.** Connected
to `DragonPWR_BBF9` (password `987654321`, the `dc_wifi` component default —
never changed by this firmware). `GET /api/v2/state` first read
`{"outputs":{"mains":false,"usb1":false},"restore":"off",...}`, matching the
safe-boot state. Toggled both outputs via the stock-compatible route
(`POST /set` with `usb=1`/`usb=0`, then `power=1`/`power=0` — PowerShell's
`curl` alias needs `curl.exe` or `Invoke-RestMethod` explicitly, plain
`curl -X POST ... -d ...` doesn't work under `Invoke-WebRequest`'s aliasing).
Confirmed working end to end — this closes out GPIO7 (relay) and GPIO18
(USB1) as fully confirmed, both by physical observation now, not just by
safe-boot-time inference.

## Next session, in order

1. Relay latching-vs-momentary (unresolvable without watching it under
   repeated toggling), GPIO6's physical identity, whether the stock web
   UI's OTA accepts a foreign image. Metering-IC photo stays blocked — the
   case can't be opened non-destructively on this unit.
2. Figure out the 74880-vs-115200 UART anomaly above if it becomes
   annoying enough to matter, or if a future clock-sensitive bug shows up
   that this might also explain.

## 2026-09-09 — first hardware session

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
