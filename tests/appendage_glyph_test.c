// Synthetic guest nodes exercise production policy/binding scope without guest code or game artwork.
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../runtime/appendage_glyph.c"
static InputMap mapping;
static int pad_layout = PAD_LAYOUT_XBOX, hash_ok = 1, source = GLYPH_SOURCE_PAD, family = GLYPH_PAD_PS;
static const char *preference = "playstation", *appearance_preference = "standard";
static const void *zl_record_ptr;
static uint64_t ticks, serial = 9, storage = 100, observed;
static unsigned observations;
static int probe_kind=-1;
static unsigned probe_handles;
void vk_glyph_resource_observe(uint64_t h, unsigned event) { assert(h==0x10400338 && event); probe_handles++; }
const RtFileHash rt_exefs_sha256[] = {{"main","0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026"}};
const int rt_exefs_sha256_count = 1;
const char *cfg_value(const char *name) { return !strcmp(name,"XCX_GLYPH_APPEARANCE") ? appearance_preference : preference; }
const char *cfg_get(const char *name) { (void)name; return NULL; }
void rt_log(const char *fmt, ...) { (void)fmt; }
void plat_once(PlatOnce *o, void (*fn)(void *), void *p) { if (!o->state) { o->state = 1; fn(p); } }
void plat_mutex_lock(PlatMutex *m) { (void)m; }
void plat_mutex_unlock(PlatMutex *m) { (void)m; }
int plat_mem_readable(uint64_t a, uint64_t n) { return a > 4096 && n <= 4096; }
uint64_t kern_now_ns(void) { return ticks; }
void input_glyph_snapshot(int *s, int *f, int *l, InputMap *m) { *s=source; *f=family; *l=pad_layout; *m=mapping; }
void sha256(const void *p, size_t n, uint8_t h[32]) {
    (void)p; assert(n == 128 || n == 84 || n == 28);
    if (n==128 && probe_kind>=0) { memcpy(h,probe_prefixes[probe_kind],32); return; }
    const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
      0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    const uint8_t record[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    const uint8_t x_record[32] = {0xe2,0xc3,0x5a,0xe2,0x8e,0x55,0x45,0xec,0xfb,0x66,0x58,0xb2,0x50,0x3f,0x3e,0x06,0x2f,0xd4,0x21,0x6c,0x43,0x26,0x09,0x0e,0xaf,0x49,0x6e,0xee,0x3b,0x5d,0xa4,0x6d};
    const uint8_t zl_record[32] = {0x7c,0xe0,0xa3,0xa3,0xa8,0x06,0x64,0x7b,0x28,0xe5,0x6f,0x64,0xb5,0x73,0x46,0xa6,0x36,0x05,0x00,0xbd,0xc5,0x42,0x05,0x25,0xca,0x7e,0x5d,0xe9,0x4f,0xad,0x4f,0xa9};
    memcpy(h, n==128 ? expected : n==28 ? (p==zl_record_ptr ? zl_record : x_record) : record, 32); if (!hash_ok) h[0] ^= 1;
}
int vk_appendage_capture_source(uint64_t h) { return appendage_glyph_source(h, serial, storage); }
static void draw(Cpu *c) {
    Cpu saved = *c; c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    observed = appendage_glyph_handle(c, 0x10400338);
    assert(appendage_glyph_handle(c,0x10400339)==0x10400339);
    c->x[30]++; assert(appendage_glyph_handle(c,0x10400338)==0x10400338);
    c->x[30]=saved.x[30]; assert(!memcmp(c,&saved,sizeof *c)); observations++;
}
static uint64_t node, resolved;
static void select_shared(Cpu *c) {
    Cpu saved=*c;
    _Alignas(16) uint64_t frame[2]={0,c->x[30]};
    c->x[0]=(uintptr_t)&resolved; c->x[29]=(uintptr_t)frame; c->x[30]=MOD_MAIN_BASE+0x55ff20;
    appendage_glyph_lookup_attach(c,c->x[29]); *c=saved;
}
static unsigned live_id(uint64_t node) {
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) return i+1;
    return 0;
}
static unsigned live_slot(uint64_t node) { unsigned id=live_id(node); return id ? live[id-1].slot : ~0u; }

