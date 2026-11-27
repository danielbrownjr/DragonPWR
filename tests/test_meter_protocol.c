// SPDX-License-Identifier: MIT
#include "dp_meter_protocol.h"
#include <assert.h>
#include <math.h>
typedef struct { unsigned calls, fail_at; bool change; } fake_t;
static bool fake_read(void *ctx, uint8_t reg, size_t width, uint32_t *value)
{
    fake_t *f = ctx;
    uint8_t req[2];
    assert(dp_meter_request(reg, width, req));
    if (++f->calls == f->fail_at) return false;
    *value = (reg == 0 && f->change && f->calls > 11) ? 1u : reg;
    return true;
}
typedef struct {
    unsigned step;
    bool flush_fail, send_fail, extra;
    int reply_count;
    uint8_t checksum;
} mock_io_t;

static bool mock_flush(void *ctx)
{
    mock_io_t *m = ctx;
    assert(m->step++ == 0);
    return !m->flush_fail;
}
static bool mock_send(void *ctx, const uint8_t *bytes, size_t count)
{
    mock_io_t *m = ctx;
    assert(m->step++ == 1 && count == 2);
    assert(bytes[0] == 0xa5 && bytes[1] == 0x26);
    return !m->send_fail;
}
static int mock_receive(void *ctx, uint8_t *bytes, size_t count, unsigned ms)
{
    mock_io_t *m = ctx;
    if (m->step++ == 2) {
        assert(count == 4 && ms == 80);
        bytes[0] = 1; bytes[1] = 2; bytes[2] = 3; bytes[3] = m->checksum;
        return m->reply_count;
    }
    assert(m->step == 4 && count == 1 && ms == 5);
    if (m->extra) bytes[0] = 0xff;
    return m->extra ? 1 : 0;
}
int main(void)
{
    uint8_t req[2];
    uint32_t value = 123;
    assert(dp_meter_request(0x26, 3, req));
    assert(req[0] == 0xa5 && req[1] == 0x26);
    const uint8_t good[] = {0x01, 0x02, 0x03, 0x2e};
    assert(dp_meter_decode(0x26, 3, good, sizeof(good), &value));
    assert(value == 0x010203);
    assert(!dp_meter_decode(0x26, 3, good, 3, &value));
    assert(!dp_meter_decode(0x24, 3, good, 4, &value));
    assert(value == 0x010203);
    assert(!dp_meter_request(0xea, 2, req));
    assert(!dp_meter_request(0x42, 2, req));
    assert(!dp_meter_request(0x28, 3, req));
    assert(!dp_meter_request(0x26, 4, req));
    assert(!dp_meter_request(0xa6, 3, req));
    assert(!dp_meter_request(0x26, 0, req));
    assert(!dp_meter_decode(0x26, 3, NULL, 4, &value));
    const uint8_t corrupt[] = {1, 2, 3, 0x2f};
    assert(!dp_meter_decode(0x26, 3, corrupt, 4, &value));
    assert(value == 0x010203);
    // Synthetic register vectors using the recovered BTT equations.
    dp_meter_snapshot_t s = {
        .status = DP_METER_FRESH, .syscon = 0x0a04, .emucon = 0x0181,
        .emucon2 = 0x046d, .voltage_raw = 4194304, .voltage_coef = 6120,
        .current_raw = 4194304, .current_coef = 2000,
        .power_raw = 0x40000000, .power_coef = 102,
        .frequency_raw = 7457
    };
    dp_meter_values_t v = dp_meter_values(&s);
    assert(!v.voltage_reason && fabsf(v.voltage - 120) < 0.001f);
    assert(!v.current_reason && fabsf(v.current - 1) < 0.001f);
    assert(!v.power_reason && fabsf(v.power - 100) < 0.001f);
    assert(!v.frequency_reason && fabsf(v.frequency - 60) < 0.01f);
    s.power_raw = 0xc0000000;
    v = dp_meter_values(&s);
    assert(!v.power_reason && fabsf(v.power + 100) < 0.001f);
    s.current_raw = 0x800000;
    v = dp_meter_values(&s);
    assert(!v.current_reason && v.current == 0 && v.power == 0);
    s.current_raw = 4194304;
    s.syscon ^= 1u << 9;
    v = dp_meter_values(&s);
    assert(v.current_reason && v.power_reason && !v.voltage_reason);
    for (uint32_t gain = 4; gain <= 7; ++gain) {
        s.syscon = 0x0a00u | gain;
        v = dp_meter_values(&s);
        assert(!v.current_reason && !v.power_reason);
    }
    s.syscon = 0x0a03;
    assert(dp_meter_values(&s).current_reason);
    s.syscon = 0x0a04;
    s.power_coef = 0;
    assert(dp_meter_values(&s).power_reason);
    s.power_coef = 102;
    s.emucon = 0;
    assert(dp_meter_values(&s).power_reason);
    s.emucon = 0x0181;
    s.emucon = 0;
    s.emucon2 = 1;
    v = dp_meter_values(&s);
    assert(!v.voltage_reason && !v.current_reason && v.power_reason && v.frequency_reason);
    assert(fabsf(v.voltage - 120) < 0.001f && fabsf(v.current - 1) < 0.001f);
    s.emucon = 0x0181;
    v = dp_meter_values(&s);
    assert(!v.voltage_reason && !v.current_reason && v.power_reason && v.frequency_reason);
    s.emucon2 = 0;
    v = dp_meter_values(&s);
    assert(v.frequency_reason && v.voltage_reason && v.current_reason && v.power_reason);
    s.emucon2 = 0x046d;
    s.frequency_raw = 0;
    assert(dp_meter_values(&s).frequency_reason);
    s.frequency_raw = 7457;
    s.voltage_coef = 0xffff;
    assert(dp_meter_values(&s).voltage_reason);
    s.voltage_coef = 6120;
    s.current_coef = 0xffff;
    assert(dp_meter_values(&s).current_reason);
    s.current_coef = 2000;
    s.power_coef = 0xffff;
    assert(dp_meter_values(&s).power_reason);
    s.power_coef = 102;
    s.emucon = 0x0181 | (1u << 9);
    assert(dp_meter_values(&s).voltage_reason);
    s.emucon = 0x0181 | (1u << 4);
    assert(dp_meter_values(&s).voltage_reason);
    s.emucon = 0x0181 | (1u << 5);
    assert(dp_meter_values(&s).current_reason);
    s.emucon = 0x0181;
    s.status = DP_METER_INVALID;
    assert(dp_meter_values(&s).voltage_reason);
    s.status = DP_METER_FRESH;
    assert(!dp_meter_values(&s).voltage_reason);
    assert(dp_meter_freshness(DP_METER_FRESH, 100, 3000100) == DP_METER_FRESH);
    assert(dp_meter_freshness(DP_METER_FRESH, 100, 3000101) == DP_METER_STALE);
    assert(dp_meter_freshness(DP_METER_INVALID, 100, 3000101) == DP_METER_INVALID);
    assert(dp_meter_freshness(DP_METER_UNAVAILABLE, 0, 3000101) == DP_METER_UNAVAILABLE);
    s.status = DP_METER_STALE;
    assert(dp_meter_values(&s).power_reason);
    mock_io_t mock = {.reply_count=4, .checksum=0x2e};
    const dp_meter_io_t io = {.ctx=&mock, .flush=mock_flush,
        .send=mock_send, .receive=mock_receive};
    assert(dp_meter_transaction(&io, 0x26, 3, &value) && value == 0x010203);
    mock = (mock_io_t){.reply_count=0};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value) && mock.step == 3);
    mock = (mock_io_t){.reply_count=2};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value));
    mock = (mock_io_t){.reply_count=4, .checksum=0x2f};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value));
    mock = (mock_io_t){.reply_count=4, .checksum=0x2e, .extra=true};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value));
    mock = (mock_io_t){.flush_fail=true};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value) && mock.step == 1);
    mock = (mock_io_t){.send_fail=true};
    assert(!dp_meter_transaction(&io, 0x26, 3, &value) && mock.step == 2);
    assert(value == 0x010203);
    mock = (mock_io_t){.reply_count=4, .checksum=0x2e};
    assert(dp_meter_transaction(&io, 0x26, 3, &value));
    fake_t fake = {0};
    dp_meter_sweep(&s, fake_read, &fake);
    assert(s.status == DP_METER_FRESH && fake.calls == 15);
    for (unsigned i = 1; i <= 15; ++i) {
        fake = (fake_t){.fail_at = i};
        dp_meter_sweep(&s, fake_read, &fake);
        assert(s.status == DP_METER_INVALID && fake.calls == i);
        assert(dp_meter_values(&s).voltage_reason);
        fake = (fake_t){0};
        dp_meter_sweep(&s, fake_read, &fake);
        assert(s.status == DP_METER_FRESH);
    }
    fake = (fake_t){.change = true};
    dp_meter_sweep(&s, fake_read, &fake);
    assert(s.status == DP_METER_INVALID);
    dp_meter_tx_diag_t tx;
    mock = (mock_io_t){.reply_count=4, .checksum=0x2f};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_CHECKSUM && tx.received == 4 &&
           tx.reply[3] == 0x2f && tx.expected_checksum == 0x2e);
    mock = (mock_io_t){.reply_count=4, .checksum=0x2e, .extra=true};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_TRAILING && tx.guard_byte == 0xff);
    mock = (mock_io_t){.reply_count=0};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_TIMEOUT && !tx.guard_attempted);
    mock = (mock_io_t){.reply_count=-1};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_RX_ERROR);
    mock = (mock_io_t){.reply_count=2};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_LENGTH);
    mock = (mock_io_t){.flush_fail=true};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_FLUSH);
    mock = (mock_io_t){.send_fail=true};
    assert(!dp_meter_transaction_observed(&io, 0x26, 3, &value, &tx));
    assert(tx.result == DP_METER_TX_SEND);
    assert(!dp_meter_transaction_observed(&io, 0x42, 2, &value, &tx));
    assert(tx.result == DP_METER_TX_ARGUMENT);
    dp_meter_sweep_diag_t diag;
    for (unsigned i = 1; i <= 15; ++i) {
        fake = (fake_t){.fail_at=i};
        dp_meter_sweep_observed(&s, fake_read, &fake, &diag);
        assert(diag.valid_mask == (1u << (i-1))-1 && !diag.mismatch_mask);
        assert(s.status == DP_METER_INVALID && fake.calls == i);
    }
    fake = (fake_t){.change=true};
    dp_meter_sweep_observed(&s, fake_read, &fake, &diag);
    assert(diag.valid_mask == 0x7fff && diag.mismatch_mask == 1);
    assert(s.status == DP_METER_INVALID && diag.reread[0] == 1);
    fake = (fake_t){0};
    dp_meter_sweep_observed(&s, fake_read, &fake, &diag);
    assert(diag.valid_mask == 0x7fff && !diag.mismatch_mask && s.status == DP_METER_FRESH);
    return 0;
}
