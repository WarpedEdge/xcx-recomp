// Test-only main linked with candidate objects. Includes the draw implementation to exercise its private view fallback.
#include <assert.h>
#include <sys/mman.h>
#include "../runtime/vk_exec_draw.c"
#include "../runtime/appendage_glyph.h"
#include "../runtime/input_glyphs.h"
#include "layout.h"
#include "nvn_tex.h"
#include "inline_atlas_fixture.h"
static const void *fixture_prefix;
void __real_sha256(const void *, size_t, uint8_t *);
void __wrap_sha256(const void *p, size_t n, uint8_t h[32]) {
    if ((p != fixture_prefix || n != 128) && n != 84) { __real_sha256(p, n, h); return; }
    const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
      0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    const uint8_t record[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    memcpy(h, n==128 ? expected : record, 32);
}
static void write64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static void write16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void check_pixels(const VkTexImage *image, unsigned art) {
    VkCtx *c = vk_ctx(); const size_t bytes = 512 * 192 * 4;
    VkBuffer buffer; VmaAllocation alloc; VmaAllocationInfo info;
    VkBufferCreateInfo bi = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage=VMA_MEMORY_USAGE_AUTO,.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT};
    assert(vmaCreateBuffer(c->vma,&bi,&ai,&buffer,&alloc,&info)==VK_SUCCESS && info.pMappedData);
    VkCommandBuffer cb = vk_upload_begin();
    VkBufferImageCopy region = {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={512,192,1}};
    vkCmdCopyImageToBuffer(cb,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);
    VkMemoryBarrier2 mb = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT,.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT,.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo di = {.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&mb};
    vkCmdPipelineBarrier2(cb,&di); vk_upload_end(); vk_upload_flush(1);
    assert(vmaInvalidateAllocation(c->vma,alloc,0,bytes)==VK_SUCCESS);
    uint32_t *expected = calloc(1,bytes); assert(expected);
    assert(glyph_art_pixels(art, expected));
    assert(!memcmp(expected,info.pMappedData,bytes)); free(expected);
    vmaDestroyBuffer(c->vma,buffer,alloc);
}

