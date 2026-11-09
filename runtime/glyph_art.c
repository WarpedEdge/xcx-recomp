// Fixed licensed tiles, copied only into five verified HUD button atlas regions.
#include <string.h>
#include <zstd.h>
#include "input_glyphs.h"
#include "glyph_art.inc"
unsigned glyph_art_count(void) { return sizeof glyph_arts / sizeof *glyph_arts; }
int glyph_art_find(int style, int physical) {
    for (unsigned i = 0; i < glyph_art_count(); i++)
        if (glyph_arts[i].style == style && glyph_arts[i].input == physical && glyph_arts[i].appearance == GLYPH_STANDARD) return (int)i;
    return -1;
}
int glyph_art_variant(int art, int appearance) {
    if (art < 0 || (unsigned)art >= glyph_art_count()) return -1;
    if (appearance == GLYPH_COLORED) for (unsigned i=0;i<glyph_art_count();i++)
        if (glyph_arts[i].style == glyph_arts[art].style && glyph_arts[i].input == glyph_arts[art].input &&
            glyph_arts[i].appearance == GLYPH_COLORED) return (int)i;
    return art;
}
const char *glyph_art_name(unsigned art) { return art < glyph_art_count() ? glyph_arts[art].name : "original"; }
int glyph_art_pixels(unsigned art, uint32_t *pixels) {
    if (art >= glyph_art_count()) return 0;
    uint32_t tile[64*64];
    const typeof(glyph_arts[0]) *a = &glyph_arts[art];
    size_t n = ZSTD_decompress(tile,sizeof tile,glyph_art_bytes+a->offset,a->length);
    if (ZSTD_isError(n) || n != sizeof tile) return 0;
    memset(pixels,0,512*192*4);
    // Verified half-texel origins: X=(320.5,.5), Y=(384.5,.5), A=(448.5,.5), R=(384.5,64.5), ZL=(448.5,64.5).
    for (unsigned row = 0; row < 64; row++) {
        memcpy(pixels+row*512+320,tile+row*64,256); // Shared weapon-change X.
        memcpy(pixels+row*512+384,tile+row*64,256);
        memcpy(pixels+row*512+448,tile+row*64,256);
        memcpy(pixels+(row+64)*512+384,tile+row*64,256);
        memcpy(pixels+(row+64)*512+448,tile+row*64,256); // Shared Board ZL.
    }
    return 1;
}
