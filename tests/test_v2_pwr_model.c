// Host-only model tests: cc -std=c11 -Wall -Wextra -Werror -pedantic -I components/dp_relay/include components/dp_relay/dp_v2_pwr_model.c tests/test_v2_pwr_model.c -o /tmp/test_v2_pwr_model && /tmp/test_v2_pwr_model
#include "dp_v2_pwr_model.h"
#include <assert.h>
#include <stdio.h>
typedef struct { int high,low; bool bad_high,bad_low; } probe_t;
static bool drive(bool high,void *ctx) {
    probe_t *p=ctx;
    if (high) {p->high++; return !p->bad_high;}
    p->low++; return !p->bad_low;
}
int main(void) {
    dp_v2_pwr_model_t m; probe_t p={0};
    dp_v2_pwr_model_init(&m);
    assert(m.phase==DP_V2_PWR_OFF && !m.enabled);
    assert(dp_v2_pwr_model_set(&m,true,1,100)==DP_V2_PWR_UNAVAILABLE);
    assert(dp_v2_pwr_model_set(&m,false,1,0)==DP_V2_PWR_OK);
    assert(dp_v2_pwr_model_enable(&m,drive,&p,2000)==DP_V2_PWR_OK && p.low==1);
    assert(dp_v2_pwr_model_set(&m,true,10,0)==DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_model_set(&m,true,10,2001)==DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_model_set(&m,true,10,2000)==DP_V2_PWR_OK && m.deadline_ms==2010);
    assert(dp_v2_pwr_model_set(&m,true,20,2000)==DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_model_poll(&m,2009)==DP_V2_PWR_OK && m.phase==DP_V2_PWR_ON);
    assert(dp_v2_pwr_model_poll(&m,2010)==DP_V2_PWR_OK && m.phase==DP_V2_PWR_OFF && p.low==2);
    assert(dp_v2_pwr_model_set(&m,true,UINT64_MAX,10)==DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_model_set(&m,true,3000,100)==DP_V2_PWR_OK);
    assert(dp_v2_pwr_model_set(&m,false,3001,0)==DP_V2_PWR_OK);
    assert(dp_v2_pwr_model_set(&m,false,3002,0)==DP_V2_PWR_OK);
    p.bad_high=true;
    assert(dp_v2_pwr_model_set(&m,true,3005,100)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT && !m.deadline_ms);
    assert(dp_v2_pwr_model_set(&m,true,3006,100)==DP_V2_PWR_DRIVER_ERROR);
    assert(dp_v2_pwr_model_set(&m,false,3007,0)==DP_V2_PWR_OK);
    assert(m.phase==DP_V2_PWR_OFF);
    assert(dp_v2_pwr_model_disable(&m)==DP_V2_PWR_OK && !m.enabled);
    p.bad_high=false; p.bad_low=true;
    assert(dp_v2_pwr_model_enable(&m,drive,&p,2000)==DP_V2_PWR_DRIVER_ERROR);
    assert(!m.enabled && m.phase==DP_V2_PWR_FAULT);
    assert(m.drive == drive); // failed initial OFF retains recovery callback
    const int before_retry = p.low;
    assert(dp_v2_pwr_model_disable(&m)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT && p.low==before_retry+1);
    assert(dp_v2_pwr_model_set(&m,false,4000,0)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT);
    p.bad_low=false;
    assert(dp_v2_pwr_model_disable(&m)==DP_V2_PWR_OK);
    assert(m.phase==DP_V2_PWR_OFF && !m.enabled);
    // Defensive invariant if a corrupted/legacy model has no callback.
    dp_v2_pwr_model_init(&m);
    m.phase=DP_V2_PWR_FAULT;
    assert(dp_v2_pwr_model_disable(&m)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT);
    puts("dp_v2_pwr_model: PASS");
}
