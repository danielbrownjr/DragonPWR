# Bench sessions

Where Phase 0 stands after each real hardware session, and exactly what to
pick up next time.

## 2026-09-27 — teardown: metering IC is an HLW8112

A Panda PWR Rev 1 enclosure was opened destructively and the board inspected
directly. The unit opened destructively for this teardown is the purchased
spare. The original Panda PWR remains intact and is retained for bench and
firmware testing; continuity measurements from here on belong to the spare.

The spare cannot be resealed as it was: it has lost its touch protection, so it
should not go back into service as a plug unless properly re-enclosed.
Photographs by Daniel Brown are in [docs/images/hardware/](images/hardware/).

**Identified by marking:**
- ESP8684-MINI-1-H4 controller
- Hi-Link HLK-20M05 auxiliary supply: 100–240 VAC in, 5 VDC 4 A 20 W out
- Songle SRD-05VDC-SL-B mains relay
- **HLW8112** metering IC (second marking line `2423W1D`)
- ZMPT107-1 voltage-sensing transformer
- a fuse marked `T2A 250V`, and a separate glass cartridge fuse whose rating
  isn't legible
- RGB status LED, USB-C programming/service port, two USB-A ports, `RESET`
  button, and a pad row silkscreened roughly `GND RX TX 3V3`

**Metering-chip unknown closed.** The stock UART framing, `0xEA` write gate and
init registers match the HLW8112 datasheet; the decode is in
docs/HARDWARE_ANALYSIS.md. Stock enables current channel A only.

**Inferred, not confirmed:** the yellow EI-core transformer is probably the
current-sense transformer; the green toroid is probably a common-mode choke;
the blue disc is possibly surge suppression; the yellow box capacitor is
probably mains EMI suppression. An `AP65N06NF` marking was photographed; its
role is untraced.

**Not mapped yet:** nothing was traced with a meter this session. GPIO6's
source, the relay COM/NO path, what either fuse protects, the ZMPT107-1 and
current-sense paths into the HLW8112, HLW8112 TX/RX to GPIO3/GPIO2, the 5 V and
3.3 V rails, and the isolation boundary are all open. The full list is open
question 6 in docs/HARDWARE_ANALYSIS.md.

**Next session:** unpowered continuity mapping first, starting with HLW8112
TX/RX to GPIO3/GPIO2 and GPIO6's trace. Prefer unpowered continuity work. Any
energized measurement requires a defined mains-safe setup and appropriately
rated isolated or differential instrumentation.

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

**Console-UART baud mystery, resolved (separately from the crystal fix
above).** The console still needed 74880 baud even after `CONFIG_XTAL_FREQ`
was corrected to 26 — flipping that Kconfig setting fixed the Wi-Fi radio
(verified above) but had zero measurable effect on the console's real baud,
which stayed scaled by the identical 0.65× factor with the fix in place.
That ruled out a Kconfig-propagation bug. Chased it much further than
expected:

- Confirmed the true crystal really is 26,000,000 Hz via
  `esp_clk_tree_src_get_freq_hz(UART_SCLK_XTAL, EXACT, ...)` forcing a
  fresh hardware calibration inside the running firmware — not just
  trusting `esptool`'s or Kconfig's say-so.
- Explicitly forced the UART's clock-source mux to XTAL
  (`uart_ll_set_sclk`) and fed the divider that exact, freshly-measured
  26,000,000 Hz value directly (`uart_ll_set_baudrate`) — still came out
  scaled wrong. So it isn't a wrong-clock-source-selected bug either; the
  divider math genuinely produces the wrong result even given a verified
  input.
- Found a build that reconfigured the baud correctly from `app_main()` via
  the ordinary `uart_set_baudrate()` API and read cleanly end-to-end at
  115200 (STA-connect to a real network included). Tried to reproduce that
  exact recipe deterministically — same code, same reset method, repeated
  8+ times — and it only worked once. Every placement tried (top of
  `app_main`, end of `app_main`, with/without preceding delays, with/without
  preceding `esp_clk_*()` calls) succeeded roughly 1 time in 8-10 resets and
  otherwise reproduced the 74880 baud unchanged.
