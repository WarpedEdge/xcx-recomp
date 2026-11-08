#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "mod_settings.h"
#include "platform/platform.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static atomic_flag gate=ATOMIC_FLAG_INIT;
static ModSnapshot snapshot;
static ModAction queue[MOD_ACTION_MAX];
static int queued,active,stopped;
static char directory[4096];
static void take(void) { while (atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)) {} }
static void release(void) { atomic_flag_clear_explicit(&gate,memory_order_release); }
void mod_ui_reset(const char *root) {
    take(); uint64_t generation=snapshot.generation+1; memset(&snapshot,0,sizeof snapshot);snapshot.generation=generation;snapshot.session=1;
    queued=active=stopped=0;snprintf(directory,sizeof directory,"%s",root?root:""); release();
}
void mod_ui_publish(const ModView *view) {
    take(); int slot=-1;
    for (int i=0;i<snapshot.count;++i) if (!strcmp(view->id,snapshot.mods[i].id)) slot=i;
    if (slot<0 && snapshot.count<MOD_UI_MAX) slot=snapshot.count++;
    if (slot>=0 && !stopped) snapshot.mods[slot]=*view;
    release();
}
void mod_ui_snapshot(ModSnapshot *out) { take();*out=snapshot;out->pending=queued;release(); }
void mod_ui_interaction(int enabled) {
    take(); if (active!=enabled) {active=enabled;++snapshot.session;if (!active) queued=0;} release();
}
int mod_ui_submit(const ModAction *a) {
    int ok=0;take();
    if (!memchr(a->mod,0,sizeof a->mod) || !memchr(a->field,0,sizeof a->field)) goto done;
    if (!active || stopped || a->generation!=snapshot.generation || a->session!=snapshot.session) goto done;
    int m=-1,f=-1;
    for (int i=0;i<snapshot.count;++i) if (!strcmp(a->mod,snapshot.mods[i].id)) m=i;
    if (m<0 || snapshot.mods[m].status!=MOD_LOADED) goto done;
    for (int i=0;i<snapshot.mods[m].field_count;++i) if (!strcmp(a->field,snapshot.mods[m].fields[i].id)) f=i;
    if (f<0) goto done;
    ModField *field=&snapshot.mods[m].fields[f];
    if (a->type!=field->type || a->value<field->minimum || a->value>field->maximum) goto done;
    for (int i=0;i<queued;++i) if (!strcmp(a->mod,queue[i].mod) && !strcmp(a->field,queue[i].field)) {queue[i]=*a;ok=1;goto done;}
    if (queued<MOD_ACTION_MAX) {queue[queued++]=*a;ok=1;snapshot.notice[0]=0;}
    else snprintf(snapshot.notice,sizeof snapshot.notice,"Settings queue is full; try again.");
 done: release();return ok;
}
int mod_ui_action_current(const ModAction *a) {take();int ok=!stopped&&active&&a->generation==snapshot.generation&&a->session==snapshot.session;release();return ok;}
int mod_ui_pop(ModAction *out) { take();int ok=queued>0&&!stopped;if(ok) {*out=queue[0];memmove(queue,queue+1,(size_t)(--queued)*sizeof *queue);} release();return ok; }
void mod_ui_shutdown(void) { take();stopped=1;active=0;queued=0;++snapshot.session;release(); }
static int setting_path(const char *id,char *out,size_t size) {
    if (!directory[0]) return 0;
    return snprintf(out,size,"%s/%s.ini",directory,id)<(int)size;
}
static int stored_value(ModField *f,const char *text,int *value) {
    if (f->type==MOD_ENUM) {
        if (strncmp(text,"e:",2)) return 0;
        for (int i=0;i<f->option_count;++i) if (!strcmp(text+2,f->options[i].id)) {*value=i;return 1;}
        return 0;
    }
    if (strlen(text)<2) return 0;
    if (text[0]!=(f->type==MOD_BOOL?'b':'i') || text[1]!=':') return 0;
    errno=0;char *end;long n=strtol(text+2,&end,10);
    if (errno || end==text+2 || *end || n<f->minimum || n>f->maximum) return 0;
    *value=(int)n;return 1;
}
void mod_settings_load(ModView *view) {
    char path[4096],data[4097];if (!setting_path(view->id,path,sizeof path)) return;
    FILE *f=fopen(path,"rb");if (!f) return;
    size_t n=fread(data,1,sizeof data,f);int read_error=ferror(f);fclose(f);
    if (read_error || n>4096 || memchr(data,0,n)) {snprintf(view->save_note,sizeof view->save_note,"Stored settings rejected; using defaults.");return;}
    data[n]=0;
    ModView candidate=*view;unsigned seen=0;int valid=1;
    char *line=data;int first=1;
    while (line) {
        char *next=strchr(line,'\n');if(next)*next++=0;size_t len=strlen(line);if(len&&line[len-1]=='\r')line[--len]=0;
        if (first) {valid=!strcmp(line,"version=1");first=0;}
        else if (*line) {
            char *value=strchr(line,'=');if(!value){valid=0;break;}*value++=0;
            for(int i=0;i<candidate.field_count;++i) if(!strcmp(line,candidate.fields[i].id)) {
                if ((seen&(1u<<i)) || !stored_value(&candidate.fields[i],value,&candidate.fields[i].value)) valid=0;
                seen|=1u<<i;
            }
        }
        line=next;
    }
    if (valid) *view=candidate;
    else snprintf(view->save_note,sizeof view->save_note,"Stored settings rejected; using defaults.");
}
int mod_settings_save(ModView *view) {
    char path[4096],data[4096];if(!setting_path(view->id,path,sizeof path)) {snprintf(view->save_note,sizeof view->save_note,"Persistence unavailable; session values only.");return 0;}
    int at=snprintf(data,sizeof data,"version=1\n");
    for(int i=0;i<view->field_count;++i) {
        ModField *f=&view->fields[i];
        if(f->type==MOD_ENUM) at+=snprintf(data+at,sizeof(data)-(size_t)at,"%s=e:%s\n",f->id,f->options[f->value].id);
        else at+=snprintf(data+at,sizeof(data)-(size_t)at,"%s=%c:%d\n",f->id,f->type==MOD_BOOL?'b':'i',f->value);
    }
    plat_file_mkdir_p(directory);
    int ok=plat_file_write_atomic(path,data,(size_t)at);
    snprintf(view->save_note,sizeof view->save_note,"%s",ok?"Saved":"Save failed; previous file preserved. Session value retained.");return ok;
}
