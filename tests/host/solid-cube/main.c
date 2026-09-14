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
static unsigned int frames, lines, clears, triangles;
static unsigned int expected_layer = MIGA80_LAYER_PLANAR;
static FILE *edges, *images;
static uint32_t trace = 0x811c9dc5U;
static void record(uint32_t v) { trace = ((trace << 5) | (trace >> 27)) ^ v; }
static int play(void *p,uint32_t id) { (void)p; assert(id==0); record(80);record(id);return 1; }
static int layer(void *p, uint32_t l) { record(36); record(l); miga80_draw_select(p,l); return 1; }
static int pset(void *p,uint32_t x,uint32_t y,uint32_t c) { miga80_draw_pset(p,x,y,c); return 1; }
static int tri(void *p,uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,
    uint32_t x2,uint32_t y2,uint32_t c)
{
    assert(surface.layer==expected_layer);
    assert(x0<256 && y0<256 && x1<256 && y1<256 && x2<256 && y2<256);
    assert(c>=2 && c<=9);
    fprintf(edges,"%u %u %u %u %u %u %u %u\n",frames,x0,y0,x1,y1,x2,y2,c);
    record(40);record(x0);record(y0);record(c);
    record(72);record(x1);record(y1);record(76);record(x2);record(y2);
    miga80_draw_line_start(p,x0,y0,c);miga80_draw_tri_middle(p,x1,y1);miga80_draw_tri_end(p,x2,y2);
    ++triangles;return 1;
}
static int clear(void *p,uint32_t c) { assert(c==0); record(64); record(c); miga80_draw_clear(p,c); ++clears; return 1; }
static int flip(void *p)
{
    unsigned int i, lit = 0;
    (void)p;
    record(68); ++frames; assert(triangles-lines>=2 && triangles-lines<=6); lines=triangles;
    if (expected_layer == MIGA80_LAYER_PIXEL) {
        assert(surface.pixel_written);
        for (i=0;i<sizeof(planes);++i) { assert(planes[i]==0); }
        for (i=0;i<sizeof(pixels);++i) {
            assert(pixels[i]<=9);
            if (pixels[i]!=0) { ++lit; }
        }
        assert(lit>0);
    }
    {
        static uint8_t frame[65536];
        if (expected_layer==MIGA80_LAYER_PLANAR) {
            for (i=0;i<65536;++i) { frame[i]=miga80_draw_planar_pixel(&surface,i%256,i/256); }
        } else { memcpy(frame,pixels,sizeof(frame)); }
        assert(fwrite(frame,1,sizeof(frame),images)==sizeof(frame));
    }
    return 1;
}
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
    struct miga80_ir_runtime runtime={&surface,pset,layer,NULL,clear,flip,time_now,tri,play,NULL,NULL,NULL};
    char source[4097],path[1024];
    uint8_t code[8192];
    size_t n,size,bound=0;
    uint32_t result;
    unsigned int mode,i;
    FILE *f;
    assert(argc==3 || argc==4);
    if (argc==4) {
        assert(strcmp(argv[3],"PIXEL")==0 || strcmp(argv[3],"PLANAR")==0);
        expected_layer=strcmp(argv[3],"PIXEL")==0 ? MIGA80_LAYER_PIXEL : MIGA80_LAYER_PLANAR;
    }
    trig_tests();
    f=fopen(argv[1],"rb");assert(f);n=fread(source,1,4096,f);assert(!ferror(f));fclose(f);source[n]=0;
    surface.layer=MIGA80_LAYER_PIXEL;surface.pixels=pixels;
    for(i=0;i<4;++i)surface.planes[i]=planes+i*8192;
    snprintf(path,sizeof(path),"%s/edges.txt",argv[2]);edges=fopen(path,"w");assert(edges);
    snprintf(path,sizeof(path),"%s/frames.bin",argv[2]);images=fopen(path,"wb");assert(images);
    if(!miga80_parse_function(source,n,&ast,&diagnostic) || !miga80_lower_function(&ast,&ir,&diagnostic) ||
       !miga80_build_value_ir(&ir,&value,&diagnostic) ||
       !miga80_evaluate_ir_with_runtime(&ir,NULL,0,&result,&runtime,&diagnostic)) {
        fprintf(stderr,"%u:%u %s\n",diagnostic.line,diagnostic.column,diagnostic.message);return 1;
    }
    fclose(edges);fclose(images);
    printf("ast_nodes=%u\nstatements=%u\nlocals=%u\n",ast.node_count,ast.statement_count,ast.local_count);
    assert(frames==250 && clears==frames && triangles>=500 && triangles<=1500);
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
        assert(size<=8192);
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