- That erratic, "sometimes works, mostly doesn't, same code" signature
  turned out to be a **known, Espressif-acknowledged hardware quirk**, not
  a bug in this project or in ESP-IDF's Kconfig plumbing:
  [espressif/esp-idf#2518](https://github.com/espressif/esp-idf/issues/2518).
  A contributor there reproduced the identical symptom on a 26MHz-crystal
  ESP32 (sporadic 74880-instead-of-115200 right after power-up), root-caused
  it to the crystal oscillator's own startup transient rather than
  software, applied the exact same `uart_set_baudrate(0, 115200)` mitigation
  from `app_main()`, and confirmed it narrows the window but doesn't
  guarantee it — matching everything observed here.

**Fix shipped:** `main/app_main.c` calls `uart_set_baudrate()` for the
console UART as the first thing `app_main()` does (the upstream-recommended
placement and mitigation). It measurably improves the odds of getting a
readable 115200 log but is **not guaranteed** — if a bench session's log
looks like garbage, retry the reset once or twice, or just open the monitor
at 74880 baud as a fallback. This is a hardware characteristic of this
crystal, not something further software changes are expected to fully
close out.

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

**`dp_button` (GPIO10) and the control-token gate, flashed and bench-tested
the same day they were written.** Built at commit `14748da` (768 K, 40 % of
the app slot free), flashed over COM6 with no `--erase-flash` (NVS carried
over from the softAP-fix session, so restore policy/AP config persisted as
expected). Post-flash Wi-Fi reassociated three times in the first ~50 s
(1:53:24-1:54:13, ~15-17 s apart in the Windows WLAN event log) before
settling — read the boot-log baud quirk plus normal AP-driver settling as
the likely cause; two 35-45 s serial-console and `netsh` polling windows
after that showed a rock-solid 100 % signal connection and no reboot, so
this wasn't chased further as a firmware bug.

GPIO10: **confirmed physically** — every press of the front button produces
an audible relay click, one click per press (not two, so the 60 ms debounce
in `dp_button` is doing its job against contact bounce), toggling mains
through the same `dp_relay_set()` path the HTTP API uses.

**Control-token gate, confirmed end to end.** `POST /api/v2/token` set a
throwaway token (using the `web` sentinel header, since no token was
configured yet — presence-only tier, exactly as designed); `/api/v2/command`
then 403'd with no header, 403'd with a wrong token, and succeeded with the
right one (toggled `usb1` on, confirmed via `/api/v2/state`). Also caught,
by accident, a good negative test: a follow-up run of the relay-toggle script
below forgot the header entirely and every one of its 10 requests 403'd
cleanly rather than silently doing nothing or crashing. Cleaned up after:
`usb1` back off, token cleared, device left open again.

**Relay latching vs. momentary — resolved, without opening the case.** 5
on/off cycles driven over `/api/v2/command`, 1.5 s apart (10 toggles total,
`mains` empty of any real load — confirmed empty first). Clicks were heard
at the same ~1.5 s cadence the commands were sent at, not ~3 s, meaning both
the on-edge and the off-edge clicked, not just one direction. That is the
signature of a standard, continuously-driven relay (needs coil current held
to stay closed) rather than a latching/bistable one (which would pulse once
per direction change and stay silent while held). Caveat: this was ear-timed
against the known 1.5 s send interval, not an independent count of 10
distinct clicks, so treat it as high-confidence rather than absolute.

Not yet exercised this session: GPIO6 (the maintained-contact toggle input) —
and per the user, not testable at all without opening the case, which isn't
happening non-destructively on this unit. Left as a documented unknown
rather than chased further.

**Stock web UI's OTA — resolved: there isn't one to test.** Scanned the
app0 partition extracted straight from the verified `stock-panda-pwr-backup.bin`
for anything upload-shaped; found only `/set` and `/update_ele_data`, both
already-known status/control routes, no distinct OTA route. To confirm
directly rather than trust an absence-of-evidence read: restored the full
stock backup (`esptool write_flash 0x0 stock-panda-pwr-backup.bin`, the
documented README recovery procedure). Wi-Fi scans afterward showed no
device AP at all - it apparently tried to join its previously-saved home
network, which isn't in range at this location. Erased its NVS
(`erase_region 0x9000 0x5000`, same move that fixed DragonPWR's own softAP
issue) to force a clean-credentials state and rescanned: still no AP of any
kind. So stock has no AP-provisioning fallback the way `dc_wifi` does - it
either updates over the (unreachable, home-network-only) LAN, over BTT's
cloud, or exclusively via the serial recovery tool. Either way, there is no
local web UI reachable to even attempt a foreign-image upload against.
Incidental finding along the way: stock's own console UART shows the same
scaled-baud symptom DragonPWR hit before the `CONFIG_XTAL_FREQ` fix - the ROM
banner only decoded cleanly at 74880, and nothing further came through at
either 74880 or 115200 in a 30 s window. Consistent with BTT shipping the
same 40MHz-assumed-crystal mismatch; not chased further since it's not
DragonPWR's bug to fix.

Reflashed DragonPWR back on (`idf.py -p COM6 flash`) once the above was
settled.

**Real incident, caught in the course of the above: mains switched on with
nobody touching anything.** After the reflash, `/api/v2/state` read
`"mains":true` despite `restore:"off"` — which should force it off at every
boot — and several minutes having passed (Wi-Fi scanning, reconnecting)
since. Turned it off immediately via the API. Root cause: GPIO6 has no pull
resistor (matching stock, since it's meant to be externally driven), its
physical identity is still unconfirmed, and `dp_button`'s GPIO6 handling
acted on *any* stable transition by toggling mains — exactly the kind of
event a floating, unconnected pin produces on its own given enough idle
time. Fixed in `dp_button.c`: GPIO6 transitions are now logged (useful
forensic data for eventually identifying it) but no longer drive the relay.
Rebuilt (768 K, still 40% free), reflashed, confirmed `mains:false` on the
fixed image. Nothing was plugged into the outlet at any point during this,
confirmed before the original relay-toggle test — so the practical
consequence was zero, but the design gap was real and is now closed.

## Next session, in order

1. GPIO6's physical identity and the metering IC photo both stay blocked —
   neither is reachable without opening the case, which isn't happening
   non-destructively on this unit. Documented as permanent unknowns unless
   that changes. `dp_button` no longer acts on GPIO6 either way, so this is
   informational only now, not a safety gap waiting to be closed.
3. Figure out the 74880-vs-115200 UART anomaly above if it becomes
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
