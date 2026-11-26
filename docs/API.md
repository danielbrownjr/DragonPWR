# DragonPWR HTTP API

This page documents DragonPWR's **product-local Phase 1 routes**. dragon-core
also provides shared provisioning, settings, OTA and recovery routes; those are
owned by dragon-core and are not duplicated here.

## Authorization model

Mutation routes use the `X-Dragon-Auth` / `X-DragonBreath-Auth` family
control-token scheme.

- With no token configured, product command routes require the header to be
  present, but any value is accepted. This blocks ordinary cross-site browser
  requests; it is not LAN authentication.
- After a token is configured, the header must exactly match it.
- `GET /api/v2/info`, `GET /api/v2/state`, `GET /api/v2/logs` and
  `GET /update_ele_data` remain readable.
- Stock-compatible `POST /set` is special: before a token is configured it
  uses an Origin/CSRF check so HA-Panda-PWR can still work without the custom
  header. Once a token exists, `/set` requires it too.

Set or replace a token:

```text
POST /api/v2/token
X-Dragon-Auth: <current token or any value if unset>
Content-Type: application/json

{"token":"new-token"}
```

Clear it with `{"token":""}` while authenticated with the current token.

## Read routes

### `GET /api/v2/info`

Returns product identity and UI capabilities. Current fields include:

- `product`, `display_name`
- `version` and `firmware`
- `device_id`, `boot_id`
- optional `inactive_slot`
- `capabilities`: V1 advertises `power_switch`, `usb_switch`, and `polling`.
  V2 currently advertises only `polling`.
- `ui` descriptor

### `GET /api/v2/state`

Current Phase 1 state:

```json
{
  "firmware": "dragonpwr-version",
  "device_id": "device-identifier",
  "boot_id": "boot-session-identifier",
  "outputs": {"mains": false, "usb1": false},
  "restore": "off",
  "meter": {
    "status": "unavailable",
    "voltage": null, "current": null, "power": null, "frequency": null,
    "energy": null,
    "reasons": {
      "voltage": "unavailable", "current": "unavailable",
      "power": "unavailable", "frequency": "unavailable",
      "energy": "not_implemented"
    }
  },
  "printer": null,
  "heap": {
    "free": 0,
    "min_free": 0,
    "largest_block": 0
  }
}
```

The example shows V1 controller state. On V2, `outputs.mains`, `outputs.usb1`,
and `restore` are `null` because normal output control and restore are
unavailable. V2 additionally reports a software-only control snapshot, for
example:

```json
{
  "outputs": {"mains": null, "usb1": null},
  "restore": null,
  "pwr_control": {
    "enabled": false,
    "requested_state": "off",
    "lab_gpio7_active": false,
    "physical_ac_state": null
  }
}
```

This is a **partial example**, not the entire state response. The V2
`requested_state` is `off`, `on`, `fault` or `unavailable` depending on the
model snapshot. It is **software intent**, not measured GPIO drive, contact
position or AC voltage. `lab_gpio7_active` reflects a RAM lease, not a
reliable live pulse monitor: the experimental pulse runs on the HTTP worker, so
a simultaneous HTTP state request cannot normally observe the lease held.
When the PWR service is unavailable, the lease helper conservatively reports
`true`; together with `requested_state: "unavailable"`, this means unknown
service state rather than proof of an active lease.
Neither target reports physical contact or voltage through these fields.

Heap values are bytes. V2 always returns `meter: null`; printer remains null
on both targets.

#### V1 meter telemetry

The HLW8112 is read-only. No meter reset, configuration or calibration writes
are performed. The object contains:

| Field | Meaning |
|---|---|
| status | unavailable before the first poll or if startup fails; invalid after a failed transaction or changing configuration; stale when a successful snapshot is older than 3 seconds; fresh after a complete validated sweep |
| voltage | V RMS or null |
| current | A RMS or null |
| power | signed active W or null |
| frequency | Hz or null |
| energy | always null in this increment |
| reasons | keyed reason strings for null measurement fields |
| raw | only present for fresh sweeps; unsigned register values and factory coefficients |

Fresh describes transport/configuration consistency, not metrological validity.
The driver reads control registers before and after the sweep, but the chip's
individual registers are sampled sequentially, not latched simultaneously.
The raw keys are syscon, emucon, hfconst, emucon2, voltage, current, power,
voltage_coef, current_coef, power_coef and frequency. Raw power is an unsigned
32-bit representation of a signed two's-complement register.

Scaling requires observed ADC enable bits, factory-calibrated gain settings
and coefficients other than 0 or 0xFFFF. Current PGA encodings 4–7 all
represent 16x. DC_MODE and invalid VrefSel suppress scaling; disabled voltage
or current high-pass filters suppress the corresponding RMS path and power.
Power additionally requires PARUN and qualified frequency. Frequency requires
ZxEN and WaveEN, nonzero raw frequency and 45–65 Hz. With frequency acquisition
enabled, the stock publication gate (voltage at least 10 V and qualified
frequency) applies to voltage/current/power. When frequency is explicitly
disabled by configuration, qualified RMS voltage and current may be reported
with voltage at least 10 V; power and frequency remain null. At documented
reset defaults (SYSCON 0x0A04, EMUCON 0x0000, EMUCON2 0x0001), RMS telemetry
is therefore possible but active power and frequency are unavailable. This
is a datasheet-derived expectation, not a hardware acceptance result. Current below 0.01 A is reported as
zero, with power also zero when otherwise qualified. BTT board factors
(K1=1, K2=0.51) are recovered from stock firmware, not reference-calibrated.

