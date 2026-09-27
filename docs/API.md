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
- `capabilities`: currently `power_switch`, `usb_switch`, `polling`
- `ui` descriptor

### `GET /api/v2/state`

Current Phase 1 state:

```json
{
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

Heap values are bytes. `meter` and `printer` are deliberately `null` until
those features exist so clients can distinguish "not implemented" from a real
zero reading.

### `GET /api/v2/logs`

Returns the current RAM event-log snapshot:

```json
{"entries":[{"ms":1234,"text":"..."}]}
```

### `GET /power`

Temporary DragonPWR-specific browser control page. The page itself is open; its
commands use the authenticated command route.

## Mutation routes

### `POST /api/v2/command`

Body:

```json
{"output":"mains","on":true}
```

`output` is `mains` or `usb1`. On success the response is the same shape as
`GET /api/v2/state`.

**Current limitation:** the Phase 2 printer-state power-off interlock is not
implemented yet.

### `POST /api/v2/token`

Body `{"token":"..."}`; 1–64 printable non-space characters. An empty string
clears the token.

### `POST /api/v2/restart`

Authenticated. Refused with HTTP 409 while the mains outlet is on.

### `POST /api/v2/factory-reset?confirm=factory-reset`

Authenticated, requires the exact confirmation query parameter, and is refused
while mains is on. Clears DragonPWR product settings/token and Wi-Fi credentials,
then restarts.

### `POST /api/v2/boot-inactive`

Authenticated and refused while mains is on. Verifies the other OTA slot before
selecting it.

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
compatibility zeros while `power_state` and `usb_state` are real:

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
