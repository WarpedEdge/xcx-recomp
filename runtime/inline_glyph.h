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
// Only the sixteen button records below are ever replaced; every other cell (d-pad, sticks, power, home, plus, minus,
// capture and the compound symbols) keeps its native pixels.
#pragma once
#include <stdint.h>

enum { INLINE_ATLAS_W = 368, INLINE_ATLAS_H = 300, INLINE_NVN_FORMAT = 77, INLINE_CELL_W = 46, INLINE_CELL_H = 50,
       INLINE_COLUMNS = 8, INLINE_RECORDS = 48, INLINE_RECORD_BYTES = 28, INLINE_ACTIONS = 8, INLINE_CELLS = 16, INLINE_SIGNATURES = 24 };
#define INLINE_ENTRY_Q0 0x012c01700000004dull
#define INLINE_SOURCE_SCALE_A 50.0f   // source+0x4c
#define INLINE_SOURCE_SCALE_B 46.0f   // source+0x50
#define INLINE_RECORDS_SHA256 {0xe5,0x53,0x74,0x5f,0x74,0x9a,0x12,0xf1,0xed,0x76,0x12,0x70,0x2c,0xb6,0x1f,0x0c, \
                               0x24,0x44,0x27,0xae,0x9d,0x62,0x6d,0x32,0xce,0x2c,0xe5,0xde,0x05,0x41,0xba,0xc0}

// Virtual button of each replaceable cell, in the order of input_glyphs' inline set: A, B, X, Y, L, R, ZL, ZR.
typedef struct { uint8_t record, action, x, y, w, h; } InlineCell;   // x, y, w, h: native glyph box inside the cell
static const InlineCell inline_cells_geom[INLINE_CELLS] = {
    {5, 2, 8, 10, 30, 30}, {6, 3, 8, 10, 30, 30}, {7, 0, 8, 10, 30, 30}, {8, 1, 7, 10, 30, 30},
    {13, 4, 7, 11, 32, 28}, {14, 5, 7, 11, 32, 28}, {15, 6, 7, 9, 32, 32}, {16, 7, 7, 9, 32, 32},
    {29, 2, 7, 8, 33, 33}, {30, 3, 7, 8, 33, 33}, {31, 0, 7, 8, 33, 33}, {32, 1, 7, 7, 33, 33},
    {37, 4, 6, 10, 35, 31}, {38, 5, 6, 10, 35, 31}, {39, 6, 5, 7, 36, 36}, {40, 7, 5, 6, 36, 36},
};
