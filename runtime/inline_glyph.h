// Verified facts about the shared inline-icon source (XCX DE 2.0.0 main). Geometry and identity only: no game pixels.
//
// Source and tag mapping (offline message tables and msbp enum, runtime capture of six tags, closed-form grid):
//   * `[ST:icon p1=<tag> ]` becomes `[ML:icon ...]`; f_7000a21afc looks the code up in a 48-record table at `source+0x38`.
//   * The code is the record index; the msbp icon enum order gives code = position: ... pls 4, x 5, y 6, a 7, b 8, ...
//     l 13, r 14, zl 15, zr 16, ..., and the same 24 again as the large variants (+24: x_l 29, a_l 31, l_l 37, ...).
//   * The records are a closed form over an 8x6 grid of 46x50 cells in a 368x300 BC7 texture, NVN format 77:
//     {1, 1, 45/368, 149/900, (col*46+0.5)/368, (row*50+0.5)/300, u16 46, u16 50}; INLINE_RECORDS_SHA256 is that table.
//   * The draw's second pass binds entry 0 of the source's texture set; its first qwords are 0x012c0170'0000004d (height
//     300, width 368, format 0x4d) and the texture storage address at qword 4.
// The atlas and the icon_button sheet draw the same 24 symbols in the same order (the msbp tag order from `crs`); a record or
// cell index modulo 24 is the symbol (input_glyphs.h SYM_*). Only the replaceable symbols' cells are ever replaced; power, home and
// capture, and every cell not listed here, keep their native pixels.
#pragma once
#include <stdint.h>

enum { INLINE_ATLAS_W = 368, INLINE_ATLAS_H = 300, INLINE_NVN_FORMAT = 77, INLINE_CELL_W = 46, INLINE_CELL_H = 50,
       INLINE_COLUMNS = 8, INLINE_RECORDS = 48, INLINE_RECORD_BYTES = 28, INLINE_SIGNATURES = 48,
       SHEET_W = 512, SHEET_H = 192, SHEET_CELL = 64, SHEET_RECORDS = 24, SHEET_COLUMNS = 8,
       STICKS_W = 352, STICKS_H = 88, STICKS_CELL = 88, STICKS_RECORDS = 4, STICKS_COLUMNS = 4, INLINE_MAX_CELLS = 48 };
#define INLINE_ENTRY_Q0 0x012c01700000004dull
#define INLINE_SOURCE_SCALE_A 50.0f   // source+0x4c
#define INLINE_SOURCE_SCALE_B 46.0f   // source+0x50
#define INLINE_RECORDS_SHA256 {0xe5,0x53,0x74,0x5f,0x74,0x9a,0x12,0xf1,0xed,0x76,0x12,0x70,0x2c,0xb6,0x1f,0x0c, \
                               0x24,0x44,0x27,0xae,0x9d,0x62,0x6d,0x32,0xce,0x2c,0xe5,0xde,0x05,0x41,0xba,0xc0}

// icon_button sheet: 24 records of 28 bytes at the resource header's table; SHA-256 of the whole table (the 24 records of the
// pinned image). The resource object itself is identified by the HUD route's existing 128-byte hash guard.
#define SHEET_RECORDS_SHA256 {0x8c,0x6c,0x97,0x83,0x12,0xbd,0x39,0xbf,0x8b,0xb8,0x41,0xa1,0xb2,0x89,0x13,0x85,0x0a,0x70,0xfa,0x39,0x57,0x77,0xa1,0xba,0xbe,0xa6,0xd6,0xee,0x4e,0x99,0xaa,0x36}

