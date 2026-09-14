#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/mod.h"
#include "compiler/backend_m68k/backend.h"
#include "compiler/backend_m68k/encoder.h"
static uint8_t original[MIGA80_MOD_MAX_BYTES],bad[MIGA80_MOD_MAX_BYTES+2];
static struct miga80_ast_function ast;
static struct miga80_ir_function ir;
static struct miga80_value_function values;
static void rejected(size_t size,const char *text)
{
    struct miga80_mod_info info;
    const char *error=miga80_mod_validate(bad,size,&info);
    assert(error!=NULL && strstr(error,text)!=NULL);
}
static unsigned int calls;
static int play(void *p,uint32_t id) { (void)p;assert(calls++==1 && id==0);return 1; }
static int mute(void *p,uint32_t mask) { (void)p;assert(calls++==2 && mask==3);return 1; }
static int stop(void *p) { (void)p;assert(calls++==3);return 1; }
static uint32_t position(void *p) { (void)p;assert(calls==0 || calls==4);return calls++==0 ? 41U : 42U; }
int main(int argc,char **argv)
{
    const char *source="function main(a:i32,b:i32,c:i32):i32\n"
        "local p:i32=music_position()\n"
        "music_play(\"SYS:mods/93_10_12_A_SYNTH_1.mod\")\n"
        "music_mute(3);music_stop()\n"
        "return a+b+c+p+music_position() end\n";
    const char *invalid[]={"music_play(3)","music_play()","music_play(\"a\",1)",
        "music_stop(1)","music_mute(256)","music_mute(true)","music_mute(1,2)"};
    struct miga80_mod_info info;
    struct miga80_diagnostic diag;
    struct miga80_ir_runtime rt={.music_play=play,.music_stop=stop,.music_position=position,.music_mute=mute};
    const uint32_t args[]={1,2,3};
    uint32_t result,seed=19;
    unsigned int i,mode;
    size_t n,size,bound;
    uint8_t code[4096];
    char path[1024],fixture[1024];
    FILE *f;
    assert(argc==3);
    f=fopen(argv[1],"rb");assert(f);n=fread(original,1,sizeof(original),f);assert(!ferror(f));fclose(f);
    assert(miga80_mod_validate(original,n,&info)==NULL);
    assert(info.patterns==4 && info.song_length==8 && info.sample_bytes==14912 && info.file_bytes==20092);
    memcpy(bad,original,n);memcpy(bad+1080,"M!K!",4);assert(miga80_mod_validate(bad,n,&info)==NULL);
    memcpy(bad+1080,"4CHN",4);assert(miga80_mod_validate(bad,n,&info)==NULL);
    for(i=0;i<n;++i) { memcpy(bad,original,n);assert(miga80_mod_validate(bad,i,&info)!=NULL); }
    memcpy(bad,original,n);bad[1080]='X';rejected(n,"4 CHANNELS");
    memcpy(bad,original,n);bad[950]=0;rejected(n,"SONG LENGTH");
    memcpy(bad,original,n);bad[1079]=128;rejected(n,"PATTERN ORDER");
    memcpy(bad,original,n);bad[44]=16;rejected(n,"FINETUNE");
    memcpy(bad,original,n);bad[45]=65;rejected(n,"VOLUME");
    memcpy(bad,original,n);bad[46]=0xff;bad[48]=0xff;rejected(n,"LOOP");
    memcpy(bad,original,n);bad[1084]=0xf0;rejected(n,"SAMPLE NUMBER");
    memcpy(bad,original,n);bad[1086]=(bad[1086]&0xf0)|8;rejected(n,"PANNING");
    memcpy(bad,original,n);bad[1086]=11;bad[1087]=8;rejected(n,"POSITION JUMP");
    memcpy(bad,original,n);bad[1086]=13;bad[1087]=0x64;rejected(n,"PATTERN BREAK");
    memcpy(bad,original,n);rejected(n+1,"TRAILING");
    for(i=0;i<30000;++i) {
        size_t pos;
        memcpy(bad,original,n);
        seed=seed*1664525U+1013904223U;pos=seed%n;
        seed=seed*1664525U+1013904223U;bad[pos]=(uint8_t)(seed>>24);
        (void)miga80_mod_validate(bad,n,&info);
    }
    assert(miga80_mod_validate(original,n,&info)==NULL);
    memset(bad,0xcc,n+2);miga80_mod_prepare(bad,original,&info);
    assert(memcmp(original,bad,n)==0 && bad[n]==0 && bad[n+1]==0);
    /* One-word empty samples must still consume their original file bytes. */
    memset(original,0,2114);memcpy(original+1080,"M.K.",4);original[950]=1;
    original[43]=1;original[73]=2;original[2108]=0xab;original[2109]=0xcd;
    original[2110]=1;original[2111]=2;original[2112]=3;original[2113]=4;
    assert(miga80_mod_validate(original,2114,&info)==NULL);
    miga80_mod_prepare(bad,original,&info);
    assert(bad[43]==0 && bad[2108]==1 && bad[2111]==4 && bad[2112]==0);
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        snprintf(fixture,sizeof(fixture),"function main():void %s end",invalid[i]);
        assert(!miga80_parse_function(fixture,strlen(fixture),&ast,&diag));
    }
    assert(miga80_parse_function(source,strlen(source),&ast,&diag));
    assert(ast.pool.entry_count==1 && ast.pool.entries[0].type==MIGA80_TYPE_STRING);
    assert(miga80_lower_function(&ast,&ir,&diag));assert(miga80_build_value_ir(&ir,&values,&diag));
    assert(miga80_evaluate_ir_with_runtime(&ir,args,3,&result,&rt,&diag));assert(result==89 && calls==5);
    for(mode=0;mode<3;++mode) {
        int ok=mode==0 ? miga80_encode_m68k_o0(code,sizeof(code),&ir,&size,&diag) :
            mode==1 ? miga80_encode_m68k_o1(code,sizeof(code),&values,&size,&diag) :
            miga80_encode_m68k_o1_guarded(code,sizeof(code),&values,&size,&bound,&diag);
        if(!ok) { fprintf(stderr,"%s\n",diag.message);return 1; }
        snprintf(path,sizeof(path),"%s/api%u.bin",argv[2],mode);f=fopen(path,"wb");assert(f);
        assert(fwrite(code,1,size,f)==size);fclose(f);
        snprintf(path,sizeof(path),"%s/api%u.s",argv[2],mode);f=fopen(path,"w");assert(f);
        assert(mode==0 ? miga80_emit_gnu_m68k(f,&ir,&diag) : mode==1 ?
            miga80_emit_gnu_m68k_o1(f,&values,&diag) : miga80_emit_gnu_m68k_o1_guarded(f,&values,&diag));fclose(f);
    }
    puts("PASS MOD bounds, all 20092 truncations, 30000 mutations, empty samples, Lua types and call order");
    return 0;
}
