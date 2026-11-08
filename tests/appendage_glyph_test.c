// Synthetic guest nodes exercise production policy/binding scope without guest code or game artwork.
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../runtime/appendage_glyph.c"
static InputMap mapping;
static int pad_layout = PAD_LAYOUT_XBOX, hash_ok = 1, source = GLYPH_SOURCE_PAD, family = GLYPH_PAD_PS;
static const char *preference = "playstation";
static uint64_t ticks, serial = 9, storage = 100, observed;
static unsigned observations;
const RtFileHash rt_exefs_sha256[] = {{"main","0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026"}};
const int rt_exefs_sha256_count = 1;
const char *cfg_value(const char *name) { (void)name; return preference; }
const char *cfg_get(const char *name) { (void)name; return NULL; }
void rt_log(const char *fmt, ...) { (void)fmt; }
void plat_once(PlatOnce *o, void (*fn)(void *), void *p) { if (!o->state) { o->state = 1; fn(p); } }
void plat_mutex_lock(PlatMutex *m) { (void)m; }
void plat_mutex_unlock(PlatMutex *m) { (void)m; }
int plat_mem_readable(uint64_t a, uint64_t n) { return a > 4096 && n <= 4096; }
uint64_t kern_now_ns(void) { return ticks; }
void input_glyph_snapshot(int *s, int *f, int *l, InputMap *m) { *s=source; *f=family; *l=pad_layout; *m=mapping; }
void sha256(const void *p, size_t n, uint8_t h[32]) {
    (void)p; assert(n == 128 || n == 84);
    const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
      0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    const uint8_t record[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    memcpy(h, n==128 ? expected : record, 32); if (!hash_ok) h[0] ^= 1;
}
int vk_appendage_capture_source(uint64_t h) { return appendage_glyph_source(h, serial, storage); }
static void draw(Cpu *c) {
    Cpu saved = *c; c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    observed = appendage_glyph_handle(c, 0x10400338);
    assert(appendage_glyph_handle(c,0x10400339)==0x10400339);
    c->x[30]++; assert(appendage_glyph_handle(c,0x10400338)==0x10400338);
    c->x[30]=saved.x[30]; assert(!memcmp(c,&saved,sizeof *c)); observations++;
}
static uint64_t node;
static void nested(Cpu *c) {
    Cpu other = *c; other.x[30] = MOD_MAIN_BASE + 0x95c9cc;
    assert(appendage_glyph_handle(&other,0x10400338)==0x10400338);
    appendage_glyph_bind(c,node+1,draw); assert(observed==0x10400338);
    draw(c); assert(appendage_glyph_tagged(observed));
}
static void put64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }
int main(int argc, char **argv) {
    assert(argc==2);
    uint8_t *main=mmap((void *)MOD_MAIN_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(main==(void *)MOD_MAIN_BASE);
    uint8_t *rect=mmap(NULL,3*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(rect!=MAP_FAILED);
    uint8_t resource[0x1000]={0}, object[0x38]={0}, wrapper[3][0x40]={{0}};
    uint32_t base_record=0x80, relative=0x100; memcpy(resource+0x2c,&base_record,4); memcpy(resource+0x98,&relative,4);
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
    if (!strcmp(argv[1],"disabled")) {
        appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338&&!live_node[0]);puts("disabled fallback passed");return 0;
    }
    assert(live_generation[0]==1&&live_generation[1]==2&&live_generation[2]==3);
    appendage_glyph_bind(&c,node,nested);uint64_t first=observed;assert((uint32_t)first==0x10400338);
    assert(appendage_glyph_art(first)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_R));
    assert(!appendage_glyph_tagged(first^(1ull<<38)));
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
    munmap(rect,3*4096);munmap(main,0x2000000);
    printf("all three nodes, scoped CPU preservation, policy/original fallback, inaccessible retired node, reuse and %d-generation limit passed (%u draws)\n",GENERATIONS,observations);
}
