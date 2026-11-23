# DragonPWR architecture

DragonPWR is a product-local ESP-IDF application built on pinned dragon-core
components. Product hardware behavior stays in this repository; reusable,
board-neutral networking and management services stay in dragon-core.

## Boot order

`app_main` initializes target-specific output policy before the network stack.
On V1 it drives relay and USB outputs to the chosen startup state; on V2 it
initializes the **inactive** relay driver and non-actuating power service.
Neither the V2 driver nor its startup model proves physical outlet isolation.

1. Re-apply the console baud rate.
2. Initialize the event-log console and event log.
3. Initialize NVS.
4. Initialize `dp_relay` (V1 hardware outputs; V2 inactive backend).
5. On ESP32-C3 only, start the disabled V2 PWR model's FreeRTOS polling service.
6. Initialize `dp_button`.
7. Set the DragonPWR network identity.
8. Start `dc_wifi`.
9. Start `dp_portal`, which layers DragonPWR product routes onto dragon-core's
   shared portal.
10. Confirm a pending OTA image after the startup calls return successfully;
    this does not prove that a remote client can reach the portal.
11. Record heap headroom.

That ordering is intentional: Wi-Fi, UI and printer integration must not be
prerequisites for applying startup output policy. V2 reset-time pad and relay
behavior remains unqualified.

## Product-local components

| Component | Responsibility |
|---|---|
| `dp_board` | Panda PWR GPIO assignments and polarity; the single board-hardware contract |
| `dp_relay` | V1 mains/USB1 switching, pin readback and restore; V2 inactive backend plus non-actuating state model, service and unregistered experimental GPIO7 driver candidate |
| `dp_button` | Physical-input handling; GPIO10 is the confirmed button, GPIO6 is observed but deliberately not allowed to switch mains |
| `dp_portal` | DragonPWR API v2 routes, stock-compatible routes, auth, maintenance guards and permanent product-owned `/power` UI |
| future `dp_meter` | HLW8112 transport, configuration interpretation, conversion and energy accounting |
| future printer policy | Printer-aware interlocks and automatic shutdown policy |

## dragon-core ownership

DragonPWR currently delegates shared services including:

- Wi-Fi provisioning and network lifecycle
- common portal/recovery surface
- browser OTA
- event logging
- the shared family SPA
- future printer-source clients and MQTT where they fit the ESP32-C2 budget

DragonPWR should not move board-specific relay, metering or safety policy into
dragon-core merely to reduce application code.

## Safety boundaries

### Mains relay

**V1:** The Phase 1 firmware commands the relay before networking starts;
restore defaults to **off**. The product's maintenance guard rejects restart
operations while the driver reports mains ON. That pin-level state is not a
physical contact sensor.

**V2:** The normal relay driver does not actuate outlets. A separate,
compile-time opt-in experimental GPIO7 pulse endpoint requires configured token
authentication and explicit lab acknowledgment. The V2 power service starts
OFF/disabled with **no registered GPIO callback**. It serializes software
operations and provides a lease for experimental GPIO7 ownership; C3
maintenance guards reject requests while that lease is held or model state is
enabled, faulted or unavailable. These are software coordination boundaries,
not evidence that an AC contact is open.

Before activating a V2 service driver, resolve failed-OFF retry/escalation,
provisioning-path reboot guards, centralized GPIO ownership, atomic maintenance
holds and bounded lock waits. Reset during HIGH, loaded contacts and output
feedback remain uncharacterized. See [V2 PWR integration](V2_PWR_INTEGRATION.md).

The Phase 2 print-aware interlock does not exist yet. Today a normal command can
still turn mains off while a printer is operating. The roadmap treats closing
that gap as a required product feature, not as UI behavior.

### GPIO6

Stock firmware reacts to GPIO6 transitions, but the physical source is still
unknown and the pin has no pull. A real bench session observed floating GPIO6
behavior that switched mains unexpectedly before DragonPWR stopped acting on it.
Current DragonPWR may observe/log the pin but does not let it command the relay.

### Metering

The HLW8112 is physically identified and the stock algorithm is recovered, but
live DragonPWR metering is not implemented. The future driver should treat the
chip's **observed** control-register state as authoritative in read-only bring-up
because an ESP reset does not reset the external meter IC.

See [Metering Reverse Engineering](METERING_REVERSE_ENGINEERING.md).

## Resource constraints

The ESP8684/ESP32-C2 has 272 KiB SRAM. Phase 1 was measured at roughly 68 KiB
free heap after boot, 56 KiB low-water after using the web UI, and a 58 KiB
largest block. Those numbers are part of the product architecture: features,
especially TLS-heavy printer clients, must be added one at a time and re-measured
on hardware.

See [Roadmap](ROADMAP.md) for the current flash/RAM budget and deferred sources.
