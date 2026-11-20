# HTTP session-pressure diagnostics

This experimental, read-only DragonPWR endpoint helps determine whether
four-client HTTP session pressure correlates with browser-side
`httpd_sock_err: ... 104` warnings. It does **not** classify LRU evictions:
ESP-IDF's session-close callback does not report why a session closed.

## Querying

With the device powered via programming USB only and IEC disconnected:

```sh
curl -fsS -H 'X-Dragon-Auth: web' \
  http://192.168.8.151/api/v2/diag/http | jq .
```

The `web` sentinel works only when no control token is configured. If a
token is configured, use the configured token in the header instead.
Do not commit or share tokens.

`sessions.active` counts live TCP sessions at the moment the endpoint runs,
including the diagnostic request itself. `peak` is the largest observed
simultaneous session count since the HTTP portal started; `opened` and
`closed` are cumulative socket callback counts. `opened_at_limit` counts
opens when the active count reaches the configured limit (4).
`closed_at_limit` counts closes while the active count was at the limit.
**Neither counter proves that LRU evicted a session.** A diagnostic sample
taken while all four browser sessions are occupied can itself force an LRU
close before the diagnostic request is accepted. That sample can increment
both capacity counters and produce a contemporaneous browser reset/104
warning. Treat capacity-counter changes at the sampling instant as
measurement-induced until independently corroborated; don't blindly
subtract one, since concurrent traffic can cause additional closes.

`heap.free`, `heap.min_free`, and `heap.largest_block` measure
8-bit-capable heap in bytes. The minimum is system-wide since boot.

## Suggested acceptance matrix

Capture the endpoint before loading the dashboard, immediately after loading,
after a few minutes of polling, and after closing all browser tabs. Sample
with the same curl command each time. Compare with UART serial warnings and
firmware boot ID. Avoid high-frequency diagnostic polling, which itself
opens sessions and can change the measured traffic.

Compare an unchanged four-client build with any later five-client experiment
only under comparable browser traffic, C2/C3 hardware, and boot/network setup.
Keep V2 mains/USB outputs disabled. Do not alter the limit without evidence
of better request reliability **and** acceptable C2 peak heap headroom.

This patch intentionally does not change `max_open_sockets`, LRU policy,
timeouts, the SPA polling cadence, or relay behavior.