Reasons include adc_disabled, unsupported_gain, zero_coefficient,
uncalibrated_coefficient, invalid_reference, dc_mode, hpf_disabled,
parun_disabled, frequency_disabled, no_frequency, frequency_out_of_range,
voltage_unqualified, voltage_below_10v, frequency_unqualified, unavailable,
invalid, stale and not_implemented. A numeric zero is distinct from null.
Polls sleep one second between sweeps; the 80 ms per-read timeout is rounded
up to ticks. No energy or clear-on-read interrupt registers are accessed.
No metering-based power policy is implemented.

The legacy stock route still uses compatibility zeros, not measurements;
clients requiring valid telemetry must use the native state route. Hardware
accuracy, memory headroom and cold-start meter behavior remain unqualified.

### `GET /api/v2/logs`

Returns the current RAM event-log snapshot:

```json
{"entries":[{"ms":1234,"text":"..."}]}
```

### `GET /power`

Permanent DragonPWR-owned control and recovery page, complementary to the
shared read-only dashboard at `/`. The page is publicly readable, but output
commands, Restart and Boot inactive slot use authenticated firmware routes.

On `/power`, the Restart and Boot inactive slot buttons require matching boot
identities, `outputs.mains === false`, and the `power_switch` capability.
They therefore remain **disabled on V2**, where mains state is `null` and
`power_switch` is absent. An unknown output state is not permission to
reboot through the UI.

The firmware routes separately enforce authorization and target-specific
maintenance guards: V1 checks driver-reported mains state, while V2 checks
PWR model state and the GPIO7 lab lease. Neither check establishes physical
mains-off state.

## Mutation routes

### `POST /api/v2/command`

Body:

```json
{"output":"mains","on":true}
```

`output` is `mains` or `usb1`. On success the response is the same shape as
`GET /api/v2/state`.

On V2, this route still refuses output switching through the inactive relay
backend. It does not expose the PWR model's ON operation. **Current limitation:**
the Phase 2 printer-state power-off interlock is not implemented yet.

### `POST /api/v2/token`

Body `{"token":"..."}`; 1–64 printable non-space characters. An empty string
clears the token.

### `POST /api/v2/restart`

Authenticated. On V1 refused with HTTP 409 when the relay driver reports mains
ON. On V2 refused when the PWR service is enabled, active, faulted, holding a
GPIO7 lab lease, or unavailable; this is not a physical-contact measurement.

### `POST /api/v2/factory-reset?confirm=factory-reset`

Authenticated, requires the exact confirmation query parameter, and uses the
same target-specific maintenance guard described for restart. Clears DragonPWR product settings/token and Wi-Fi credentials,
then restarts.

### `POST /api/v2/boot-inactive`

Authenticated and subject to the same target-specific maintenance guard.
Verifies the other OTA slot before selecting it.

## Stock-compatible routes

These exist for clients written against BIGTREETECH's stock API, especially
HA-Panda-PWR.

### `POST /set`

Form body, not JSON:

```text
power=1&usb=0
```

Recognized keys are `power` and `usb`; zero means off, any other first
character means on. Returns plain text `ok` on success.

### `GET /update_ele_data`

Returns the stock field set. In this increment, the meter fields remain
compatibility zeros (not measurements). `power_state` and `usb_state` are numeric controller
states on V1 and `null` on V2:

- `countdown_state`
- `auto_poweroff`
- `countdown`
- `voltage`
- `current`
- `power`
- `power_state`
- `usb_state`
- `ele`

The recovered stock metering field semantics are documented in
[Metering Reverse Engineering](METERING_REVERSE_ENGINEERING.md).

## Experimental routes (C3 lab builds only)

### `POST /api/v2/experimental/power` (C3 lab build only)

The opt-in V2 laboratory image exposes an **explicitly human-monitored**
software ON/OFF control using the existing GPIO7 driver and independent
deadline scheduler. The normal C3 build does not compile this route or
advertise `experimental_power`; it continues to reject ordinary `/set` and
`/api/v2/command` switching.

Requires a **configured** control token in `X-Dragon-Auth` or
`X-DragonBreath-Auth` and `X-Dragon-Lab-Confirm: GPIO7-HUMAN-MONITORED`.
The local `/power` page exposes a separate, clearly labeled laboratory
button only when `experimental_power` is advertised. Do not connect an
unattended printer or load.

- `{"on":true,"duration_ms":300000}` requests GPIO7 HIGH for at most
  300,000 ms (five minutes). Duration is required and must be an integer
  between 100 and 300,000 ms. Repeated ON while already ON is refused
  rather than silently extending the deadline.
- `{"on":false}` requests release of GPIO7 to input mode, including
  after model FAULT or an expired deadline.
- A successful response is the usual V2 software state snapshot,
  retaining `outputs.mains: null`, `physical_ac_state: null`. A driver
  error may latch software FAULT; neither success nor expiry proves the
  contacts are open.

This is a **lab-only power button candidate**, not production-qualified
output switching. A missed poll, stuck callback, reset while HIGH, shared
provisioning reboot, or lost GPIO7 lease can still leave physical state
unknown. Use a human-monitored, independently disconnectable setup only,
subject to separate operator authorization.



### `POST /api/v2/experimental/gpio7` (C3 lab builds only)

Only compiled with `CONFIG_DP_V2_GPIO7_EXPERIMENT=y`. Requires an actually
configured control token (not presence-only auth), a matching `X-Dragon-Auth` or `X-DragonBreath-Auth` header and `X-Dragon-Lab-Confirm: GPIO7-HUMAN-MONITORED`.
The JSON body is `{"level":1,"duration_ms":250}`, accepting integer level
0 or 1 and durations from 100 through 2000 ms. An exclusive software lease
blocks overlapping lab ownership. The response reports the commanded level
and duration but `physical_ac_state: null`. This route is **not** a production
power API or proof of safe reset/output isolation. See
[V2 experimental switching](V2_EXPERIMENTAL_SWITCHING.md).
