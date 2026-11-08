// Optional native libraries. Every call is serialized by the mod worker, with no host lock held.
#include "mod_provider.h"
#include "mod_provider_platform.h"
#include "sha256.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define PROVIDERS 4
#ifndef XMP_STOP_TIMEOUT_MS
#define XMP_STOP_TIMEOUT_MS 5000u
#endif
typedef struct {
    char owner_id[65],id[65],hash[65],build[65];uint64_t owner,required,instance;
    XmpApi api;int valid,stopping,stopped;
    ModProviderFile file;
} Provider;
struct ModProviders {uint64_t generation;ModLog log;int count;Provider entries[PROVIDERS];};
// Process-lifetime mapping ownership, including ABI-rejected modules. Never dlclose/FreeLibrary.
static struct {void *module;intptr_t file;char hash[65],id[65];} mapped[PROVIDERS];
static int mapped_count;
static uint64_t generation;
static int id(const char *s) {
    size_t n=strlen(s);if(!n||n>64||*s<'a'||*s>'z')return 0;
    for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return 0;
    return 1;
}
static void report(ModProviders *p,const char *owner,const char *reason) {
    char line[384];snprintf(line,sizeof line,"providers: %.64s: %.256s",owner,reason);p->log(line);
}
static int zero(const void *data,size_t n){const unsigned char *b=data;for(size_t i=0;i<n;++i)if(b[i])return 0;return 1;}
static int hash_ok(const char *s){if(strlen(s)!=64)return 0;for(;*s;++s)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')))return 0;return 1;}
static char *next_line(char *line) {char *n=strchr(line,'\n');if(n)*n++=0;size_t k=strlen(line);if(k&&line[k-1]=='\r')line[k-1]=0;return n;}
static int metadata(Provider *p,char *text) {
    unsigned fields=0;char owner[65]={0},provider[65]={0};
    for(char *line=text;line;) {
        char *next=next_line(line);
        if(*line&&*line!='#') {
            char *v=strchr(line,'=');if(!v)return 0;*v++=0;unsigned bit=0;
            if(!strcmp(line,"schema")){bit=1;if(strcmp(v,"1"))return 0;}
            else if(!strcmp(line,"id")){bit=2;if(!id(v))return 0;strcpy(provider,v);}
            else if(!strcmp(line,"owner")){bit=4;if(!id(v))return 0;strcpy(owner,v);}
            else if(!strcmp(line,"abi")){bit=8;if(strcmp(v,"1.0"))return 0;}
            else if(!strcmp(line,"required")){bit=16;errno=0;char *end;if(!*v||*v<'0'||*v>'9')return 0;p->required=strtoull(v,&end,10);if(errno||*end)return 0;}
            else if(!strcmp(line,"linux-sha256")||!strcmp(line,"windows-sha256")) {
                bit=!strcmp(line,"linux-sha256")?32:64;if(!hash_ok(v))return 0;
#ifdef _WIN32
                if(bit==64)strcpy(p->hash,v);
#else
                if(bit==32)strcpy(p->hash,v);
#endif
            }else return 0;
            if(fields&bit)return 0;fields|=bit;
        }line=next;
    }
    return (fields&31)==31&&p->hash[0]&&!strcmp(owner,p->owner_id)&&!strcmp(provider,p->id);
}
static int preflight(ModProviders *ctx,Provider *p,const char *root) {
    ModProviderFile meta;int result=mod_provider_read(root,p->owner_id,"provider.ini",4096,&meta);
    if(result!=1)return 0;
    int ok=!memchr(meta.data,0,meta.size)&&metadata(p,(char *)meta.data);mod_provider_file_close(&meta);if(!ok)return 0;
#ifdef _WIN32
    const char *binary="native/windows-x86_64/provider.dll";
#else
    const char *binary="native/linux-x86_64/provider.so";
#endif
    if(mod_provider_read(root,p->owner_id,binary,32u*1024*1024,&p->file)!=1)return 0;
    unsigned char digest[32];char hex[65];sha256(p->file.data,p->file.size,digest);
    for(int i=0;i<32;++i)snprintf(hex+2*i,3,"%02x",digest[i]);
    if(strcmp(hex,p->hash)||!mod_provider_image(&p->file)){mod_provider_file_close(&p->file);return 0;}
    (void)ctx;return 1;
}
static void *map(Provider *p) {
    for(int i=0;i<mapped_count;++i)if(!strcmp(mapped[i].id,p->id)){mod_provider_file_close(&p->file);return !strcmp(mapped[i].hash,p->hash)?mapped[i].module:NULL;}
    if(mapped_count==PROVIDERS){mod_provider_file_close(&p->file);return NULL;}
    void *module=mod_provider_map(&p->file);
    if(module){mapped[mapped_count].module=module;mapped[mapped_count].file=p->file.file;strcpy(mapped[mapped_count].id,p->id);strcpy(mapped[mapped_count++].hash,p->hash);p->file.file=-1;}
    mod_provider_file_close(&p->file);return module;
}
static int describe(Provider *p,void *module) {
    XmpQuery query=(XmpQuery)mod_provider_symbol(module,"xcx_provider_query_v1");
    if(!query)return 0;
    memset(&p->api,0,sizeof p->api);p->api.size=sizeof p->api;
    if(query(XMP_MAJOR,XMP_MINOR,&p->api)!=XMP_OK||p->api.size!=sizeof p->api||p->api.major!=XMP_MAJOR||p->api.minor!=XMP_MINOR||p->api.reserved||!p->api.describe||!p->api.create_idle||!p->api.request_stop||!p->api.join_destroy)return 0;
    XmpInfo info={0};info.size=sizeof info;
    if(p->api.describe(&info)!=XMP_OK||info.size!=sizeof info||info.major!=XMP_MAJOR||info.minor!=XMP_MINOR||info.reserved||!zero(info.reserved2,sizeof info.reserved2)||!zero(info.padding,sizeof info.padding)||info.platform!=XMP_PLATFORM||info.architecture!=XMP_ARCH_X64||!memchr(info.provider_id,0,sizeof info.provider_id)||!memchr(info.build_id,0,sizeof info.build_id)||strcmp(info.provider_id,p->id)||!id(info.build_id)||(info.capabilities&p->required)!=p->required)return 0;
    strcpy(p->build,info.build_id);return 1;
}
ModProviders *mod_providers_start(const ModPlan *plan,ModLog log) {
    ModProviders *ctx=calloc(1,sizeof *ctx);if(!ctx){log("providers: native loader allocation failed; no providers loaded");return NULL;}
    ctx->log=log;if(generation==UINT64_MAX){free(ctx);return NULL;}ctx->generation=++generation;
    ModProviderFile activation;int r=mod_provider_read(plan->root,NULL,"providers-enabled.txt",2048,&activation);
    if(r!=1){if(r<0)report(ctx,"activation","unsafe or unreadable list; no providers loaded");return ctx;}
    int invalid=memchr(activation.data,0,activation.size)!=NULL;char owners[16][65],ids[16][65];int count=0;
    for(char *line=(char *)activation.data;line&&!invalid;) {
        char *next=next_line(line);
        if(*line&&*line!='#') {
            char *sep=strchr(line,':');if(!sep||count==16){invalid=1;break;}*sep++=0;
            if(!id(line)||!id(sep)){invalid=1;break;}
            for(int i=0;i<count;++i)if(!strcmp(owners[i],line)||!strcmp(ids[i],sep))invalid=1;
            strcpy(owners[count],line);strcpy(ids[count++],sep);
        }line=next;
    }
    mod_provider_file_close(&activation);
    if(invalid){report(ctx,"activation","invalid or duplicate association; no providers loaded");return ctx;}
    for(int n=0;n<count;++n) {
        int owner=-1;for(int i=0;i<plan->count;++i)if(!strcmp(plan->mods[i].id,owners[n])&&plan->mods[i].enabled&&!plan->mods[i].rejected)owner=i;
        if(owner<0){report(ctx,owners[n],"owner absent/disabled/rejected; not loaded");continue;}
        if(ctx->count==PROVIDERS){report(ctx,"activation","more than four eligible providers; no providers loaded");for(int i=0;i<ctx->count;++i)mod_provider_file_close(&ctx->entries[i].file);ctx->count=0;return ctx;}
        Provider *p=&ctx->entries[ctx->count++];p->file.file=-1;p->owner=(uint64_t)owner+1;strcpy(p->owner_id,owners[n]);strcpy(p->id,ids[n]);
        p->valid=preflight(ctx,p,plan->root);if(!p->valid)report(ctx,p->owner_id,"metadata, path, hash, image or dependency preflight rejected");
    }
    if(ctx->count && !mod_provider_environment()) {
        report(ctx,"activation","unsupported native dependency environment; no providers loaded");
        for(int i=0;i<ctx->count;++i){mod_provider_file_close(&ctx->entries[i].file);ctx->entries[i].valid=0;}
        return ctx;
    }
    // Complete all package preflight before mapping any library.
    for(int i=0;i<ctx->count;++i) {
        Provider *p=&ctx->entries[i];if(!p->valid)continue;p->valid=0;
        void *module=map(p);if(!module||!describe(p,module)){report(ctx,p->owner_id,"mapping or ABI rejected; mapped library retained");continue;}
        XmpCreate config={0};config.size=sizeof config;config.major=XMP_MAJOR;config.minor=XMP_MINOR;config.owner=p->owner;config.generation=ctx->generation;config.required_capabilities=p->required;strcpy(config.package_id,p->owner_id);
        XmpResult result=p->api.create_idle(&config,&p->instance);
        if(result!=XMP_OK||!p->instance){report(ctx,p->owner_id,"idle create failed (requires complete provider rollback)");if(p->instance){p->valid=1;report(ctx,p->owner_id,"provider violated failed-create token contract; retained for stop");}continue;}
        p->valid=1;char ready[128];snprintf(ready,sizeof ready,"idle provider ready build=%s",p->build);report(ctx,p->owner_id,ready);
    }
    return ctx;
}
int mod_providers_token(ModProviders *p,uint64_t owner,ModProviderToken *out) {
    if(!p||!out)return 0;
    for(int i=0;i<p->count;++i)if(p->entries[i].owner==owner&&p->entries[i].valid&&!p->entries[i].stopped){*out=(ModProviderToken){p->generation,owner,(uint32_t)i,0};return 1;}
    return 0;
}
static int stop_one(ModProviders *p,Provider *s,uint64_t deadline) {
    if(!s->valid||s->stopped)return 1;
    if(!s->stopping){s->stopping=1;if(s->api.request_stop(s->instance)!=XMP_OK){report(p,s->owner_id,"stop request failed; instance retained");return 0;}}
    do {
        XmpResult r=s->api.join_destroy(s->instance,0);
        if(r==XMP_OK){s->stopped=1;s->instance=0;report(p,s->owner_id,"provider destroyed before Lua teardown");return 1;}
        if(r!=XMP_BUSY){report(p,s->owner_id,"join failed; instance retained");return 0;}
        if(mod_provider_clock_ms()>=deadline)break;mod_provider_sleep();
    }while(1);
    report(p,s->owner_id,"cleanup timeout; live instance retained");return 0;
}
int mod_providers_stop_owner(ModProviders *p,uint64_t owner,ModProviderToken token) {
    if(!p||token.reserved||token.generation!=p->generation||token.owner!=owner||token.slot>=(uint32_t)p->count||p->entries[token.slot].owner!=owner)return 0;
    return stop_one(p,&p->entries[token.slot],mod_provider_clock_ms()+XMP_STOP_TIMEOUT_MS);
}
int mod_providers_stop(ModProviders *p) {
    if(!p)return 1;int ok=1;uint64_t deadline=mod_provider_clock_ms()+XMP_STOP_TIMEOUT_MS;
    for(int i=p->count-1;i>=0;--i){Provider *s=&p->entries[i];if(s->valid&&!s->stopped&&!s->stopping){s->stopping=1;if(s->api.request_stop(s->instance)!=XMP_OK)ok=0;}}
    for(int i=p->count-1;i>=0;--i)if(!stop_one(p,&p->entries[i],deadline))ok=0;
    return ok;
}
void mod_providers_free(ModProviders *p) {
    if(!p)return;for(int i=0;i<p->count;++i)if(p->entries[i].valid&&!p->entries[i].stopped){report(p,p->entries[i].owner_id,"refused free of live provider context");return;}free(p);
}