// Native glyph box (x, y, w, h) of one symbol inside its cell: the alpha bounding box of the native art, measured on the decoded
// pinned image. The licensed tile is fitted into this box (see glyph_art_cell). Geometry only; no pixels.
typedef struct { uint8_t x, y, w, h; } InlineBox;
// Cells of the 368x300 atlas: records 0..23 standard, 24..47 large, symbol = record % 24.
static const InlineBox inline_font_boxes[2][24] = {
    {
    {5, 7, 36, 36}, {9, 9, 28, 31}, {7, 10, 30, 30}, {7, 10, 30, 30},   // crs pow hom mns
    {8, 10, 30, 30}, {8, 10, 30, 30}, {8, 10, 30, 30}, {8, 10, 30, 30},   // pls x y a
    {7, 10, 30, 30}, {6, 4, 33, 42}, {6, 4, 33, 42}, {6, 7, 34, 35},   // b analp anarp anal
    {6, 7, 34, 35}, {7, 11, 32, 28}, {7, 11, 32, 28}, {7, 9, 32, 32},   // anar l r zl
    {7, 9, 32, 32}, {6, 7, 35, 36}, {5, 8, 35, 35}, {5, 8, 36, 35},   // zr crsu crsd crsl
    {5, 8, 36, 35}, {6, 7, 34, 36}, {5, 7, 37, 36}, {8, 10, 30, 31},   // crsr crsud crslr cap
    }, {
    {5, 7, 36, 36}, {8, 8, 31, 34}, {7, 9, 32, 32}, {7, 9, 32, 32},   // crs pow hom mns
    {7, 9, 32, 32}, {7, 8, 33, 33}, {7, 8, 33, 33}, {7, 8, 33, 33},   // pls x y a
    {7, 7, 33, 33}, {5, 0, 35, 47}, {5, 0, 35, 47}, {4, 6, 37, 38},   // b analp anarp anal
    {4, 6, 37, 38}, {6, 10, 35, 31}, {6, 10, 35, 31}, {5, 7, 36, 36},   // anar l r zl
    {5, 6, 36, 36}, {5, 4, 37, 38}, {5, 5, 37, 37}, {4, 5, 38, 37},   // zr crsu crsd crsl
    {5, 5, 38, 37}, {5, 4, 37, 39}, {3, 5, 40, 38}, {7, 7, 34, 34},   // crsr crsud crslr cap
    }
};
// Cells of the 512x192 icon_button sheet, symbol = cell index.
static const InlineBox inline_sheet_boxes[24] = {
    {3, 4, 58, 56}, {4, 2, 57, 60}, {4, 4, 56, 56}, {4, 4, 56, 56},   // crs pow hom mns
    {4, 4, 56, 56}, {4, 4, 56, 56}, {4, 4, 56, 56}, {4, 4, 56, 56},   // pls x y a
    {4, 4, 56, 56}, {4, 1, 55, 59}, {4, 1, 55, 59}, {4, 4, 55, 56},   // b analp anarp anal
    {4, 4, 55, 56}, {3, 6, 57, 52}, {3, 6, 57, 52}, {4, 4, 56, 56},   // anar l r zl
    {4, 3, 56, 56}, {5, 2, 55, 57}, {4, 4, 56, 57}, {3, 4, 57, 56},   // zr crsu crsd crsl
    {4, 4, 57, 56}, {4, 3, 56, 58}, {3, 4, 58, 56}, {4, 4, 56, 56},   // crsr crsud crslr cap
};
// icon_button02: four 88x88 cells in a 352x88 image: left stick vertical, left stick horizontal, right stick vertical, right stick
// horizontal (the stick with arrows on both sides). Its 4-record table is a closed form too:
//   {1, 1, 87.75/352, 87/88, (col*88+0.5)/352, 0.5/88, u16 88, u16 88}.
#define STICKS_RECORDS_SHA256 {0x19,0x36,0xef,0x6c,0x9d,0x8c,0x6c,0x19,0xb4,0xf8,0x7f,0x87,0x8c,0x5b,0x23,0xf9, \
                               0xb8,0xdd,0xdd,0x91,0x78,0xe5,0x51,0x0b,0xb1,0x26,0x42,0x45,0x46,0x91,0xf8,0x69}
// SHA-256 of the first 128 bytes of the image's resource object. For the icon_button sheet the same relation (file prefix equals the
// live object) is what the HUD route's guard has verified on real runs.
#define STICKS_GUARD_SHA256 {0xbb,0xe2,0xf0,0xce,0x2c,0x5a,0xda,0x43,0x5b,0x8b,0xc8,0x4c,0x1e,0xbe,0xdb,0xeb, \
                             0xa1,0x3d,0x54,0xc0,0x5f,0xbb,0xa5,0x39,0xff,0xf7,0x2c,0x12,0xe4,0xe8,0x7d,0x38}
static const InlineBox inline_stick_boxes[4] = {{16, 4, 55, 81}, {4, 16, 80, 56}, {16, 4, 55, 81}, {4, 16, 79, 56}};   // L-V, L-H, R-V, R-H

// Power, home and capture have no host input: their cells always stay native.
static inline int inline_symbol_replaceable(unsigned symbol) { return symbol != 1 && symbol != 2 && symbol != 23; }
