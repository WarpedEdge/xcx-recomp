// Serialized Lua host domain. Source and event data are copied; no engine ABI is exposed.
#include "mod_internal.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CALL_BUDGET 20000
#define DISPATCH_BUDGET 25000
typedef union { size_t capacity; long double align; void *pointer; } Allocation;
typedef struct {
    struct ModRuntime *runtime;
    ModPackage *package;
    lua_State *L;
    size_t used, peak, attempts, denied;
    int refs[3], failed, initialized, phase, instructions, logs;
} ModState;
struct ModRuntime {
    ModPlan *plan;
    ModLog log;
    ModState states[MOD_LIMIT];
    int order[MOD_LIMIT], count, aggregate, event_cursor;
};
static const char *phase_names[]={"state","setup","load","chunk","descriptor","init","event","shutdown","close"};
static ModState *owner(lua_State *L) { return *(ModState **)lua_getextraspace(L); }
static void message(ModState *m,const char *kind,const char *s,size_t n) {
    char line[512];
    int at=snprintf(line,sizeof line,"mods: %s %s %s: ",m->package->id,kind,phase_names[m->phase]);
    if (n>256) n=256;
    for (size_t i=0;i<n && at<(int)sizeof(line)-1;++i) { unsigned char c=(unsigned char)s[i]; line[at++]=c>=32 && c!=127 ? (char)c : '?'; }
    line[at]=0; m->runtime->log(line);
}
static void fail(ModState *m,const char *s) { message(m,"error",s,strlen(s)); m->failed=1; }
static void *allocate(void *context,void *ptr,size_t old,size_t n) {
    ModState *m=context; Allocation *block=ptr?(Allocation *)ptr-1:NULL;
    size_t capacity=block?block->capacity:0; (void)old;
    if (!n) { free(block); m->used-=capacity; return NULL; }
    if (n<=capacity) return ptr; // Lua requires shrinking requests to succeed.
    ++m->attempts;
#ifdef XCX_MOD_TESTING
    const char *id=getenv("MOD_FAIL_ID"), *phase=getenv("MOD_FAIL_PHASE"), *after=getenv("MOD_FAIL_AFTER");
    if (id && phase && !strcmp(id,m->package->id) && !strcmp(phase,phase_names[m->phase]) && m->attempts>(after?strtoul(after,NULL,10):0)) { ++m->denied; return NULL; }
#endif
    if (n-capacity>MOD_MEMORY_LIMIT-m->used) { ++m->denied; return NULL; }
    Allocation *p=realloc(block,sizeof(*p)+n); if (!p) return NULL;
    p->capacity=n; m->used=m->used-capacity+n; if (m->used>m->peak) m->peak=m->used;
    return p+1;
}
static void hook(lua_State *L,lua_Debug *ar) {
    (void)ar; ModState *m=owner(L); m->instructions-=100;
    if (m->phase==6) { m->runtime->aggregate-=100; if (m->runtime->aggregate<=0) luaL_error(L,"aggregate dispatch budget exceeded"); }
    if (m->instructions<=0) luaL_error(L,"instruction budget exceeded");
}
static int log_message(lua_State *L) {
    ModState *m=owner(L); size_t n; luaL_checktype(L,1,LUA_TSTRING);
    const char *s=lua_tolstring(L,1,&n); if (n>256) return luaL_error(L,"log exceeds 256 bytes");
    if (m->logs++<16) message(m,"log",s,n); return 0;
}
static void select_fields(lua_State *L,int original,const char *const *names) {
    original=lua_absindex(L,original); lua_newtable(L);
    for (;*names;++names) { lua_getfield(L,original,*names); lua_setfield(L,-2,*names); }
}
static int setup(lua_State *L) {
    static const char *const base[]={"assert","error","ipairs","next","pairs","select","tonumber","tostring","type","_VERSION",NULL};
    static const char *const strings[]={"byte","char","len","lower","reverse","sub","upper",NULL};
    static const char *const tables[]={"concat","insert","pack","remove","unpack",NULL};
    static const char *const maths[]={"abs","ceil","floor","max","min","sqrt","huge","maxinteger","mininteger","pi","tointeger","type",NULL};
    luaopen_base(L); select_fields(L,-1,base); lua_remove(L,-2);
    lua_pushvalue(L,-1); lua_setfield(L,-2,"_G"); lua_rawseti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    luaopen_string(L); select_fields(L,-1,strings); lua_remove(L,-2);
    // String method lookup must use the restricted table too.
    lua_pushliteral(L,""); lua_getmetatable(L,-1); lua_pushvalue(L,-3); lua_setfield(L,-2,"__index"); lua_pop(L,2); lua_setglobal(L,"string");
    luaopen_table(L); select_fields(L,-1,tables); lua_remove(L,-2); lua_setglobal(L,"table");
    luaopen_math(L); select_fields(L,-1,maths); lua_remove(L,-2); lua_setglobal(L,"math");
    lua_newtable(L); lua_pushcfunction(L,log_message); lua_setfield(L,-2,"log");
    lua_pushinteger(L,1); lua_setfield(L,-2,"api"); lua_pushstring(L,owner(L)->package->id); lua_setfield(L,-2,"id"); lua_setglobal(L,"host");
    return 0;
}
static int load(lua_State *L) {
    ModPackage *p=owner(L)->package;
    if (luaL_loadbufferx(L,p->source,p->source_size,p->id,"t")!=LUA_OK) return lua_error(L);
    return 1;
}
static int discover(lua_State *L) {
    ModState *m=owner(L); luaL_checktype(L,1,LUA_TTABLE);
    lua_pushliteral(L,"shutdown"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[2]=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_pushliteral(L,"init"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[0]=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_pushliteral(L,"events"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L,-2)) {
        size_t size=0; const char *name=lua_type(L,-2)==LUA_TSTRING?lua_tolstring(L,-2,&size):NULL;
        if (!name || size!=12 || memcmp(name,"host.started",12)) return luaL_error(L,"unsupported event name");
        lua_pop(L,1);
    }
    lua_pushliteral(L,"host.started"); lua_rawget(L,-2); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[1]=luaL_ref(L,LUA_REGISTRYINDEX);
    return 0;
}
static int invoke(lua_State *L) {
    ModState *m=owner(L); int slot=m->phase==5?0:m->phase==6?1:2;
    lua_rawgeti(L,LUA_REGISTRYINDEX,m->refs[slot]);
    if (slot==1) { lua_newtable(L); lua_pushinteger(L,1); lua_setfield(L,-2,"api"); lua_pushliteral(L,"startup"); lua_setfield(L,-2,"phase"); }
    lua_call(L,slot==1,0); return 0;
}
static int call(ModState *m,lua_CFunction fn,int args,int results,int phase) {
    m->phase=phase; m->instructions=CALL_BUDGET; m->attempts=m->denied=0;
    // Zero-upvalue C functions don't allocate. Arguments are prepared inside the trampoline.
    if (fn) { lua_pushcfunction(m->L,fn); if (args) lua_insert(m->L,-1-args); }
    lua_sethook(m->L,hook,LUA_MASKCOUNT,100);
    int status=lua_pcall(m->L,args,results,0); lua_sethook(m->L,NULL,0,0);
    if (status!=LUA_OK) {
        size_t n=0; const char *s=lua_type(m->L,-1)==LUA_TSTRING?lua_tolstring(m->L,-1,&n):NULL;
        message(m,"error",s?s:"non-string error",s?n:16); m->failed=1; lua_pop(m->L,1);
    }
#ifdef XCX_MOD_TESTING
    char line[160]; snprintf(line,sizeof line,"alloc %s %s attempts=%zu denied=%zu",m->package->id,phase_names[phase],m->attempts,m->denied); m->runtime->log(line);
#endif
    return status==LUA_OK;
}
static int unavailable(ModRuntime *rt,ModState *m) {
    for (int i=0;i<rt->plan->count;++i) if ((m->package->dependencies&(1u<<i)) && (!rt->states[i].initialized || rt->states[i].failed)) return 1;
    return 0;
}
ModRuntime *mod_runtime_start(ModPlan *plan,ModLog log,ModCancelled cancelled) {
    ModRuntime *rt=calloc(1,sizeof *rt); if (!rt) { log("mods: runtime allocation failed"); return NULL; }
    rt->plan=plan; rt->log=log;
    unsigned processed=0;
    for (int i=0;i<plan->count;++i) {
        ModState *m=&rt->states[i]; m->runtime=rt; m->package=&plan->mods[i]; m->failed=m->package->rejected;
        for (int r=0;r<3;++r) m->refs[r]=LUA_NOREF;
        if (m->failed) processed|=1u<<i;
    }
    for (int step=0;step<plan->count;++step) {
        int chosen=-1;
        for (int i=0;i<plan->count;++i) if (!(processed&(1u<<i)) && (plan->mods[i].dependencies&processed)==plan->mods[i].dependencies) { chosen=i; break; }
        if (chosen<0 || (cancelled && cancelled())) break;
        processed|=1u<<chosen; rt->order[rt->count++]=chosen; ModState *m=&rt->states[chosen];
        if (unavailable(rt,m)) { fail(m,"dependency unavailable"); continue; }
        m->L=lua_newstate(allocate,m,0);
#ifdef XCX_MOD_TESTING
        char state_line[160]; snprintf(state_line,sizeof state_line,"alloc %s state attempts=%zu denied=%zu",m->package->id,m->attempts,m->denied); log(state_line);
#endif
        if (!m->L) { fail(m,"state allocation failed"); continue; }
        *(ModState **)lua_getextraspace(m->L)=m;
        if (call(m,setup,0,0,1) && call(m,load,0,1,2) && call(m,NULL,0,1,3) && call(m,discover,1,0,4) && call(m,invoke,0,0,5)) {
            m->initialized=1; message(m,"ready",m->package->version,strlen(m->package->version));
        }
    }
    return rt;
}
int mod_runtime_event(ModRuntime *rt,ModCancelled cancelled) {
    if (!rt) return 0;
    rt->aggregate=DISPATCH_BUDGET;
    while (rt->event_cursor<rt->count && rt->aggregate>=100 && !(cancelled && cancelled())) {
        ModState *m=&rt->states[rt->order[rt->event_cursor++]];
        if (m->failed || !m->initialized) continue;
        if (unavailable(rt,m)) { m->phase=6; fail(m,"dependency unavailable"); continue; }
        rt->aggregate-=100; call(m,invoke,0,0,6);
    }
    return rt->event_cursor<rt->count && !(cancelled && cancelled());
}
void mod_runtime_stop(ModRuntime *rt) {
    if (!rt) return;
    for (int n=rt->count-1;n>=0;--n) {
        ModState *m=&rt->states[rt->order[n]];
        m->initialized=0;
        if (m->L) {
            if (m->refs[2]!=LUA_NOREF) call(m,invoke,0,0,7);
            m->phase=8; m->attempts=m->denied=0; lua_close(m->L); m->L=NULL;
        }
        for (int r=0;r<3;++r) m->refs[r]=LUA_NOREF;
        char line[192]; snprintf(line,sizeof line,"mods: %s closed bytes=%zu callbacks=0",m->package->id,m->used); rt->log(line);
        if (m->used) rt->log("mods: allocator accounting violation");
    }
    free(rt);
}
