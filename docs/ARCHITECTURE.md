# DragonPWR architecture

DragonPWR is a product-local ESP-IDF application built on pinned dragon-core
components. Product hardware behavior stays in this repository; reusable,
board-neutral networking and management services stay in dragon-core.

## Boot order

`app_main` deliberately brings the dangerous outputs under control before
starting the network stack:

1. Re-apply the console baud rate.
2. Initialize the event-log console and event log.
3. Initialize NVS.
4. Initialize `dp_relay` so the mains and USB outputs enter known states.
5. Initialize `dp_button`.
6. Set the DragonPWR network identity.
7. Start `dc_wifi`.
8. Start `dp_portal`, which layers DragonPWR product routes onto dragon-core's
   shared portal.
9. Confirm a pending OTA image once the portal is reachable.
10. Record heap headroom.

That ordering is intentional: Wi-Fi, UI and printer integration must never be
prerequisites for driving the mains relay into its safe boot state.

## Product-local components

| Component | Responsibility |
|---|---|
| `dp_board` | Panda PWR GPIO assignments and polarity; the single board-hardware contract |
| `dp_relay` | Mains and USB1 outputs, safe initialization, readback and restore policy |
| `dp_button` | Physical-input handling; GPIO10 is the confirmed button, GPIO6 is observed but deliberately not allowed to switch mains |
| `dp_portal` | DragonPWR API v2 routes, stock-compatible routes, auth, maintenance guards and temporary `/power` UI |
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

The Phase 1 firmware drives the relay to a known state before networking starts.
The restore policy defaults to **off**.

Every operation that reboots the ESP can drop the relay, so maintenance routes
refuse to proceed while the mains outlet is on. The user must turn the outlet
off first.

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
