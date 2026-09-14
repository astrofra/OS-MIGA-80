#include <stdio.h>
#include <string.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/audio.h>
#include "audio/mod.h"
#include "demo/music_host.h"
#define MUSIC_TOTAL_LIMIT (512U * 1024U)
struct music_asset {
    uint8_t *source, *replay;
    struct miga80_mod_info info;
};
struct miga80_host_music {
    struct Task *owner;
    struct music_asset assets[MIGA80_MAX_POOL_ENTRIES];
    LONG bit;
    volatile ULONG pending, argument;
    ULONG starts;
    int installed;
    UBYTE filter;
};
extern int miga80_pt_install(void);
extern void mt_remove(void);
extern void miga80_pt_start(void *,void *,void *);
extern void miga80_pt_stop(void);
extern void miga80_pt_mute(ULONG mask);
extern ULONG miga80_pt_position(void);
extern volatile ULONG miga80_pt_ticks, miga80_pt_dma_seen;
extern void miga80_runtime_music_play(void),miga80_runtime_music_stop(void);
extern void miga80_runtime_music_mute(void),miga80_runtime_music_position(void);

static int service_music(void *data,ULONG signals)
{
    struct miga80_host_music *m=data;
    ULONG command=m->pending,id=m->argument;
    (void)signals;
    if(command==1 && id<MIGA80_MAX_POOL_ENTRIES && m->assets[id].source!=NULL) {
        struct music_asset *asset=&m->assets[id];
        miga80_pt_stop();
        miga80_mod_prepare(asset->replay,asset->source,&asset->info);
        miga80_pt_start(asset->replay,asset->replay+asset->info.sample_offset,
                        asset->replay+asset->info.file_bytes);
        ++m->starts;
    } else if(command==2) { miga80_pt_stop(); }
    else if(command==3) { miga80_pt_mute(id&15U); }
    m->pending=0;
    return 1;
}
/* Worker publishes only a bounded command; the owner performs all OS work. */
static void submit(struct miga80_host_music *m,ULONG command,ULONG argument)
{
    if(m==NULL) return;
    m->argument=argument;m->pending=command;
    if(FindTask(NULL)==m->owner) { (void)service_music(m,0); }
    else {
        Signal(m->owner,1UL<<m->bit);
        while(m->pending!=0) { __asm__ volatile ("" ::: "memory"); }
    }
}
void miga80_music_play(struct miga80_host_music *m,uint32_t id) { submit(m,1,id); }
void miga80_music_stop(struct miga80_host_music *m) { submit(m,2,0); }
void miga80_music_mute(struct miga80_host_music *m,uint32_t mask) { submit(m,3,mask); }
uint32_t miga80_music_position(struct miga80_host_music *m)
{ return m==NULL ? UINT32_MAX : miga80_pt_position(); }

void miga80_host_music_destroy(struct miga80_host_music *m,struct miga80_music_stats *stats)
{
    unsigned int i;
    if(m==NULL) return;
    if(m->installed) {
        volatile struct CIA *cia=(volatile struct CIA *)0xbfe001;
        if(stats!=NULL) { stats->ticks=miga80_pt_ticks;stats->position=miga80_pt_position();stats->starts=m->starts;stats->dma_seen=miga80_pt_dma_seen; }
        miga80_pt_stop();
        mt_remove();
        /* Unregister CIA callbacks before returning or freeing any sample RAM. */
        cia->ciapra=(cia->ciapra&~2U)|m->filter;
    }
    if(m->bit>=0) { SetSignal(0,1UL<<m->bit);FreeSignal(m->bit); }
    for(i=0;i<MIGA80_MAX_POOL_ENTRIES;++i) {
        struct music_asset *a=&m->assets[i];
        if(a->replay!=NULL) FreeMem(a->replay,a->info.file_bytes+2U);
        if(a->source!=NULL) FreeMem(a->source,a->info.file_bytes);
    }
    FreeMem(m,sizeof(*m));
}

