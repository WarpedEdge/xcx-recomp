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

// Fits one licensed tile into the native glyph box (x, y, w, h) of a 46x50 inline-icon cell: the tile is cropped to its
// visible area, scaled to fit the box with its aspect kept, centred in it and area-resampled in premultiplied alpha so
// edges stay clean. Everything outside the fitted art is transparent. cell is the whole 46x50 RGBA8 cell.
int glyph_art_inline_cell(unsigned art, int x, int y, int w, int h, uint32_t *cell) {
    enum { CW = 46, CH = 50, T = 64 };
    if (art >= glyph_art_count() || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > CW || y + h > CH) return 0;
    uint32_t tile[T * T];
    const typeof(glyph_arts[0]) *a = &glyph_arts[art];
    size_t n = ZSTD_decompress(tile, sizeof tile, glyph_art_bytes + a->offset, a->length);
    if (ZSTD_isError(n) || n != sizeof tile) return 0;
    int x0 = T, y0 = T, x1 = -1, y1 = -1;
    for (int j = 0; j < T; j++) for (int i = 0; i < T; i++)
        if ((tile[j * T + i] >> 24) > 8) { if (i < x0) x0 = i; if (i > x1) x1 = i; if (j < y0) y0 = j; if (j > y1) y1 = j; }
    if (x1 < x0 || y1 < y0) return 0;
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    float scale = (float)w / (float)bw < (float)h / (float)bh ? (float)w / (float)bw : (float)h / (float)bh;
    int tw = (int)(bw * scale + 0.5f), th = (int)(bh * scale + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw > w) tw = w;
    if (th > h) th = h;
    int ox = x + (w - tw) / 2, oy = y + (h - th) / 2;
    memset(cell, 0, CW * CH * sizeof *cell);
    for (int j = 0; j < th; j++) for (int i = 0; i < tw; i++) {
        // Source rectangle of this destination texel, in tile pixels.
        float sx0 = x0 + (float)i * bw / tw, sx1 = x0 + (float)(i + 1) * bw / tw;
        float sy0 = y0 + (float)j * bh / th, sy1 = y0 + (float)(j + 1) * bh / th;
        float r = 0, g = 0, b = 0, al = 0, wsum = 0;
        for (int sy = (int)sy0; sy < (int)(sy1 + 0.999f) && sy < T; sy++) for (int sx = (int)sx0; sx < (int)(sx1 + 0.999f) && sx < T; sx++) {
            float cx = (sx + 1 < sx1 ? sx + 1 : sx1) - (sx > sx0 ? sx : sx0);
            float cy = (sy + 1 < sy1 ? sy + 1 : sy1) - (sy > sy0 ? sy : sy0);
            if (cx <= 0 || cy <= 0) continue;
            uint32_t p = tile[sy * T + sx];
            float pa = (float)(p >> 24) / 255.0f, wgt = cx * cy;
            r += (float)(p & 255) * pa * wgt; g += (float)(p >> 8 & 255) * pa * wgt; b += (float)(p >> 16 & 255) * pa * wgt;
            al += pa * wgt; wsum += wgt;
        }
        if (wsum <= 0 || al <= 0) continue;
        float out_a = al / wsum;
        uint32_t R = (uint32_t)(r / al + 0.5f), G = (uint32_t)(g / al + 0.5f), B = (uint32_t)(b / al + 0.5f), A = (uint32_t)(out_a * 255.0f + 0.5f);
        cell[(oy + j) * CW + ox + i] = (R > 255 ? 255 : R) | (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B) << 16 | (A > 255 ? 255 : A) << 24;
    }
    return 1;
}
