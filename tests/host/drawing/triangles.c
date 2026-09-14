#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "graphics/drawing.h"

static int64_t orient(int32_t ax, int32_t ay, int32_t bx, int32_t by,
    int32_t px, int32_t py)
{
    return (int64_t)(bx-ax)*(py-ay) - (int64_t)(by-ay)*(px-ax);
}

void triangle_tests(void)
{
    static struct miga80_draw_surface surface;
    static uint8_t guarded[65536+32];
    static uint8_t reference[65536];
    uint32_t random = 68020U;
    unsigned int trial, x, y, i;
    surface.pixels = guarded+16; surface.layer = MIGA80_LAYER_PIXEL;
    for (trial = 0; trial < 150U; ++trial) {
        int32_t v[6];
        int64_t area;
        struct miga80_draw_triangle t;
        for (i = 0; i < 6U; ++i) {
            random = random * 1664525U + 1013904223U;
            v[i] = (int32_t)(random >> 16) % 640 - 192;
        }
        if (trial == 0U) { v[0]=0;v[1]=0;v[2]=256;v[3]=0;v[4]=0;v[5]=256; }
        if (trial == 1U) { v[0]=0;v[1]=0;v[2]=0;v[3]=256;v[4]=1;v[5]=256; }
        if (trial == 2U) { v[0]=-32768;v[1]=-32768;v[2]=32767;v[3]=-32768;v[4]=32767;v[5]=32767; }
        if (trial == 3U) { v[0]=10;v[1]=10;v[2]=20;v[3]=20;v[4]=30;v[5]=30; }
        area = orient(v[0],v[1],v[2],v[3],v[4],v[5]);
        if (area < 0) {
            int32_t tmp=v[2];v[2]=v[4];v[4]=tmp;
            tmp=v[3];v[3]=v[5];v[5]=tmp;
        }
        memset(reference, 3, sizeof(reference));
        for (y=0;y<256U;++y) { for (x=0;x<256U;++x) {
            int inside = area != 0;
            for (i=0;i<3U;++i) {
                unsigned int j=(i+1U)%3U;
                int32_t dx=v[j*2]-v[i*2], dy=v[j*2+1]-v[i*2+1];
                int64_t e=(int64_t)dx*((int32_t)y*2+1-v[i*2+1]*2) -
                    (int64_t)dy*((int32_t)x*2+1-v[i*2]*2);
                if (e<0 || (e==0 && !(dy<0 || (dy==0 && dx>0)))) { inside=0; }
            }
            if (inside) { reference[y*256+x]=(uint8_t)(trial%16U); }
        } }
        t=(struct miga80_draw_triangle){v[0],v[1],v[2],v[3],v[4],v[5],trial%16U};
        for (i=0;i<2U;++i) {
            int32_t tmp;
            memset(guarded, 0xa5, sizeof(guarded));
            memset(surface.pixels, 3, 65536U);
            miga80_draw_triangle(&surface, &t);
            assert(memcmp(surface.pixels,reference,65536U)==0);
            for (x=0;x<16U;++x) { assert(guarded[x]==0xa5 && guarded[65552U+x]==0xa5); }
            tmp=t.x1;t.x1=t.x2;t.x2=tmp;tmp=t.y1;t.y1=t.y2;t.y2=tmp;
        }
    }
}
