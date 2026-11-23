# DragonPWR V2 experimental GPIO7 pulse firmware

**Status:** the opt-in GPIO7 laboratory firmware was merged in
[PR #42](https://github.com/danielbrownjr/DragonPWR/pull/42) at
`cc5fc96ba3af5824ea1c50f79c7dc7fc4b8b5e30`. It combines the
previously separated Stage A/B experiment for
[issue #18](https://github.com/danielbrownjr/DragonPWR/issues/18).
Subsequent [PR #43](https://github.com/danielbrownjr/DragonPWR/pull/43)
added an exclusive software GPIO7 lab lease, but **did not connect a normal
power driver**. Merge acceptance is not electrical qualification or a
production release. See [V2 PWR integration](V2_PWR_INTEGRATION.md) for the
current software and hardware acceptance boundaries.

## Why GPIO7?

Static analysis of the original V2 firmware indicates GPIO7 HIGH commands AC ON at the MCU, but stock applies the queued level from a GPIO18 falling-edge handler. GPIO18 also overlaps the C3 USB Serial/JTAG D− pad. Neither the origin/purpose of that edge nor external relay topology is verified.

Dan reported approximately **240 V AC at the receptacle on initial application of mains with stock firmware**. That external DVM observation supersedes an earlier mistaken DC-range reading, but does not establish whether GPIO7 directly controls the contacts or the timing of boot-time switching. This firmware **deliberately tests an arbitrary GPIO7 level write without claiming equivalence to stock firmware's edge-based scheme**.

## The single experimental candidate

- **Target:** ESP32-C3 V2 only. The normal `sdkconfig.v2.defaults` keeps output drivers inactive. A distinct `sdkconfig.v2.lab.defaults` opts into `CONFIG_DP_V2_GPIO7_EXPERIMENT=y`. V1 remains on its original driver. CI builds and uploads a separately named **experimental** C3 artifact.
- **Startup:** no GPIO7 writes, GPIO18/19 configuration, USB enable writes or local button configuration from the experimental module at boot. The regular C3 relay driver remains inactive.
- **Command entry point:** `POST /api/v2/experimental/gpio7`, present **only** in the opt-in build. Requires an actually configured DragonPWR `X-Dragon-Auth` token (presence-only auth is explicitly rejected), a separate `X-Dragon-Lab-Confirm: GPIO7-HUMAN-MONITORED` header, and JSON `{"level":1,"duration_ms":250}` or level 0. Accepts only 0/1 and integer 100–2000 ms; no toggle, continuous/latched-on state or automatic restore.
- **Execution:** capture IO_MUX/driver diagnostic state, explicitly select the GPIO7 pad function (the reset MTDO/JTAG function is not necessarily GPIO), preload the requested output register level, switch GPIO7 to output for the interval, then switch back to input. The IO_MUX and preload dumps run **before** output enable and the release dump runs **after** output disable, avoiding synchronous console I/O during the timed pulse; therefore they do **not** capture OutputEn=1 during the drive. The requested duration remains a nominal tick-based delay, not a measured physical switching time. Return physical AC state as **null/unknown** regardless of success. A CPU reset during the pulse can terminate the command unpredictably; releasing to high impedance does **not** prove the outlet is off.
- **Other routes:** Existing `POST /api/v2/command`, legacy `POST /set`, provisioning restore, physical SET, and the shared GUI stay **inactive** for V2; USB-A/C control is not implemented. No experimental capability is advertised in `/api/v2/info`.
- **Environment:** Set the control token **before any connection to a shared network**. Initial token provisioning is presence-only: when no token exists, any associated client can set one through the token endpoint. Use a physically isolated bench AP/network for first configuration, do not expose the lab image to shared Wi-Fi, and verify the configured token before activating mains. HTTP on a trusted isolated bench network only. This route is not cryptographically secure against traffic sniffing/MITM; the second header is a deliberate-acknowledgment check, not a second secret. Power must not be connected to an unattended printer.
- **Observable output:** One HTTP response after the bounded interval, showing the level written and duration, with no claimed relay/contact/voltage readback. DVM readings and timing must be recorded separately.

## Review, build and measurement workflow

1. Review GPIO driver, generated C3 config, route token enforcement, portal bypasses, all fail/restart paths, default/experimental linker separation, preservation of ROM recovery and C2 functionality. Have an independent reviewer verify the **exact PR head** and native IDF 5.3.5 C2/C3/lab build artifacts.
2. Run the host mock test `python3 scripts/test_v2_gpio7_experiment.py`, CI default C3 inactive build, C2 build, and CI lab build. These prove only code behavior and build wiring, not electrical behavior.
3. Before flashing, confirm the preexisting private factory 4 MiB image, current device state, any NVS preservation, exact flash/partition options and a specific recovery procedure. A stock reflash has **not** been successfully exercised. Verify binary digest and experimental config before using it.
4. The **separately authorized human-monitored test** uses the sealed device and external mains-rated DVM/test fixture. Arrange an operator-controlled upstream mains disconnect (appropriately rated, protected switch or switched supply) within reach and independent of GPIO7, Wi-Fi, and the firmware. Record outlet state **before the first pulse** and between pulses. Record idle AC voltage, the precise pulse time and level, outlet response and recovery after the pulse. A DVM may miss subsecond events; a voltage reading is not evidence of safe contact or protection behavior. Treat high-impedance release as **unknown outlet state**, and rely on the upstream disconnect if the outlet remains energized.
5. Stop on unexpected switch state, reset, GPIO ownership conflict, loss of network/ROM access, inability to recover, or a fixture problem. There is no assumption that input mode equals relay OFF.

**Merge scope:** PR #42 did not itself perform a flash, energized test,
firmware release/tag or stock restore. Separately recorded operator bench
observations of unloaded outlet voltage exist, but do not qualify physical
contact closure, loaded switching, reset behavior or certification. This
source document does not authorize hardware operations.

## Important caveat: experimental by design

The pulse mechanism intentionally does **not** replicate stock GPIO18 edge timing. GPIO7 LOW may also be electrically meaningful, despite static evidence that HIGH is the stock commanded-ON level. A DVM-based external test can answer whether the receptacle responds, but not identify isolation topology, zero-cross behavior or submillisecond relay transients. The experiment must not be generalized to production switching without further evidence.