// ---- inline atlas fixture: a 368x300 BC7 texture of solid mode-6 blocks, so the expected decode is known exactly.
static void put_bits(uint8_t *b, int *pos, uint32_t v, int n) { for (int i = 0; i < n; i++, (*pos)++) if (v >> i & 1) b[*pos >> 3] |= (uint8_t)(1u << (*pos & 7)); }
static void bc7_solid(uint8_t b[16], unsigned r, unsigned g, unsigned bl, unsigned a) {   // channels are 7-bit endpoint values
    memset(b, 0, 16); int pos = 0;
    put_bits(b, &pos, 1u << 6, 7);                                  // mode 6: six zero bits then a one
    unsigned v[4] = {r, g, bl, a};
    for (int c = 0; c < 4; c++) { put_bits(b, &pos, v[c], 7); put_bits(b, &pos, v[c], 7); }   // both endpoints equal
    put_bits(b, &pos, 0, 2);                                        // p-bits 0: decoded value is the 7-bit value * 2
    // 63 index bits stay 0: every texel takes endpoint 0.
}
static uint32_t block_rgba(int bx, int by) {
    unsigned r = (unsigned)(bx * 7 + 3) % 128, g = (unsigned)(by * 5 + 11) % 128, b = (unsigned)(bx + by * 3 + 1) % 128;
    return (r * 2) | (g * 2) << 8 | (b * 2) << 16 | 254u << 24;
}
static void read_image(const VkTexImage *image, unsigned w, unsigned h, size_t bytes, void *out) {
    VkCtx *c = vk_ctx();
    VkBuffer buffer; VmaAllocation alloc; VmaAllocationInfo info;
    VkBufferCreateInfo bi = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage=VMA_MEMORY_USAGE_AUTO,.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT};
    assert(vmaCreateBuffer(c->vma,&bi,&ai,&buffer,&alloc,&info)==VK_SUCCESS && info.pMappedData);
    VkCommandBuffer cb = vk_upload_begin();
    VkBufferImageCopy region = {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer(cb,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);
    VkMemoryBarrier2 mb = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT,.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT,.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo di = {.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&mb};
    vkCmdPipelineBarrier2(cb,&di); vk_upload_end(); vk_upload_flush(1);
    assert(vmaInvalidateAllocation(c->vma,alloc,0,bytes)==VK_SUCCESS);
    memcpy(out,info.pMappedData,bytes);
    vmaDestroyBuffer(c->vma,alloc?buffer:buffer,alloc);
}
static uint64_t inline_handle;
static void inline_import(Cpu *c) {   // the BindTexture import inside the guest bind helper
    uint64_t lr = c->x[30]; c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    inline_handle = appendage_glyph_handle(c, 0x10000000u | 825);
    c->x[30] = lr;
}
int __wrap_main(int argc, char **argv) {
    assert(argc==2); cfg_init();
    const char *settings = getenv("GLYPH_TEST_CONFIG"); assert(settings);
    FILE *file = fopen(settings,"wb"); assert(file);
    fputs("pad_layout = xbox\nbind_R = key:r,pad:r\n",file); fclose(file);
    assert(cfg_load(settings));
    cfg_set("XCX_GLYPH_STYLE","original"); cfg_set("XCX_GLYPH_APPEARANCE","colored"); assert(cfg_save_to(settings));
    assert(cfg_load(settings));
    assert(!strcmp(cfg_value("XCX_GLYPH_STYLE"),"original"));
    assert(!strcmp(cfg_value("XCX_GLYPH_APPEARANCE"),"colored"));
    assert(!strcmp(cfg_value("XCX_PAD_LAYOUT"),"xbox"));
    assert(!strcmp(cfg_value("XCX_BIND_R"),"key:r,pad:r"));
    assert(vk_init());
    uint8_t *main = mmap((void *)MOD_MAIN_BASE,0x2000000,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); assert(main==(void *)MOD_MAIN_BASE);
    uint8_t rect[0x350]={0},resource[0x1000]={0},object[0x38]={0},wrapper[0x40]={0};
    uint64_t table=(uintptr_t)object; uint32_t base=824;
    fixture_prefix=resource;
    uint32_t base_record=0x80,relative=0x100; memcpy(resource+0x2c,&base_record,4); memcpy(resource+0x98,&relative,4);
    write64(main+0x1d64f18,(uintptr_t)&base); write64(rect,MOD_MAIN_BASE+0x1c94318); rect[0x18]=1;
    write64(rect+0xe0,(uintptr_t)resource); write64(rect+0x330,(uintptr_t)&table);
    write16(rect+0x34c,14); write16(rect+0x34e,24); write64(wrapper+0x38,(uintptr_t)rect);
    Cpu cpu={0}; cpu.x[0]=(uintptr_t)wrapper; cpu.x[1]=14; cpu.x[30]=MOD_MAIN_BASE+0x60f690;
    appendage_glyph_attach(&cpu);
    pool_t pool={.flags=NVN_MEMORY_POOL_FLAGS_CPU_CACHED};
    tex_t source={.d={.target=NVN_TEXTURE_TARGET_2D,.format=77,.flags=NVN_TEXTURE_FLAGS_LINEAR,
      .width=512,.height=192,.depth=1,.levels=1},.pool=&pool,.serial=9};
    nvn_tex_layout(&source.d,&source.L); source.mem=calloc(1,source.L.size); assert(source.mem);
    vk_texture_register(824,&source,NULL);
    uint64_t handle=APPENDAGE_GLYPH_TAG|(1ull<<44)|0x10400338;
    assert(vk_appendage_capture_source(handle));
    vk_texture_lock();
    const VkTexImage *original=vk_texture_by_id_locked(824,NULL,NULL);
    assert(original);
    const VkTexImage *chosen=vk_appendage_texture_locked(handle,NULL,NULL);
    int failed=strcmp(argv[1],"normal") && strcmp(argv[1],"view") && strcmp(argv[1],"blit");   // blit only affects the inline atlas
    assert(chosen && ((chosen==original)==failed));
    VkExec ex={0}; GfxShaderRes res={.type=2}; VkDescriptorImageInfo out; VkImage image;
    tex_info_full(&ex,&res,handle,0,0,&out,&image);
    assert(out.imageView && image==(failed || !strcmp(argv[1],"view") ? original->image : chosen->image));
    VkDescriptorImageInfo copied=out;
    appendage_glyph_retire((uintptr_t)rect); memset(rect,0xa5,sizeof rect);
    assert(vk_appendage_texture_locked(handle,NULL,NULL)==chosen); // Copied command survives the node.
    assert(copied.imageView==out.imageView);
    if (!failed) check_pixels(chosen,0);
    if (!strcmp(argv[1],"normal")) for (unsigned art=1;art<glyph_art_count();art++) {
        uint64_t h=handle | (uint64_t)art<<32;
        const VkTexImage *variant=vk_appendage_texture_locked(h,NULL,NULL);
        assert(variant && variant != original);
        tex_info_full(&ex,&res,h,0,0,&out,&image); assert(out.imageView && image==variant->image);
        check_pixels(variant,art);
    }
    vk_texture_unlock();
    source.serial++; vk_texture_register(824,&source,NULL);
    assert(!vk_appendage_capture_source(handle));
    vk_texture_lock();
    assert(vk_appendage_texture_locked(handle,NULL,NULL)==vk_texture_by_id_locked(824,NULL,NULL));
    vk_texture_unlock();

    // ---- inline text-icon atlas: guest arm -> tag -> capture -> composite on the GPU -> readback.
    {
        const unsigned AW = INLINE_ATLAS_W, AH = INLINE_ATLAS_H, BW = AW / 4, BH = AH / 4;
        tex_t atlas = {.d = {.target = NVN_TEXTURE_TARGET_2D, .format = INLINE_NVN_FORMAT, .flags = NVN_TEXTURE_FLAGS_LINEAR,
                             .width = AW, .height = AH, .depth = 1, .levels = 1, .stride = BW * 16}, .pool = &pool, .serial = 31};
        nvn_tex_layout(&atlas.d, &atlas.L); atlas.mem = calloc(1, atlas.L.size); assert(atlas.mem);
        uint8_t *blocks = malloc((size_t)BW * BH * 16); assert(blocks);
        for (unsigned by = 0; by < BH; by++) for (unsigned bx = 0; bx < BW; bx++) {
            uint32_t col = block_rgba((int)bx, (int)by);
            bc7_solid(blocks + ((size_t)by * BW + bx) * 16, (col & 255) / 2, (col >> 8 & 255) / 2, (col >> 16 & 255) / 2, 127);
            memcpy(atlas.mem + (size_t)by * BW * 16 + (size_t)bx * 16, blocks + ((size_t)by * BW + bx) * 16, 16);
        }
        vk_texture_register(825, &atlas, NULL);
        // Guest-side fixture: node -> source -> table, texture set -> entry 0 naming the atlas storage.
        static _Alignas(16) uint8_t X[0x60], T[0x20 + INLINE_RECORDS * INLINE_RECORD_BYTES], tset[0x40], entries[0x78], node[0x400];
        inline_atlas_records(T + 0x20); { uint32_t off = 0x20; memcpy(T + 0x18, &off, 4); }
        write64(X + 0x38, (uintptr_t)T); { uint32_t cnt = INLINE_RECORDS; memcpy(X + 0x48, &cnt, 4); float a = INLINE_SOURCE_SCALE_A, b = INLINE_SOURCE_SCALE_B; memcpy(X + 0x4c, &a, 4); memcpy(X + 0x50, &b, 4); }
        write64(X + 0x40, (uintptr_t)tset); write64(tset + 0x30, (uintptr_t)entries);
        write64(entries, INLINE_ENTRY_Q0); write64(entries + 0x20, (uintptr_t)atlas.mem); write64(node + 0x3b8, (uintptr_t)X);
        cfg_set("XCX_GLYPH_STYLE", "keyboard"); cfg_set("XCX_GLYPH_APPEARANCE", "standard");
        assert(cfg_save_to(settings)); assert(cfg_load(settings));   // no gamepad here, so the keyboard source is the one that replaces
        Cpu b = {0}; b.x[19] = (uintptr_t)node; b.x[1] = (uintptr_t)entries; b.x[30] = MOD_MAIN_BASE + 0xa325fc;
        appendage_glyph_icon_bind(&b, inline_import);
        const int source_stage = !strcmp(argv[1], "source");
        const int inline_expected_base = strcmp(argv[1], "pixels") && strcmp(argv[1], "image") && strcmp(argv[1], "upload") && strcmp(argv[1], "blit") && !source_stage;
        assert((inline_handle & 0xffffffffu) == (0x10000000u | 825));
        assert(appendage_inline_tagged(inline_handle) || source_stage);   // a refused capture hands the guest its own handle back
        const int tagged = appendage_inline_tagged(inline_handle);
        const int inline_expected = tagged && inline_expected_base;
        int8_t arts[INLINE_ACTIONS] = {0};
        if (tagged) assert(appendage_inline_signature(appendage_inline_id(inline_handle), arts));
        vk_texture_lock();
        const VkTexImage *native = vk_texture_by_id_locked(825, NULL, NULL); assert(native);
        const VkTexImage *picked = vk_appendage_texture_locked(inline_handle, NULL, NULL);
        assert(picked && ((picked == native) == !inline_expected));
        VkExec ex = {0}; GfxShaderRes res = {.type = 2}; VkDescriptorImageInfo out; VkImage image;
        tex_info_full(&ex, &res, inline_handle, 0, 0, &out, &image);
        assert(out.imageView && image == (inline_expected && strcmp(argv[1], "view") ? picked->image : native->image));
        if (inline_expected) {
            // Expected image: the native decode everywhere, the fitted tiles in the replaced cells only.
            uint32_t *want = malloc((size_t)AW * AH * 4), *got = malloc((size_t)AW * AH * 4); assert(want && got);
            for (unsigned y = 0; y < AH; y++) for (unsigned x = 0; x < AW; x++) want[y * AW + x] = block_rgba((int)x / 4, (int)y / 4);
            unsigned replaced = 0, native_kept = 0;
            for (unsigned k = 0; k < INLINE_CELLS; k++) {
                const InlineCell *ic = &inline_cells_geom[k];
                if (arts[ic->action] < 0) { native_kept++; continue; }
                uint32_t cell[INLINE_CELL_W * INLINE_CELL_H];
                assert(glyph_art_inline_cell((unsigned)arts[ic->action], ic->x, ic->y, ic->w, ic->h, cell));
                unsigned cx = (ic->record % INLINE_COLUMNS) * INLINE_CELL_W, cy = (ic->record / INLINE_COLUMNS) * INLINE_CELL_H;
                for (unsigned y = 0; y < INLINE_CELL_H; y++) for (unsigned x = 0; x < INLINE_CELL_W; x++) want[(cy + y) * AW + cx + x] = cell[y * INLINE_CELL_W + x];
                replaced++;
            }
            assert(replaced >= 4 && picked != native);
            read_image(picked, AW, AH, (size_t)AW * AH * 4, got);
            for (unsigned i = 0; i < AW * AH; i++) if (want[i] != got[i]) { fprintf(stderr, "pixel %u,%u: want %08x got %08x\n", i % AW, i / AW, want[i], got[i]); assert(0); }
            // The native atlas still holds exactly its original BC7 blocks: nothing was written to it.
            uint8_t *raw = malloc((size_t)BW * BH * 16); assert(raw);
            read_image(native, AW, AH, (size_t)BW * BH * 16, raw);
            assert(!memcmp(raw, blocks, (size_t)BW * BH * 16));
            free(raw); free(want); free(got);
            // A second resolution returns the same owned image; a new signature gets its own image and leaves this one alone.
            assert(vk_appendage_texture_locked(inline_handle, NULL, NULL) == picked);
            cfg_set("XCX_BIND_R", "key:t,pad:r"); assert(cfg_save_to(settings)); assert(cfg_load(settings));   // another binding, another signature
            appendage_glyph_icon_bind(&b, inline_import);
            assert(appendage_inline_id(inline_handle) == 2);
            const VkTexImage *second = vk_appendage_texture_locked(inline_handle, NULL, NULL);
            assert(second && second != picked && second != native);
            uint32_t *cmp = malloc((size_t)AW * AH * 4), *again = malloc((size_t)AW * AH * 4); assert(cmp && again);
            read_image(second, AW, AH, (size_t)AW * AH * 4, cmp);
            read_image(picked, AW, AH, (size_t)AW * AH * 4, again);
            assert(memcmp(cmp, again, (size_t)AW * AH * 4));                      // different art in the replaced cells
            for (unsigned y = 0; y < AH; y++) for (unsigned x = 0; x < AW; x++)  // untouched cells equal the native decode in both
                if (x / 46 == 0 && y / 50 == 0) assert(cmp[y * AW + x] == block_rgba((int)x / 4, (int)y / 4) && again[y * AW + x] == cmp[y * AW + x]);
            free(cmp); free(again);
            cfg_set("XCX_BIND_R", "key:r,pad:r"); assert(cfg_save_to(settings)); assert(cfg_load(settings));
        }
        vk_texture_unlock();
        // Identity: the same handle against a different registration (new serial, or another storage) falls back to native.
        atlas.serial++; vk_texture_register(825, &atlas, NULL);
        vk_texture_lock();
        assert(vk_appendage_texture_locked(inline_handle, NULL, NULL) == vk_texture_by_id_locked(825, NULL, NULL));
        vk_texture_unlock();
        write64(entries + 0x20, (uintptr_t)atlas.mem ^ 0x1000);   // the entry names storage the slot does not have
        appendage_glyph_icon_bind(&b, inline_import);
        assert(!appendage_inline_tagged(inline_handle));
        free(blocks); free(atlas.mem);
    }
    int validation = vk_ctx()->validation;
    vk_upload_flush(1); vk_mem_shutdown(); vk_shutdown();
    free(source.mem); munmap(main,0x2000000);
    assert(!vk_validation_errors_total());
    printf("Appendage GPU %s: pixel/readback or original fallback, copied view, retired node, slot reuse, inline atlas composite/native-untouched/fallback, shutdown passed; validation enabled %d, reported errors 0\n",argv[1],validation);
    return 0;
}
