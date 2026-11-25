#include "probe_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void frames(void) {
    uint8_t wire[TB_HEADER_SIZE+TB_MAX_PAYLOAD]={0};
    struct tb_frame f={.op=TB_DAP,.request=0x12345678,.lease=0x123456789abcdefULL}, decoded;
    size_t n=tb_encode(wire,&f,TB_OK,2);
    assert(n==26 && wire[6]==0 && wire[7]==0x80);
    // A response cannot be replayed as a request.
    assert(tb_decode(wire,n,&decoded)==TB_INVALID);
    tb_put16(wire+6,0);
    assert(tb_decode(wire,n,&decoded)==TB_OK);
    assert(decoded.request==f.request && decoded.lease==f.lease && decoded.size==2);
    for (size_t i=0;i<n;i++) assert(tb_decode(wire,i,&decoded)==TB_INVALID);
    assert(tb_decode(wire,n+1,&decoded)==TB_INVALID);
    wire[4]=2; assert(tb_decode(wire,n,&decoded)==TB_INVALID); wire[4]=1;
    tb_put32(wire+12,TB_MAX_PAYLOAD+1); assert(tb_decode(wire,sizeof wire,&decoded)==TB_INVALID);
    assert(tb_encode(wire,&f,TB_OK,TB_MAX_PAYLOAD+1)==0);
}
static void leases(void) {
    struct tb_lease l={0};
    assert(tb_acquire(&l,100,0,10000)==TB_INVALID);
    assert(tb_acquire(&l,100,42,10000)==TB_OK);
    assert(tb_acquire(&l,101,43,10000)==TB_BUSY);
    assert(!tb_owned(&l,102,43));
    assert(tb_renew(&l,102,43,10000)==TB_LEASE);
    assert(tb_renew(&l,102,42,100)==TB_INVALID);
    assert(tb_renew(&l,102,42,5000)==TB_OK);
    assert(!tb_owned(&l,5102,42));
    assert(tb_acquire(&l,5102,43,5000)==TB_OK);
    assert(tb_acquire(&l,UINT64_MAX-10,43,5000)==TB_INVALID);
}
static void pins(void) {
    struct tb_pins p={.protocol=TB_SWD,.count=3,.pin={2,3,4,-1},.hz=4000000};
    uint32_t allowed=(1u<<2)|(1u<<3)|(1u<<4);
    assert(tb_validate_pins(&p,allowed,0)==TB_OK);
    assert(tb_validate_pins(&p,allowed,1u<<4)==TB_CONFLICT);
    p.pin[1]=2; assert(tb_validate_pins(&p,allowed,0)==TB_CONFLICT);
    p.pin[1]=16; assert(tb_validate_pins(&p,allowed,0)==TB_CONFLICT);
    p.pin[1]=-1; assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
    p.pin[1]=3; p.hz=4000001; assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
    p.hz=0; assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
    p.protocol=TB_I2C; p.hz=400000; assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
    p=(struct tb_pins){.protocol=TB_UART,.count=3,.pin={2,3,4,-1},.hz=115200};
    assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
    p=(struct tb_pins){.protocol=TB_UART,.pin={-1,-1,-1,-1}};
    assert(tb_validate_pins(&p,0,UINT32_MAX)==TB_OK);
    p.pin[0]=2; assert(tb_validate_pins(&p,allowed,0)==TB_INVALID);
}
static void dap(void) {
    const uint8_t transfer[]={5,0,2,2,0,1,2,3,4};
    assert(tb_dap_validate(transfer,sizeof transfer,4000000));
    for (size_t n=0;n<sizeof transfer;n++) assert(!tb_dap_validate(transfer,n,4000000));
    const uint8_t batch[]={0x7f,2,0,0xf0,3};
    assert(tb_dap_validate(batch,sizeof batch,4000000));
    const uint8_t recursive[]={0x7f,1,0x7f,1,3};
    assert(!tb_dap_validate(recursive,sizeof recursive,4000000));
    const uint8_t huge_read[]={6,0,255,255,2};
    assert(!tb_dap_validate(huge_read,sizeof huge_read,4000000));
    const uint8_t sequence[]={0x1d,2,0x80,8,0xff};
    assert(tb_dap_validate(sequence,sizeof sequence,4000000));
    const uint8_t clock[]={0x11,0,0,0,0};
    assert(!tb_dap_validate(clock,sizeof clock,4000000));
    uint32_t seed=1234;
    uint8_t fuzz[1024];
    for (unsigned i=0;i<30000;i++) {
        size_t n=(seed>>16)%sizeof fuzz;
        for (size_t k=0;k<n;k++) { seed=seed*1664525u+1013904223u; fuzz[k]=(uint8_t)(seed>>24); }
        (void)tb_dap_validate(fuzz,n,4000000);
    }
}
static void bands(void) {
    struct tb_band b={0};
    struct tb_link old[2]={{-80,20,15,10,1000},{-78,10,10,10,1000}};
    struct tb_link next[2]={{-55,5,5,10,1000},{-58,10,5,10,1000}};
    assert(tb_band_prepare(&b,1100,1,40,6,old,next)==TB_OK);
    assert(tb_band_commit(&b,1200,2,1000)==TB_STATE);
    assert(tb_band_commit(&b,1200,1,100)==TB_INVALID);
    assert(tb_band_commit(&b,1200,1,1000)==TB_OK);
    assert(tb_band_tick(&b,2199)==0);
    assert(tb_band_tick(&b,2200)==1);
    assert(tb_band_tick(&b,17200)==-1);
    assert(b.stage==TB_BAND_IDLE);
    assert(tb_band_prepare(&b,18000,2,40,6,old,next)==TB_INVALID);
    old[0].measured=old[1].measured=next[0].measured=next[1].measured=18000;
    assert(tb_band_prepare(&b,18000,2,40,6,old,next)==TB_OK);
    assert(tb_band_commit(&b,18100,2,500)==TB_OK);
    assert(tb_band_tick(&b,18600)==1);
    assert(tb_band_confirm(&b,19000,2)==TB_OK);
    assert(tb_band_prepare(&b,19001,3,6,40,old,next)==TB_STATE);
    next[1].loss_permille=1001;
    assert(tb_band_prepare(&b,19001,3,6,40,old,next)==TB_INVALID);
}
int main(void) { frames(); leases(); pins(); dap(); bands(); puts("core: frame, lease, pin, DAP and band checks passed"); }
