// Test-only main linked with candidate objects. Includes the draw implementation to exercise its private view fallback.
#include <assert.h>
#include <sys/mman.h>
#include "../runtime/vk_exec_draw.c"
#include "../runtime/appendage_glyph.h"
#include "../runtime/input_glyphs.h"
#include "layout.h"
#include "nvn_tex.h"
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
    int failed=strcmp(argv[1],"normal") && strcmp(argv[1],"view");
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
    int validation = vk_ctx()->validation;
    vk_upload_flush(1); vk_mem_shutdown(); vk_shutdown();
    free(source.mem); munmap(main,0x2000000);
    assert(!vk_validation_errors_total());
    printf("Appendage GPU %s: pixel/readback or original fallback, copied view, retired node, slot reuse, shutdown passed; validation enabled %d, reported errors 0\n",argv[1],validation);
    return 0;
}
