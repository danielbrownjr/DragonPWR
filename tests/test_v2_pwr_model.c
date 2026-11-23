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
    // Deadline OFF failure retries a finite number of scheduler polls,
    // remaining FAULT until the driver actually accepts OFF.
    dp_v2_pwr_model_init(&m);
    p=(probe_t){0};
    assert(dp_v2_pwr_model_enable(&m,drive,&p,200)==DP_V2_PWR_OK);
    assert(dp_v2_pwr_model_set(&m,true,100,100)==DP_V2_PWR_OK);
    p.bad_low=true;
    assert(dp_v2_pwr_model_poll(&m,199)==DP_V2_PWR_OK && m.phase==DP_V2_PWR_ON);
    assert(dp_v2_pwr_model_poll(&m,200)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT);
    assert(m.deadline_off_retries_remaining==DP_V2_PWR_DEADLINE_OFF_RETRIES);
    const int deadline_off_calls=p.low;
    for (unsigned i=0;i<DP_V2_PWR_DEADLINE_OFF_RETRIES;i++) {
        assert(dp_v2_pwr_model_poll(&m,201+i)==DP_V2_PWR_DRIVER_ERROR);
        assert(m.phase==DP_V2_PWR_FAULT);
        assert(m.deadline_off_retries_remaining==DP_V2_PWR_DEADLINE_OFF_RETRIES-i-1);
    }
    assert(p.low==deadline_off_calls+(int)DP_V2_PWR_DEADLINE_OFF_RETRIES);
    for (unsigned i=0;i<1000;i++) {
        assert(dp_v2_pwr_model_poll(&m,300+i)==DP_V2_PWR_OK);
    }
    assert(p.low==deadline_off_calls+(int)DP_V2_PWR_DEADLINE_OFF_RETRIES);
    assert(m.phase==DP_V2_PWR_FAULT); // exhaustion must never be reported OFF
    // Manual OFF remains an authorized recovery attempt after exhaustion.
    p.bad_low=false;
    assert(dp_v2_pwr_model_set(&m,false,2000,0)==DP_V2_PWR_OK);
    assert(m.phase==DP_V2_PWR_OFF && m.deadline_off_retries_remaining==0);

    // An OFF retry can recover before the budget is exhausted.
    assert(dp_v2_pwr_model_set(&m,true,2100,100)==DP_V2_PWR_OK);
    p.bad_low=true;
    assert(dp_v2_pwr_model_poll(&m,2200)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT);
    p.bad_low=false;
    assert(dp_v2_pwr_model_poll(&m,2201)==DP_V2_PWR_OK);
    assert(m.phase==DP_V2_PWR_OFF && m.deadline_off_retries_remaining==0);
    const int recovered_low_calls=p.low;
    assert(dp_v2_pwr_model_poll(&m,5000)==DP_V2_PWR_OK);
    assert(p.low==recovered_low_calls);
    assert(dp_v2_pwr_model_disable(&m)==DP_V2_PWR_OK);

    // A failed initial enable/OFF is NOT an expired ON deadline, so it
    // does not arm automatic retries or report successful de-energization.
    dp_v2_pwr_model_init(&m);
    p=(probe_t){0};
    p.bad_low=true;
    assert(dp_v2_pwr_model_enable(&m,drive,&p,200)==DP_V2_PWR_DRIVER_ERROR);
    assert(m.phase==DP_V2_PWR_FAULT && !m.enabled);
    assert(m.deadline_off_retries_remaining==0);
    const int initial_off_calls=p.low;
    for (unsigned i=0;i<50;i++) assert(dp_v2_pwr_model_poll(&m,i)==DP_V2_PWR_OK);
    assert(p.low==initial_off_calls);
    assert(m.phase==DP_V2_PWR_FAULT);
    puts("dp_v2_pwr_model: PASS");
}
