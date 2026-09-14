#include "audio/mod.h"
#include <string.h>
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0]<<8)|p[1]; }
const char *miga80_mod_validate(const uint8_t *b,size_t bytes,struct miga80_mod_info *info)
{
    uint32_t patterns=0, sample_bytes=0, offset;
    unsigned int i;
    if (b==NULL || info==NULL || bytes<1084U) return "MOD HEADER TRUNCATED";
    if (bytes>MIGA80_MOD_MAX_BYTES) return "MOD EXCEEDS 256K LIMIT";
    if (memcmp(b+1080,"M.K.",4)!=0 && memcmp(b+1080,"M!K!",4)!=0 && memcmp(b+1080,"4CHN",4)!=0)
        return "MOD NEEDS 4 CHANNELS / 31 SAMPLES";
    if (b[950]==0 || b[950]>128) return "MOD INVALID SONG LENGTH";
    /* PT determines the sample base from all 128 orders, including unused ones. */
    for (i=0;i<128;++i) {
        if (b[952+i]>127) return "MOD INVALID PATTERN ORDER";
        if (b[952+i]>=patterns) patterns=(uint32_t)b[952+i]+1U;
    }
    offset=1084U+1024U*patterns;
    if (offset>bytes) return "MOD PATTERNS TRUNCATED";
    for (i=0;i<31;++i) {
        const uint8_t *h=b+20+30*i;
        uint32_t length=be16(h+22)*2, start=be16(h+26)*2, loop=be16(h+28)*2;
        if (h[24]>15 || h[25]>64) return "MOD INVALID FINETUNE / VOLUME";
        if ((loop>2 && (start>length || loop>length-start)) ||
            (loop<=2 && start!=0)) return "MOD SAMPLE LOOP OUT OF BOUNDS";
        sample_bytes+=length;
    }
    if (sample_bytes>bytes-offset) return "MOD SAMPLES TRUNCATED";
    if (sample_bytes!=bytes-offset) return "MOD UNEXPECTED TRAILING DATA";
    for (i=1084;i<offset;i+=4) {
        unsigned int sample=(b[i]&0xf0U)|(b[i+2]>>4);
        unsigned int period=((b[i]&15U)<<8)|b[i+1];
        unsigned int effect=b[i+2]&15U, param=b[i+3];
        if (sample>31) return "MOD INVALID SAMPLE NUMBER";
        if (period && (period<113 || period>856)) return "MOD NOTE OUTSIDE PROTRACKER RANGE";
        if (effect==8) return "MOD PANNING EFFECT 8 UNSUPPORTED";
        if (effect==11 && param>=b[950]) return "MOD POSITION JUMP OUT OF BOUNDS";
        if (effect==13 && ((param>>4)>6 || (param&15)>9 || (param>>4)*10+(param&15)>63))
            return "MOD INVALID PATTERN BREAK";
    }
    info->patterns=(uint16_t)patterns;info->song_length=b[950];
    info->sample_offset=offset;info->sample_bytes=sample_bytes;info->file_bytes=(uint32_t)bytes;
    return NULL;
}
void miga80_mod_prepare(uint8_t *copy,const uint8_t *data,const struct miga80_mod_info *info)
{
    uint32_t source=info->sample_offset, dest=source;
    unsigned int i;
    memcpy(copy,data,info->sample_offset);
    for(i=0;i<31;++i) {
        uint8_t *h=copy+20+30*i;
        uint32_t length=be16(h+22)*2;
        if(length<=2) { h[22]=h[23]=h[26]=h[27]=h[28]=0;h[29]=1; }
        else { memcpy(copy+dest,data+source,length);dest+=length; }
        source+=length;
    }
    /* Safe padding for one-word idle DMA; the ASM uses a separate zero word. */
    memset(copy+dest,0,info->file_bytes+2U-dest);
}
