# DragonPWR Roadmap

Open firmware for the BIGTREETECH Panda PWR, built on
[`dragon-core`](https://github.com/justinh-rahb/dragon-core).

## Goals

- Replace the stock firmware on Panda PWR hardware with a Klipper-first,
  cloud-free alternative
- Be the smart plug that **knows what is plugged into it**: never cut power
  mid-print, power down only once the printer is actually cool, and account for
  energy per print
- Keep the stock HTTP API working so existing integrations survive the swap
- Install and update from a browser, over the stock partition table
- Feature parity with stock where it makes sense; skip cloud-only and
  Panda-Touch-only features

## What makes this device worth writing firmware for

A generic smart plug has a meter but no idea what is plugged into it. Moonraker's
`[power]` plugin knows the print state but cannot measure anything. The Panda PWR
has both a mains meter and — through `dragon-core` — the printer's real state, so
it can answer questions neither side can answer alone:

- Is the printer *actually* printing, or is it drawing 15 W and hung?
- How much did that print cost?
- Is it safe to cut power right now?

Every phase below is ordered by how directly it serves that.

## The constraint that shapes the phases

Flash, not ideas.

| | |
|---|---|
| App slot (stock partition table) | **1280 K** |
| **DragonPWR Phase 1, measured** | **768 K — 40 % of the slot free** |
| DragonStatus v1.0.0 OTA image (ESP32-C3) | 1.15 MB |
| DragonVent v0.5.9 OTA image (ESP32) | 1.20 MB |

The Phase 1 number is real, from `idf.py build` against ESP-IDF v5.3.1 for
`esp32c2`: Wi-Fi, captive portal, the family SPA, mDNS, OTA and the product API
come to 778 K, leaving about 500 K free. That is far more headroom than the
sibling images suggested — they carry Moonraker, Bambu, lighting and audio on
top. The budget is real but it is not tight yet; re-measure at the end of every
phase and keep this table honest.

Two consequences still stand:

- `dc_source` only *starts* the selected printer client, but every linked client
  still costs flash. Ship **Moonraker-only** first, then add sources one at a
  time and watch the map file.
- Bambu LAN MQTT needs mbedTLS, and the ESP32-C2 has **272 KB of SRAM** against
  the C3's 400 KB. If something has to go, Bambu is the candidate: dropping it
  buys back both flash and RAM.

`CONFIG_COMPILER_OPTIMIZATION_SIZE=y` from the start, as DragonStatus does.

## Upstream dependency: there is no DragonPWR surface in `dc_ui` yet

The shared family SPA recognises `dragonbreath`, `dragonvent`, `dragonstatus`
and `dragonwheeze`. A `dragonpwr` product descriptor gets the common setup and
provisioning overlay — Wi-Fi, printer source, OTA, logs, factory reset — but not
a dedicated dashboard.

So the UI work splits in two: DragonPWR ships `/api/v2/info`, `/api/v2/state` and
the command routes from day one, and the matching surface is a **contribution to
dragon-core's `dc_ui`**, not something this repo can carry alone. Phase 1 is
usable without it.

## Phase 0 — Bench confirmation ⛔ blocked on hardware

Everything in [HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md) is derived from the
stock binary and has never been checked against a board.

- [x] Back up the stock flash over USB **before anything else** — BTT publishes
      the app images but not a full flash dump, so this is the only way back.
      Verified restorable: `app0`/`app1` both read as `panda_pwr 08a40b2-dirty`,
      IDF v5.1.1-dirty, built Jan 13 2025 — matches the build already analysed
      in HARDWARE_ANALYSIS.md
- [x] Confirm GPIO7 drives the relay, and that it is active **low** —
      physically confirmed twice: an audible click at safe-boot re-drive
      (09-09), then a deliberate on/off toggle over `/set` (09-14). See
      docs/BENCH_NOTES.md.
- [x] Confirm GPIO18 switches USB1, active high — confirmed the same way,
      deliberate on/off toggle over `/set` (09-14). See docs/BENCH_NOTES.md.
- [ ] Identify GPIO6 physically. The handling is edge-triggered-toggle, but the
      manual documents only the Bind button and no photo shows a second control,
      so what drives this pin is genuinely unknown
- [ ] Photograph the metering IC and match it against the register map
- [x] Determine whether the relay is latching or momentary — 5 on/off cycles
      driven 1.5 s apart over `/api/v2/command`; clicks landed at that same
      ~1.5 s cadence (not ~3 s), i.e. **both** the on and the off edge
      clicked, not just one. Consistent with a standard, continuously-driven
      relay, not a latching/bistable one. Ear-timed, not click-counted — see
      docs/BENCH_NOTES.md for the caveat
- [x] Check whether the stock web UI's OTA accepts a foreign image — moot:
      stock has **no local update surface to test in the first place**. No
      upload-shaped HTTP route exists anywhere in the app0 image (only `/set`
      and `/update_ele_data`), and restoring the verified stock backup
      produced no reachable AP even with NVS wiped — stock apparently has no
      AP-provisioning fallback the way `dc_wifi` does. First install needs
      serial; that was never a DragonPWR limitation to begin with. See
      docs/BENCH_NOTES.md

The first relay test must happen with the mains side disconnected.

## Phase 1 — Bring-up

"It boots, the portal works, the relay switches, and it cannot be bricked from a
browser."

- [x] `dp_board` pin map, with polarity encoded once and only once
- [x] `dp_relay`: mains + USB1 outputs, safe-by-construction boot state
      (GPIO7 written **high before** it becomes an output), readback via
      `gpio_get_level` the way stock does
- [x] Power-loss restore policy — off / on / last state, persisted to NVS,
      defaulting to **off**
- [x] `dp_button` on GPIO10 (confirmed physically: audible relay click, one
      per press) drives `dp_relay_set()` directly, so the Phase 2 interlock
      protects it too once it lands there. GPIO6 is polled and its
      transitions are logged, but deliberately **not** wired to the relay —
      no pull resistor plus unconfirmed physical identity means a floating
      pin can produce a spurious "transition," and a bench session caught
      exactly that: mains switched on with nobody touching anything. See
      docs/BENCH_NOTES.md
- [x] Wi-Fi, captive portal, mDNS, OTA, factory reset — all inherited from
      `dc_wifi` + `dc_portal`
- [x] `/api/v2/info` + `/api/v2/state` + `/api/v2/command`
- [x] Stock-compatible `/set` and `/update_ele_data`, so
      [HA-Panda-PWR](https://github.com/juanillo62gm/HA-Panda-PWR) keeps working
      across the firmware swap
- [x] Byte-identical partition table, verified against the stock binary, so install-over-stock stays possible
- [x] Record the real image size and update the budget table above
- [x] `dp_portal`'s `authorize` now gates `/api/v2/command` and the new
      `/api/v2/token` behind the family's `X-Dragon-Auth` / `X-DragonBreath-Auth`
      control-token scheme (presence-only until a token is set, exact match
      after). Stock `/set` has its own gate so HA-Panda-PWR keeps working: an
      Origin check (refuses cross-site browser posts) until a token is set,
      the token after
      No dedicated dc_ui settings card yet (that surface is dragonbreath-only
      today) — set/clear the token directly against `/api/v2/token` until one
      lands

No meter and no printer integration in this phase. The point is a device that
is safe to leave plugged in.

**Built, never run.** The tree compiles clean for `esp32c2` and the emitted
partition table is byte-identical to stock, but no part of it has executed on
hardware. Phase 0 comes first.

## Phase 2 — Printer awareness

The three features that justify the product.

- [ ] `dc_moonraker` as the first and only control source
- [ ] **Mid-print interlock.** Every power-off path — web, MQTT, countdown,
      schedule, button — refuses while the printer reports printing, paused, or
      heating. `dc_portal`'s `guard_operation` already has this shape for OTA and
      factory reset; the relay needs the same gate
- [ ] **Auto power-off with a cooldown gate**: print complete **and** hotend/bed
      below a threshold **and** idle for N minutes. DragonVent's `dv_policy` is
      the working template for the hysteresis
- [ ] Countdown timer (stock parity), subject to the interlock
- [ ] Idle timeout — no print for N hours → power off
- [ ] Manual override that survives reboot

## Phase 3 — Metering

- [ ] `dp_meter`: UART1 9600 8E1, `[0xA5][reg|0x80][data][~sum]` framing, the
      `0xEA` write-enable gate. Blocked on Phase 0 identifying the chip before
      the register semantics can be trusted
- [ ] Live voltage / current / power / energy / frequency in the state JSON
- [ ] **Per-print energy and cost.** Latch the kWh counter on the print-start and
      print-end edges. Configurable rate and currency
- [ ] **Draw-vs-state cross-check.** "Printing" but drawing standby current is a
      hung MCU or a thermal shutdown, and nothing without a meter can see it
- [ ] **Over-power cutoff** at a configurable ceiling
- [ ] Voltage-sag logging to `dc_evlog` — the meter samples at roughly 10 Hz
      (stock's `ele_task` polls on a 100 ms tick), so brownouts behind mystery
      printer resets become visible
- [ ] Standby-draw reporting

## Phase 4 — Integration

- [ ] **Moonraker `[power]` device.** Moonraker drives generic HTTP power
      devices, so a stable on/off/status URL shape inherits `POWER_ON` /
      `POWER_OFF` macros, `off_when_job_complete`, and `bound_service` without
      writing any of it. Confirm the current `type: http` field names against
      Moonraker's docs before fixing the URLs
- [ ] **Home Assistant MQTT Discovery** — a switch plus voltage / current /
      power / energy sensors, over `dc_mqtt`
- [ ] Split shutdown policy: cut mains, leave USB1 up for a camera or light
      (USB2 is unswitched and stays on regardless)

## Phase 5 — Lighting and the family surface

- [ ] Status LED via `dc_lighting`'s **SPI** backend — GPIO4, WS2812-style. The
      ESP32-C2 has no RMT, so the SPI path is the only one that works here, and
      `dc_lighting` includes `led_strip_rmt.h` unconditionally: expect to fix
      that upstream before it compiles for this target
- [ ] Printer-state colours reusing the family's existing lighting contract
- [ ] `dragonpwr` surface contributed to `dc_ui`: relay + USB toggles, live
      power, per-print energy, policy editing

## Deferred and non-goals

- **Panda Touch pairing over ESP-NOW.** Reconstructible — the frame format is in
  BTT's `pwr_api.md` and the LMK is in the binary — but ESP-NOW coexisting with
  Wi-Fi on a 272 KB C2, for a feature that only helps owners of a second BTT
  device, is a bad trade against the flash budget.
- **Bambu and PrusaLink sources.** Not on principle, on budget. Revisit once
  Phase 3 lands and the real numbers are known.
- **Cloud anything.** LAN only, like the rest of the family.
