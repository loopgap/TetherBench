#include "probe_core.h"
#include <limits.h>
#include <string.h>

uint16_t tb_u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
uint32_t tb_u32(const uint8_t *p) { return (uint32_t)tb_u16(p) | (uint32_t)tb_u16(p+2)<<16; }
uint64_t tb_u64(const uint8_t *p) { return (uint64_t)tb_u32(p) | (uint64_t)tb_u32(p+4)<<32; }
void tb_put16(uint8_t *p, uint16_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
void tb_put32(uint8_t *p, uint32_t n) { tb_put16(p,(uint16_t)n); tb_put16(p+2,(uint16_t)(n>>16)); }
void tb_put64(uint8_t *p, uint64_t n) { tb_put32(p,(uint32_t)n); tb_put32(p+4,(uint32_t)(n>>32)); }
enum tb_status tb_decode(const uint8_t *p, size_t n, struct tb_frame *f) {
    if (n<TB_HEADER_SIZE || memcmp(p,"TBC5",4) || p[4]!=1 || tb_u16(p+6)!=0 ||
        tb_u32(p+12)>TB_MAX_PAYLOAD || tb_u32(p+12)!=n-TB_HEADER_SIZE) return TB_INVALID;
    *f=(struct tb_frame){.op=p[5],.request=tb_u32(p+8),.lease=tb_u64(p+16),
                         .data=p+TB_HEADER_SIZE,.size=n-TB_HEADER_SIZE};
    return TB_OK;
}
size_t tb_encode(uint8_t *p, const struct tb_frame *f, enum tb_status status, size_t size) {
    if (size>TB_MAX_PAYLOAD) return 0;
    memcpy(p,"TBC5",4); p[4]=1; p[5]=f->op;
    tb_put16(p+6,(uint16_t)(0x8000u|status)); tb_put32(p+8,f->request);
    tb_put32(p+12,(uint32_t)size); tb_put64(p+16,f->lease);
    return TB_HEADER_SIZE+size;
}
bool tb_owned(const struct tb_lease *l, uint64_t now, uint64_t id) {
    return id && id==l->id && now<l->deadline;
}
enum tb_status tb_acquire(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl) {
    if (!id || ttl<5000 || ttl>60000 || now>UINT64_MAX-ttl) return TB_INVALID;
    if (l->id && now<l->deadline) return TB_BUSY;
    l->id=id; l->deadline=now+ttl; return TB_OK;
}
enum tb_status tb_renew(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl) {
    if (!tb_owned(l,now,id)) return TB_LEASE;
    if (ttl<5000 || ttl>60000 || now>UINT64_MAX-ttl) return TB_INVALID;
    l->deadline=now+ttl; return TB_OK;
}
enum tb_status tb_validate_pins(const struct tb_pins *p, uint32_t allowed, uint32_t occupied) {
    static const uint32_t maximum[]={0,4000000,5000000,20000000,1000000};
    static const uint8_t minimum[]={0,2,2,4,2};
    if (p->protocol<1 || p->protocol>=TB_PROTOCOL_COUNT) return TB_INVALID;
    if (!p->count) {
        if (p->hz) return TB_INVALID;
        for (unsigned i=0;i<4;i++) if (p->pin[i]!=-1) return TB_INVALID;
        return TB_OK;
    }
    if (p->count<minimum[p->protocol] ||
        p->count>4 || !p->hz || p->hz>maximum[p->protocol] ||
        (p->protocol==TB_SWD && p->hz<1000) ||
        (p->protocol==TB_UART && p->count!=2 && p->count!=4) ||
        (p->protocol==TB_I2C && p->count!=2)) return TB_INVALID;
    uint32_t used=occupied;
    for (unsigned i=0;i<4;i++) {
        if (i>=p->count) { if (p->pin[i]!=-1) return TB_INVALID; continue; }
        if (p->pin[i]<0 || p->pin[i]>28) return TB_INVALID;
        uint32_t bit=1u<<(unsigned)p->pin[i];
        if (!(allowed&bit) || (used&bit)) return TB_CONFLICT;
        used|=bit;
    }
    if (p->protocol==TB_SWD && p->count>3) return TB_INVALID;
    return TB_OK;
}

static bool dap_command(const uint8_t *p, size_t n, size_t *used, size_t *out, uint32_t max_hz) {
    if (!n) return false;
    size_t need=0, response=2;
    switch (p[0]) {
    case 0x00: need=2; response=258; break;
    case 0x01: need=3; break;
    case 0x02: need=2; if (n>=2 && p[1]>1) return false; break;
    case 0x03: need=1; break;
    case 0x04:
        need=6;
        if (n>=6 && (p[1]>32 || tb_u16(p+2)>100 || tb_u16(p+4)>100)) return false;
        break;
    case 0x05: {
        if (n<3 || p[1]!=0 || p[2]>64) return false;
        need=3; response=3;
        for (unsigned i=0;i<p[2];i++) {
            if (need>=n) return false;
            uint8_t r=p[need++];
            if (r&0x40 || ((r&0x10) && !(r&0x02)) || ((r&0x20) && (r&0x02))) return false;
            if (!(r&2) || (r&0x10)) need+=4;
            if ((r&2) && !(r&0x10)) response+=4;
            if (r&0x80) response+=4;
            if (need>n) return false;
        }
        break;
    }
    case 0x06: {
        if (n<5 || p[1]!=0 || !tb_u16(p+2) || tb_u16(p+2)>128 || p[4]&0xf0) return false;
        size_t count=tb_u16(p+2); need=5; response=4;
        if (p[4]&2) response+=count*4; else need+=count*4;
        break;
    }
    case 0x08: need=6; break;
    case 0x09: need=3; if (n>=3 && tb_u16(p+1)>10000) return false; break;
    case 0x0a: need=1; response=3; break;
    case 0x10: need=7; if (n>=7 && ((p[2]&~0x83u) || tb_u32(p+3)>10000)) return false; break;
    case 0x11: need=5; if (n>=5 && (tb_u32(p+1)<1000 || tb_u32(p+1)>max_hz)) return false; break;
    case 0x12: if (n<2) return false; need=2+((p[1]?p[1]:256)+7u)/8; break;
    case 0x13: need=2; if (n>=2 && p[1]&0xf8) return false; break;
    case 0x1d:
        if (n<2 || !p[1] || p[1]>64) return false;
        need=2;
        for (unsigned i=0;i<p[1];i++) {
            if (need>=n) return false;
            uint8_t info=p[need++];
            if (info&0x40) return false;
            size_t bytes=((info&63)?(info&63):64)+7u; bytes/=8;
            if (info&0x80) response+=bytes; else need+=bytes;
            if (need>n) return false;
        }
        break;
    default: return false;
    }
    if (need>n || response>TB_DAP_PACKET) return false;
    *used=need; *out=response; return true;
}
bool tb_dap_validate(const uint8_t *p, size_t n, uint32_t max_hz) {
    if (!n || n>TB_DAP_PACKET) return false;
    size_t used=0, out=0;
    if (p[0]!=0x7f) return dap_command(p,n,&used,&out,max_hz) && used==n;
    if (n<2 || !p[1] || p[1]>16) return false;
    size_t pos=2,total=2;
    for (unsigned i=0;i<p[1];i++) {
        if (!dap_command(p+pos,n-pos,&used,&out,max_hz)) return false;
        pos+=used; total+=out;
        if (total>TB_DAP_PACKET) return false;
    }
    return pos==n;
}
int tb_link_score(const struct tb_link *local, const struct tb_link *peer, uint64_t now) {
    const struct tb_link *links[]={local,peer};
    int worst=INT_MAX;
    for (unsigned i=0;i<2;i++) {
        const struct tb_link *l=links[i];
        if (l->measured>now || now-l->measured>5000 || l->samples<5 ||
            l->rssi < -110 || l->rssi > -1 || l->loss_permille>1000 || l->rtt_ms>5000) return INT_MIN;
        int rssi=l->rssi<-90?0:l->rssi>-40?100:2*(l->rssi+90);
        int score=rssi-(int)l->loss_permille/10-(int)l->rtt_ms/5;
        if (score<worst) worst=score;
    }
    return worst;
}
enum tb_status tb_band_prepare(struct tb_band *b, uint64_t now, uint64_t id, uint8_t channel,
                              uint8_t previous, const struct tb_link current[2], const struct tb_link next[2]) {
    if (b->stage!=TB_BAND_IDLE) return TB_BUSY;
    if (!id || !((channel>=1 && channel<=11) || channel==36 || channel==40 || channel==44 || channel==48) ||
        channel==previous) return TB_INVALID;
    int old=tb_link_score(current,current+1,now), candidate=tb_link_score(next,next+1,now);
    if (old==INT_MIN || candidate==INT_MIN) return TB_INVALID;
    if (candidate<old+10 || (b->last_switch && now-b->last_switch<30000)) return TB_STATE;
    *b=(struct tb_band){.stage=TB_BAND_PREPARED,.id=id,.deadline=now+5000,
                        .last_switch=b->last_switch,.channel=channel,.previous=previous};
    return TB_OK;
}
enum tb_status tb_band_commit(struct tb_band *b, uint64_t now, uint64_t id, uint32_t delay_ms) {
    if (b->stage!=TB_BAND_PREPARED || b->id!=id || now>=b->deadline) return TB_STATE;
    if (delay_ms<500 || delay_ms>3000) return TB_INVALID;
    b->stage=TB_BAND_COMMITTED; b->deadline=now+delay_ms; return TB_OK;
}
enum tb_status tb_band_confirm(struct tb_band *b, uint64_t now, uint64_t id) {
    if (b->stage!=TB_BAND_SWITCHED || b->id!=id || now>=b->deadline) return TB_STATE;
    b->stage=TB_BAND_IDLE; b->last_switch=now; return TB_OK;
}
int tb_band_tick(struct tb_band *b, uint64_t now) {
    if (b->stage==TB_BAND_IDLE || now<b->deadline) return 0;
    if (b->stage==TB_BAND_PREPARED) { b->stage=TB_BAND_IDLE; return 0; }
    if (b->stage==TB_BAND_COMMITTED) { b->stage=TB_BAND_SWITCHED; b->deadline=now+15000; return 1; }
    b->stage=TB_BAND_IDLE; return -1;
}
