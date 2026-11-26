// SPDX-License-Identifier: MIT
#include "dp_meter_protocol.h"
#include <assert.h>
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
    return 0;
}