static void nested(Cpu *c) {
    Cpu other = *c; other.x[30] = MOD_MAIN_BASE + 0x95c9cc;
    assert(appendage_glyph_handle(&other,0x10400338)==0x10400338);
    appendage_glyph_bind(c,node+1,draw); assert(observed==0x10400338);
    draw(c); assert(appendage_glyph_tagged(observed));
}
static void put64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }
int main(int argc, char **argv) {
    assert(appendage_glyph_art(APPENDAGE_GLYPH_TAG | (74ull<<32) | (192ull<<44))==74);
    assert(appendage_glyph_tagged(APPENDAGE_GLYPH_TAG | (74ull<<32) | (192ull<<44)));
    assert(!appendage_glyph_tagged(APPENDAGE_GLYPH_TAG | (1ull<<39)));
    assert(argc==2);
    uint8_t *main=mmap((void *)MOD_MAIN_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(main==(void *)MOD_MAIN_BASE);
    uint8_t *rect=mmap(NULL,6*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(rect!=MAP_FAILED);
    uint8_t resource[0x1000]={0}, object[0x38]={0}, wrapper[3][0x40]={{0}};
    uint32_t base_record=0x80, relative=0x100; memcpy(resource+0x2c,&base_record,4); memcpy(resource+0x98,&relative,4);
    zl_record_ptr=resource+base_record+relative+15*28;
    uint64_t table=(uintptr_t)object; uint32_t base=824; put64(main+0x1d64f18,(uintptr_t)&base);
    input_map_reset(); mapping=input_map; node=(uintptr_t)rect;
    const uint64_t returns[3]={0x60f690,0x60f6ac,0x60f6c8};
    Cpu c={0};
    for (int i=0;i<3;i++) {
        uint8_t *r=rect+i*4096;
        put64(r,MOD_MAIN_BASE+0x1c94318); r[0x18]=1; put64(r+0xe0,(uintptr_t)resource);
        put64(r+0x330,(uintptr_t)&table); put16(r+0x34c,selectors[i]); put16(r+0x34e,24);
        put64(wrapper[i]+0x38,(uintptr_t)r);
        c.x[0]=(uintptr_t)wrapper[i];c.x[1]=selectors[i];c.x[30]=MOD_MAIN_BASE+returns[i];appendage_glyph_attach(&c);
    }
    Cpu initial=c;
    if (!strcmp(argv[1],"resources")) {
        resource_probe=1;probe_deadline=180000000000ull;
        // Unrelated resources, wrong types and expired captures never enter the diagnostic scope.
        unsigned old_count=generation_count;
        appendage_glyph_bind(&c,node+3*4096,draw);assert(!probe_handles);
        uint8_t *r=rect+3*4096;memcpy(r,rect,0x350);probe_kind=0;
        r[0x18]=7;appendage_glyph_bind(&c,(uintptr_t)r,draw);assert(!probe_handles);r[0x18]=1;
        ticks=probe_deadline;appendage_glyph_bind(&c,(uintptr_t)r,draw);assert(!probe_handles);ticks=0;
        for (int kind=0;kind<4;kind++) {
            probe_kind=kind;
            for (int i=0;i<10;i++) {
                Cpu before=c;appendage_glyph_bind(&c,(uintptr_t)r,draw);
                assert(!memcmp(&c,&before,sizeof c));assert(observed==0x10400338);
            }
            assert(probe_counts[kind]==8);
        }
        assert(probe_handles==32 && probe_events==32 && generation_count==old_count);
        assert(!probe_scope.cpu && !probe_scope.event);
        appendage_glyph_retire((uintptr_t)r);assert(!mprotect(r,4096,PROT_NONE));
        // A later unrelated draw/handle observes no retired address or expired scope.
        probe_kind=-1;draw(&c);assert(probe_handles==32);
        resource_probe=0;
        puts("Bounded resource probe: four filters, unrelated/type rejection, CPU/original passthrough, 32-event/time bounds and no retained node passed");
        return 0;
    }
    if (!strcmp(argv[1],"guides")) {
        uint8_t *weapon=rect+3*4096,*field=rect+4*4096,*second=rect+5*4096;
        for(int i=3;i<6;i++) memcpy(rect+i*4096,rect,0x350);
        put16(weapon+0x34c,5);put16(field+0x34c,7);put16(second+0x34c,7);
        resolved=(uintptr_t)weapon;c.x[30]=MOD_MAIN_BASE+0x5a6664;c.x[1]=5;
        select_shared(&c);assert(!live_id(resolved)); // Adjacent unrelated caller.
        c.x[30]=MOD_MAIN_BASE+0x5a6660;
        appendage_glyph_lookup_attach(&c,c.x[29]);assert(!live_id(resolved)); // Wrong inner LR/frame.
        Cpu probe=c;probe.x[0]=(uintptr_t)&resolved;probe.x[30]=MOD_MAIN_BASE+0x55ff20;
        appendage_glyph_lookup_attach(&probe,0);appendage_glyph_lookup_attach(&probe,1);
        assert(!live_id(resolved)); // An unreadable or unaligned frame is not a selector context.
        Cpu before=c;select_shared(&c);assert(!memcmp(&c,&before,sizeof c));
        assert(live_slot(resolved)==3);unsigned first_count=generation_count;
        select_shared(&c);assert(generation_count==first_count);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_WEST));
        preference="auto";appearance_preference="colored";appendage_glyph_bind(&c,resolved,draw);
        assert(appendage_glyph_art(observed)==(unsigned)glyph_art_variant(glyph_art_find(GLYPH_PLAYSTATION,PAD_WEST),GLYPH_COLORED));
        assert(appendage_glyph_art(observed)>63);
        family=GLYPH_PAD_SWITCH;appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        family=GLYPH_PAD_PS;appearance_preference="standard";
        preference="keyboard";appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_KEYBOARD,KEY_SPACE));
        preference="playstation";resolved=(uintptr_t)field;c.x[30]=MOD_MAIN_BASE+0x5c6ed8;c.x[1]=7;
        hash_ok=0;select_shared(&c);assert(!live_id(resolved));hash_ok=1;
        field[0x18]=7;select_shared(&c);assert(!live_id(resolved));field[0x18]=1;
        select_shared(&c);assert(live_slot(resolved)==4);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_SOUTH));
        unsigned field_id=live_id(resolved);
        resolved=(uintptr_t)second;select_shared(&c);assert(live_slot(resolved)==4);
        // Retaining a second field instance cannot evict the first, including after its own retirement.
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        appendage_glyph_retire((uintptr_t)second);assert(live_id((uintptr_t)field)==field_id);
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        resolved=(uintptr_t)field;c.x[1]=16;select_shared(&c);assert(live_slot(resolved)==4);
        unsigned retained_count=generation_count;
        c.x[1]=14;put16(field+0x34c,14);select_shared(&c);assert(live_slot(resolved)==5);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_R));
        c.x[1]=15;put16(field+0x34c,15);select_shared(&c);assert(live_slot(resolved)==6);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_ZL));
        preference="keyboard";appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        preference="original";appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        preference="playstation";c.x[1]=7;put16(field+0x34c,7);select_shared(&c);
        assert(live_slot(resolved)==4&&generation_count==retained_count&&live_id(resolved)==field_id);
        appendage_glyph_retire((uintptr_t)weapon);assert(!mprotect(weapon,4096,PROT_NONE));
        appendage_glyph_bind(&c,(uintptr_t)weapon,draw);assert(observed==0x10400338);
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        assert(!mprotect(weapon,4096,PROT_READ|PROT_WRITE));
        resolved=(uintptr_t)weapon;c.x[1]=5;c.x[30]=MOD_MAIN_BASE+0x5a6660;select_shared(&c);
        assert(live_slot(resolved)==3&&generation_count>first_count);
        puts("saved-caller filtering, concurrent shared nodes, binding fallback, selector reuse, retirement and address reuse passed");return 0;
    }

    if (!strcmp(argv[1],"disabled")) {
        appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338&&!live_node[0]);puts("disabled fallback passed");return 0;
    }
    assert(live_generation[0]==1&&live_generation[1]==2&&live_generation[2]==3);
    appendage_glyph_bind(&c,node,nested);uint64_t first=observed;assert((uint32_t)first==0x10400338);
    assert(appendage_glyph_art(first)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_R));
    assert(!appendage_glyph_tagged(first^(1ull<<39)));
    for (int i=0;i<3;i++) {appendage_glyph_bind(&c,node+i*4096,draw);assert(appendage_glyph_tagged(observed));}
    if (control[0]) {
        FILE *f=fopen(control,"wb");assert(f);fputc('0',f);fclose(f);ticks+=100000001;
        appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
        f=fopen(control,"wb");assert(f);fputc('1',f);fclose(f);ticks+=100000001;
        appendage_glyph_bind(&c,node,draw);assert(observed==first);
    }
    hash_ok=0;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);hash_ok=1;
    uint32_t invalid_record=0xffffffff;memcpy(resource+0x2c,&invalid_record,4);
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);memcpy(resource+0x2c,&base_record,4);
    preference="original";appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    preference="auto";source=GLYPH_SOURCE_KEYBOARD;appendage_glyph_bind(&c,node,draw);
    assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_KEYBOARD,KEY_R));
    source=GLYPH_SOURCE_PAD;family=GLYPH_PAD_UNKNOWN;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    preference="playstation";family=GLYPH_PAD_PS;
    InputMap preserved=mapping;mapping.npads[1]=0;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);mapping=preserved;
    put16(rect+0x34c,6);appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);put16(rect+0x34c,14);
    appendage_glyph_retire(node+1);assert(live_node[0]==node);
    appendage_glyph_retire(node);assert(!mprotect(rect,4096,PROT_NONE));
    assert(strstr(appendage_glyph_runtime_note(),"waiting"));
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    assert(appendage_glyph_source(first,serial,storage));assert(!appendage_glyph_source(first,serial+1,storage));
    assert(!appendage_glyph_source(first,serial,storage+1));assert(!mprotect(rect,4096,PROT_READ|PROT_WRITE));
    c.x[0]=(uintptr_t)wrapper[0];c.x[1]=14;c.x[30]=MOD_MAIN_BASE+returns[0];
    hash_ok=0;appendage_glyph_attach(&c);assert(!live_node[0]);hash_ok=1;
    for (int i=3;i<GENERATIONS-3;i++) appendage_glyph_attach(&c);
    for (int i=0;i<3;i++) {
        c.x[0]=(uintptr_t)wrapper[i];c.x[1]=selectors[i];c.x[30]=MOD_MAIN_BASE+returns[i];
        appendage_glyph_attach(&c);
    }
    assert(generation_count==GENERATIONS && !appendage_glyph_runtime_note());
    appendage_glyph_bind(&c,node,draw);assert(appendage_glyph_tagged(observed));
    c.x[0]=(uintptr_t)wrapper[0];c.x[1]=14;c.x[30]=MOD_MAIN_BASE+returns[0];
    appendage_glyph_attach(&c);assert(!live_node[0] && generation_count==GENERATIONS);
    assert(strstr(appendage_glyph_runtime_note(),"history exhausted; restart"));
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    c=initial;assert(!memcmp(&mapping,&preserved,sizeof mapping));
    munmap(rect,6*4096);munmap(main,0x2000000);
    printf("all three nodes, scoped CPU preservation, policy/original fallback, inaccessible retired node, reuse and %d-generation limit passed (%u draws)\n",GENERATIONS,observations);
}
