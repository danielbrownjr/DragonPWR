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
  "meter": null,
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
Neither target reports physical contact or voltage through these fields.

Heap values are bytes. `meter` and `printer` are deliberately `null` until
those features exist so clients can distinguish "not implemented" from a real
zero reading.

### `GET /api/v2/logs`

Returns the current RAM event-log snapshot:

```json
{"entries":[{"ms":1234,"text":"..."}]}
```

### `GET /power`

Permanent DragonPWR-owned control and recovery page, complementary to the shared
read-only dashboard at `/`. The page is publicly readable, but output commands,
Restart and Boot inactive slot use authenticated firmware routes. Recovery buttons use boot identities and the applicable maintenance gate.
The V1 gate uses driver-reported mains state. The V2 gate checks software model
status and the experimental GPIO7 lease; it does not establish an observed
physical mains-off state. The firmware independently enforces authorization and
the product maintenance gate.

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

Returns the stock field set. Until `dp_meter` lands, the meter fields are
compatibility zeros. `power_state` and `usb_state` are numeric controller
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

### `POST /api/v2/experimental/gpio7` (C3 lab builds only)

Only compiled with `CONFIG_DP_V2_GPIO7_EXPERIMENT=y`. Requires an actually
configured control token (not presence-only auth), the matching
`X-Dragon-Auth` header and `X-Dragon-Lab-Confirm: GPIO7-HUMAN-MONITORED`.
The JSON body is `{"level":1,"duration_ms":250}`, accepting integer level
0 or 1 and durations from 100 through 2000 ms. An exclusive software lease
blocks overlapping lab ownership. The response reports the commanded level
and duration but `physical_ac_state: null`. This route is **not** a production
power API or proof of safe reset/output isolation. See
[V2 experimental switching](V2_EXPERIMENTAL_SWITCHING.md).
