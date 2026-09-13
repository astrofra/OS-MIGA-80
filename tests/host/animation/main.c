#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics/drawing.h"
#include "compiler/backend_m68k/backend.h"
#include "compiler/backend_m68k/encoder.h"

static struct miga80_draw_surface surface;
static uint8_t pixels[65536], planes[32768];
static unsigned int frames, lines, clears;
static FILE *edges;
static uint32_t trace = 0x811c9dc5U;
static void record(uint32_t v) { trace = ((trace << 5) | (trace >> 27)) ^ v; }
static int layer(void *p, uint32_t l) { record(36); record(l); miga80_draw_select(p,l); return 1; }
static int pset(void *p,uint32_t x,uint32_t y,uint32_t c) { miga80_draw_pset(p,x,y,c); return 1; }
static int line(void *p,uint32_t x,uint32_t y,uint32_t xx,uint32_t yy,uint32_t c)
{
    assert(surface.layer == MIGA80_LAYER_PLANAR);
    assert(x < 256 && y < 256 && xx < 256 && yy < 256);
    assert(c == 2 || c == 4 || c == 6 || c == 8);
    fprintf(edges,"%u %u %u %u %u %u %u\n",frames,lines%12,x,y,xx,yy,c);
    record(40); record(x); record(y); record(c); record(44); record(xx); record(yy);
    miga80_draw_line_start(p,x,y,c); miga80_draw_line_end(p,xx,yy); ++lines;
    return 1;
}
static int clear(void *p,uint32_t c) { assert(c==0); record(64); record(c); miga80_draw_clear(p,c); ++clears; return 1; }
static int flip(void *p) { (void)p; record(68); ++frames; assert(lines==frames*12); return 1; }
static uint32_t time_now(void *p) { (void)p; return frames*65536U/25U; }

static void trig_tests(void)
{
    uint32_t seed=1;
    unsigned int i;
    double worst=0;
    for(i=0;i<100000;++i) {
        double a,error;
        seed=seed*1664525U+1013904223U;
        a=(double)(int32_t)seed/65536.0;
        error=fabs((double)miga80_fix_sin((int32_t)seed)/65536.0-sin(a));
        if(error>worst)worst=error;
        assert(error<0.00012);
        error=fabs((double)miga80_fix_cos((int32_t)seed)/65536.0-cos(a));
        if(error>worst)worst=error;
        assert(error<0.00012);
    }
    assert(miga80_fix_sin(0)==0 && miga80_fix_cos(0)==65536);
    printf("trig_max_error=%.8f\n",worst);
}

int main(int argc,char **argv)
{
    static struct miga80_ast_function ast;
    static struct miga80_ir_function ir;
    static struct miga80_value_function value;
    struct miga80_diagnostic diagnostic;
    struct miga80_ir_runtime runtime={&surface,pset,layer,line,clear,flip,time_now};
    char source[4097],path[1024];
    uint8_t code[8192];
    size_t n,size,bound=0;
    uint32_t result;
    unsigned int mode,i;
    FILE *f;
    assert(argc==3);
    trig_tests();
    f=fopen(argv[1],"rb");assert(f);n=fread(source,1,4096,f);assert(!ferror(f));fclose(f);source[n]=0;
    surface.layer=MIGA80_LAYER_PIXEL;surface.pixels=pixels;
    for(i=0;i<4;++i)surface.planes[i]=planes+i*8192;
    snprintf(path,sizeof(path),"%s/edges.txt",argv[2]);edges=fopen(path,"w");assert(edges);
    if(!miga80_parse_function(source,n,&ast,&diagnostic) || !miga80_lower_function(&ast,&ir,&diagnostic) ||
       !miga80_build_value_ir(&ir,&value,&diagnostic) ||
       !miga80_evaluate_ir_with_runtime(&ir,NULL,0,&result,&runtime,&diagnostic)) {
        fprintf(stderr,"%u:%u %s\n",diagnostic.line,diagnostic.column,diagnostic.message);return 1;
    }
    fclose(edges);
    assert(frames==250 && clears==frames && lines==3000);
    snprintf(path,sizeof(path),"%s/trig-table.s",argv[2]);f=fopen(path,"w");assert(f);
    for(i=0;i<250;++i) {
        const int32_t angle=(int32_t)(((int64_t)(i*65536U/25U)*52429)/65536);
        const int32_t angle_x=(int32_t)(((int64_t)(i*65536U/25U)*36045)/65536)+42598;
        fprintf(f," .long %ld,%ld,%ld,%ld,%ld,%ld\n",(long)angle,
            (long)miga80_fix_sin(angle),(long)miga80_fix_cos(angle),
            (long)angle_x,(long)miga80_fix_sin(angle_x),(long)miga80_fix_cos(angle_x));
    }
    fclose(f);
    printf("trace=%08x\n",trace);
    for(mode=1;mode<3;++mode) {
        int ok=mode==1 ? miga80_encode_m68k_o1(code,sizeof(code),&value,&size,&diagnostic) :
               miga80_encode_m68k_o1_guarded(code,sizeof(code),&value,&size,&bound,&diagnostic);
        if(!ok){fprintf(stderr,"%u:%u %s\n",diagnostic.line,diagnostic.column,diagnostic.message);return 1;}
        assert(size<=4096);
        snprintf(path,sizeof(path),"%s/mode%u.bin",argv[2],mode);f=fopen(path,"wb");assert(f);
        assert(fwrite(code,1,size,f)==size);fclose(f);
        snprintf(path,sizeof(path),"%s/mode%u.s",argv[2],mode);f=fopen(path,"w");assert(f);
        assert(mode==1 ?
            miga80_emit_gnu_m68k_o1(f,&value,&diagnostic) : miga80_emit_gnu_m68k_o1_guarded(f,&value,&diagnostic));
        fclose(f);printf("mode%u_bytes=%lu\n",mode,(unsigned long)size);
    }
    printf("frames=%u\nlines=%u\nstack_bound=%lu\n",frames,lines,(unsigned long)bound);
    assert(bound<=4096);
    {
        static const char calls[] = "function main(a:fix,b:fix,c:fix): fix\n"
            " local k:fix=a+b+c\n cls(3); flip()\n return sin(a)+cos(b)+time()+k\nend\n";
        assert(miga80_parse_function(calls,strlen(calls),&ast,&diagnostic));
        assert(miga80_lower_function(&ast,&ir,&diagnostic));
        assert(miga80_build_value_ir(&ir,&value,&diagnostic));
        for(mode=0;mode<3;++mode) {
            assert(mode==0 ? miga80_encode_m68k_o0(code,sizeof(code),&ir,&size,&diagnostic) :
                mode==1 ? miga80_encode_m68k_o1(code,sizeof(code),&value,&size,&diagnostic) :
                miga80_encode_m68k_o1_guarded(code,sizeof(code),&value,&size,&bound,&diagnostic));
            snprintf(path,sizeof(path),"%s/calls%u.bin",argv[2],mode);f=fopen(path,"wb");assert(f);
            assert(fwrite(code,1,size,f)==size);fclose(f);
        }
    }
    return 0;
}