int miga80_host_music_create(const struct miga80_ast_function *ast,
    struct miga80_drawing_context *context,struct miga80_supervisor_events *events,
    struct miga80_host_music **result,char *error,size_t capacity)
{
    struct miga80_host_music *m=NULL;
    unsigned int i;
    ULONG total=0;
    const char *reason=NULL;
    *result=NULL;
    context->music_state=0;
    context->music_play_handler=(uint32_t)(uintptr_t)miga80_runtime_music_play;
    context->music_stop_handler=(uint32_t)(uintptr_t)miga80_runtime_music_stop;
    context->music_mute_handler=(uint32_t)(uintptr_t)miga80_runtime_music_mute;
    context->music_position_handler=(uint32_t)(uintptr_t)miga80_runtime_music_position;
    for(i=0;i<ast->statement_count;++i) {
        const struct miga80_ast_statement *statement=&ast->statements[i];
        unsigned int id;
        const struct miga80_pool_entry *entry;
        struct music_asset *a;
        char path[MIGA80_MAX_POOL_BYTES+1];
        FILE *file;
        long bytes;
        if(statement->kind!=MIGA80_AST_CALL_MUSIC_PLAY) continue;
        if(m==NULL) {
            m=AllocMem(sizeof(*m),MEMF_PUBLIC|MEMF_CLEAR);
            if(m==NULL) { reason="MOD OWNER MEMORY";goto fail; }
            m->bit=-1;m->owner=FindTask(NULL);
        }
        id=ast->nodes[statement->arguments[0]].value;
        if(id>=ast->pool.entry_count) { reason="MOD INVALID RESOURCE";goto fail; }
        a=&m->assets[id];
        if(a->source!=NULL) continue;
        entry=&ast->pool.entries[id];
        if(entry->type!=MIGA80_TYPE_STRING || entry->length==0 || entry->length>=sizeof(path) ||
            memchr(ast->pool.bytes+entry->offset,0,entry->length)!=NULL) {
            reason="MOD INVALID PATH";goto fail;
        }
        memcpy(path,ast->pool.bytes+entry->offset,entry->length);path[entry->length]=0;
        file=fopen(path,"rb");
        if(file==NULL) { reason="MOD FILE NOT FOUND";goto fail; }
        if(fseek(file,0,SEEK_END)!=0 || (bytes=ftell(file))<1084 ||
            bytes>(long)MIGA80_MOD_MAX_BYTES || fseek(file,0,SEEK_SET)!=0) {
            fclose(file);reason="MOD INVALID SIZE (MAX 256K)";goto fail;
        }
        if(total+2U*(ULONG)bytes+2U>MUSIC_TOTAL_LIMIT) {
            fclose(file);reason="MOD ASSETS EXCEED 512K";goto fail;
        }
        a->info.file_bytes=(uint32_t)bytes;
        a->source=AllocMem((ULONG)bytes,MEMF_PUBLIC);
        if(a->source==NULL) { fclose(file);reason="MOD FILE MEMORY";goto fail; }
        if(fread(a->source,1,(size_t)bytes,file)!=(size_t)bytes) {
            fclose(file);reason="MOD READ ERROR";goto fail;
        }
        fclose(file);
        reason=miga80_mod_validate(a->source,(size_t)bytes,&a->info);
        if(reason!=NULL) goto fail;
        a->replay=AllocMem((ULONG)bytes+2U,MEMF_CHIP|MEMF_CLEAR);
        if(a->replay==NULL) { reason="MOD CHIP MEMORY";goto fail; }
        total+=2U*(ULONG)bytes+2U;
    }
    if(m==NULL) return 1;
    m->bit=AllocSignal(-1);
    if(m->bit<0) { reason="MOD SIGNAL UNAVAILABLE";goto fail; }
    m->filter=((volatile struct CIA *)0xbfe001)->ciapra&2U;
    if(!miga80_pt_install()) { reason="MOD AUDIO / CIA BUSY";goto fail; }
    m->installed=1;
    events->signals=1UL<<m->bit;events->service=service_music;events->data=m;
    context->music_state=(uint32_t)(uintptr_t)m;
    *result=m;
    return 1;
fail:
    (void)snprintf(error,capacity,"ERROR - %s",reason);
    miga80_host_music_destroy(m,NULL);
    return 0;
}

