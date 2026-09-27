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

## The constraints that shape the phases

RAM first, then flash. The ESP32-C2 has **272 KB of SRAM** against the C3's
400 KB, and RAM ran out long before flash did.

| | |
|---|---|
| App slot (stock partition table) | **1280 K** |
| **DragonPWR end of Phase 1, measured** | **793 K — 38 % of the slot free** |
| DragonStatus v1.0.0 OTA image (ESP32-C3) | 1.15 MB |
| DragonVent v0.5.9 OTA image (ESP32) | 1.20 MB |
| **Heap free after boot, measured on hardware** | **68 KB** (largest block 58 KB) |
| **Heap low-water after using the web UI** | **56 KB** |

The image size is from `idf.py build` for `esp32c2` (ESP-IDF v5.3.1 here, v5.3.5
on the bench machine; the two differ by a few K). The heap numbers are read off
a real unit through `/api/v2/state` and the `/power` footer: free right now,
the low-water mark since boot, and the largest contiguous block.

Before the RAM tuning in `sdkconfig.defaults` (Wi-Fi out of IRAM, fewer Wi-Fi
buffers, A-MPDU off, at most four HTTP sockets) the same unit booted with 47 KB
free and fell to **4 KB** after a minute of browsing - one burst from an
allocation failure, which on this chip means a reset and a dropped relay.
Re-measure at the end of every phase, on hardware, and keep this table honest.

Consequences:

- `dc_source` only *starts* the selected printer client, but every linked client
  still costs flash and static RAM. Add sources one at a time and re-measure.
- **Bambu does not fit today.** LAN MQTT over TLS peaks at roughly 50-60 KB
  (a 16 KB report buffer in `dc_bambu`, 30-40 KB for the handshake, MQTT),
  against a 56 KB low-water mark. The known ways to make room are shrinking
  `dc_evlog`'s fixed 16 KB console ring upstream in dragon-core and tuning
  mbedTLS for low memory (dynamic buffers, a smaller outgoing record). Until
  both land and the numbers are re-measured, Bambu stays off this chip.

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

Meanwhile a `dragonpwr` device lands on DragonBreath's dashboard, and two
stopgaps cover the gap:

- **`/power`** (`components/dp_portal/power.html`): Outlet and USB 1 toggles,
  live state and heap, in the family look and token transport. Delete it once
  `dc_ui` has a DragonPWR view.
- **DragonBreath's Settings contract is served**, so the shared Settings screen
  works: event log (`/api/v2/logs`), Maintenance (firmware, device ID, boot ID,
  inactive slot) and its Restart, Factory reset and Boot inactive slot buttons.
  The dashboard itself still shows DragonBreath's chamber readout, all dashes.

## Phase 0 — Bench confirmation

Everything in [HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md) was derived from the
stock binary; these items check it against a board. What is left needs the case
open, and a spare unit has been set aside for the teardown so the working one
stays sealed.

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
      so what drives this pin is genuinely unknown. On the teardown unit: follow
      its trace
- [x] Photograph the metering IC and match it against the register map —
      it is an **HLW8112** (package marking, 2026-09-27 teardown), and the stock
      framing, `0xEA` write gate and init registers match its datasheet. See
      docs/HARDWARE_ANALYSIS.md
- [ ] On the teardown unit, trace which ESP pins reach the HLW8112 (UART1,
      GPIO2/3, per the stock firmware) and what scales its current input: stock
      uses channel A only, and the sensing element looks like a current
      transformer rather than a shunt, which is still to be confirmed
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
      the token after. There is no token card in the UI yet (`dc_ui` shows it
      to dragonbreath only), so set and clear it against `/api/v2/token`
- [x] OTA from the browser that survives a power cycle, confirmed on the bench.
      A new image confirms itself once the portal is up, so a bad one rolls
      back - **but only on a bootloader built with rollback**. The bench unit's
      is not (every boot logs `image new`), so it needs one serial flash of a
      current build before that protection is real. Every boot logs its slot,
      OTA state and reset reason to the event log
- [x] `/power` control page, and DragonBreath's Settings contract (event log,
      IDs, inactive slot, Restart / Factory reset / Boot inactive slot) - see
      the upstream section above
- [x] Heap reporting, and RAM tuning that took the low-water mark from 4 KB
      to 56 KB - see the constraints table

No meter and no printer integration in this phase. The point is a device that
is safe to leave plugged in.

**Bench-verified.** Everything above has run on a real Panda PWR and is installed
by OTA from the browser. Not yet exercised: a factory reset from the Maintenance
card, and booting the inactive slot.

## Phase 2 — Printer awareness

The three features that justify the product.

- [ ] A printer source. `dc_moonraker` for Klipper printers is the one that fits:
      plain HTTP, no TLS. The bench printer is a Bambu X1C (X1plus), and
      `dc_bambu` does not fit in RAM yet - see the constraints section
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
      `0xEA` write-enable gate. The chip is identified (HLW8112), and the stock
      registers, widths, coefficients and conversions are recovered in
      docs/METERING_REVERSE_ENGINEERING.md. Energy and frequency need stock's init writes;
      voltage, current and power work from the chip's reset defaults
- [ ] Live voltage / current / power / energy / frequency in the state JSON
- [ ] **Per-print energy and cost.** Latch the kWh counter on the print-start and
      print-end edges. Configurable rate and currency
- [ ] **Draw-vs-state cross-check.** "Printing" but drawing standby current is a
      hung MCU or a thermal shutdown, and nothing without a meter can see it
- [ ] **Over-power cutoff** at a configurable ceiling
- [ ] Voltage-sag logging to `dc_evlog`, so brownouts behind mystery printer
      resets become visible. Stock polls about once a second and sets the
      HLW8112's averaged registers to update at 3.4 Hz; the chip's own sag
      detection (`SAGEN`, off in stock) is worth evaluating for this
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
- **Bambu and PrusaLink sources.** Not on principle, on budget: Bambu's TLS
  client needs more heap than this chip has spare (see the constraints
  section, with the measured numbers). Revisit after the upstream console ring
  and mbedTLS changes, and re-measure on hardware.
- **Cloud anything.** LAN only, like the rest of the family.