/* Emulator regression, run only by the SOLID*TEST modes. */
const char *miga80_host_music_selftest(void)
{
    struct miga80_ast_function *ast=AllocMem(sizeof(*ast),MEMF_PUBLIC|MEMF_CLEAR);
    struct miga80_drawing_context context;
    struct miga80_supervisor_events events={0};
    struct miga80_host_music *m=NULL;
    struct miga80_diagnostic diagnostic;
    struct miga80_music_stats stats={0};
    struct MsgPort *port=NULL;
    struct IOAudio *audio=NULL;
    UBYTE mask=15;
    int opened=0,success=0;
    const char *failure="music_test_parse";
    ULONG baseline,ticks;
    char error[160];
    const char *source="function main():void music_play(\"SYS:mods/93_10_12_A_SYNTH_1.mod\") end";
    if(ast==NULL) return "music_test_memory";
    if(!miga80_parse_function(source,strlen(source),ast,&diagnostic)) goto done;
    /* audio.device at equal priority must prevent ptplayer from stealing it. */
    failure="music_test_audio_reserve";
    port=CreateMsgPort();
    if(port==NULL) goto done;
    audio=(struct IOAudio *)CreateIORequest(port,sizeof(*audio));
    if(audio==NULL) goto done;
    audio->ioa_Request.io_Message.mn_Node.ln_Pri=ADALLOC_MINPREC;
    audio->ioa_Request.io_Flags=ADIOF_NOWAIT;audio->ioa_Data=&mask;audio->ioa_Length=1;
    if(OpenDevice(AUDIONAME,0,(struct IORequest *)audio,0)!=0) goto done;
    opened=1;
    failure="music_test_audio_busy";
    if(miga80_host_music_create(ast,&context,&events,&m,error,sizeof(error)) ||
        strcmp(error,"ERROR - MOD AUDIO / CIA BUSY")!=0) goto done;
    CloseDevice((struct IORequest *)audio);opened=0;
    DeleteIORequest((struct IORequest *)audio);audio=NULL;
    DeleteMsgPort(port);port=NULL;
    failure="music_test_install";
    baseline=AvailMem(MEMF_PUBLIC);
    if(!miga80_host_music_create(ast,&context,&events,&m,error,sizeof(error))) goto done;
    failure="music_test_initial_position";
    if(miga80_music_position(m)!=UINT32_MAX) goto done;
    miga80_music_play(m,0);
    Delay(20);
    failure="music_test_play";
    if(miga80_music_position(m)<2U || miga80_pt_ticks<15U || !miga80_pt_dma_seen) goto done;
    miga80_music_mute(m,15);Delay(5);miga80_music_mute(m,0);
    miga80_music_stop(m);ticks=miga80_pt_ticks;
    Delay(3); /* exceed both delayed DMA IRQs: a stop must stay stopped */
    failure="music_test_stop";
    if(miga80_music_position(m)!=UINT32_MAX || miga80_pt_ticks!=ticks ||
        (((volatile struct Custom *)0xdff000)->dmaconr&15U)!=0) goto done;
    miga80_music_play(m,0);
    Delay(20);
    failure="music_test_restart";
    if(miga80_music_position(m)<2U || miga80_music_position(m)>4U) goto done;
    /* Valid module with notes/arp/retrigger/funk before any instrument.
     * Reuse the test's owned copy; never modify the file or user asset. */
    miga80_music_stop(m);
    memset(m->assets[0].source,0,m->assets[0].info.file_bytes);
    {
        uint8_t *b=m->assets[0].source;
        b[42]=0x1d;b[43]=0x20;b[49]=1;b[950]=1;b[1079]=3;
        memcpy(b+1080,"M.K.",4);
        b[1084]=1;b[1085]=172;b[1091]=0x37;
        b[1094]=14;b[1095]=255;b[1098]=14;b[1099]=0x93;
        failure="music_test_empty_module";
        if(miga80_mod_validate(b,m->assets[0].info.file_bytes,&m->assets[0].info)!=NULL) goto done;
    }
    miga80_music_play(m,0);Delay(20);
    if(miga80_music_position(m)<2U || miga80_music_position(m)>4U) goto done;
    miga80_host_music_destroy(m,&stats);m=NULL;
    Delay(3);
    failure="music_test_cleanup";
    if(stats.starts!=3 || (((volatile struct Custom *)0xdff000)->dmaconr&15U)!=0 ||
        AvailMem(MEMF_PUBLIC)<baseline) goto done;
    /* DOS failure must also leave no audio/timer/signal allocation behind. */
    /* SYS is already mounted: a cold RAM: handler would change OS memory. */
    failure="music_test_missing";
    source="function main():void music_play(\"SYS:MIGA80-NO-SUCH-MODULE.mod\") end";
    if(!miga80_parse_function(source,strlen(source),ast,&diagnostic) ||
        miga80_host_music_create(ast,&context,&events,&m,error,sizeof(error)) ||
        strcmp(error,"ERROR - MOD FILE NOT FOUND")!=0 || AvailMem(MEMF_PUBLIC)<baseline) goto done;
    success=1;
done:
    miga80_host_music_destroy(m,NULL);
    if(opened) CloseDevice((struct IORequest *)audio);
    if(audio!=NULL) DeleteIORequest((struct IORequest *)audio);
    if(port!=NULL) DeleteMsgPort(port);
    FreeMem(ast,sizeof(*ast));
    return success ? NULL : failure;
}
