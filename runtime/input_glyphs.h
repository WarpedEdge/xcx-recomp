// Display policy for the verified HUD guides. Never changes input mapping or emitted inputs.
#pragma once
#include <stdint.h>
#include "input_map.h"
enum { GLYPH_ORIGINAL, GLYPH_AUTO, GLYPH_PLAYSTATION, GLYPH_XBOX, GLYPH_STEAM, GLYPH_KEYBOARD, GLYPH_STYLE_COUNT };
enum { GLYPH_STANDARD, GLYPH_COLORED };
enum { GLYPH_SOURCE_NONE, GLYPH_SOURCE_KEYBOARD, GLYPH_SOURCE_PAD };
enum { GLYPH_PAD_UNKNOWN, GLYPH_PAD_SWITCH, GLYPH_PAD_PS, GLYPH_PAD_XBOX, GLYPH_PAD_STEAM };
enum { GLYPH_ART_MAX = 128 };
typedef struct {
    uint8_t keys[KEY_COUNT];
    uint32_t buttons;
    int32_t axes[6];
    uint64_t device, now;
    int active, captured;
    int32_t mouse_x, mouse_y;
} GlyphIntentSample;
typedef struct {
    uint8_t keys[KEY_COUNT];
    uint32_t buttons;
    uint64_t device, axis_since[6], pointer_since, pointer_ready;
    int32_t previous_axes[6];
    unsigned axes, pointer_distance, pointer_samples;
    int active, captured, source;
} GlyphIntent;
void glyph_intent_sample(GlyphIntent *state, const GlyphIntentSample *sample);
int glyph_style_parse(const char *value);
const char *glyph_style_name(int style);
typedef struct { int style, art[3]; const char *reason; } GlyphDecision;
GlyphDecision glyph_decide(int preference, int source, int family, int layout, const InputMap *map);
// Atomically copies the intent, exposed controller family, layout and current bindings under input.c's lock.
void input_glyph_snapshot(int *source, int *family, int *layout, InputMap *map);
int glyph_build_supported(void);
GlyphDecision glyph_current_decision(void);
GlyphDecision glyph_current_button(int action);
GlyphDecision glyph_decide_button(int preference, int source, int family, int layout, const InputMap *map, int action);
const char *glyph_status(void); // Thread-local copied status for the experimental Controls row.
int glyph_art_find(int style, int physical);
int glyph_art_variant(int art, int appearance);
int glyph_appearance_parse(const char *value);
const char *glyph_art_name(unsigned art);
unsigned glyph_art_count(void);
int glyph_art_pixels(unsigned art, uint32_t *pixels); // 512x192 owned destination, all five verified UV tiles.
// Inline text icons: the virtual buttons A, B, X, Y, L, R, ZL, ZR in that order. Each decision is made on its own (so one
// ambiguous or unbound action keeps only itself native) from the same snapshot; art[i] is an art id or -1 for Original.
// Unlike the guide actions above, these use every verified physical tile (face buttons, L/R, ZL/ZR) for the current layout.
enum { GLYPH_INLINE_SET = 8 };
void glyph_decide_inline_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int8_t art[GLYPH_INLINE_SET]);
void glyph_current_inline_set(int8_t art[GLYPH_INLINE_SET]);
// Fits a tile into the native glyph box of a 46x50 inline cell; see glyph_art.c. cell is 46*50 RGBA8.
int glyph_art_inline_cell(unsigned art, int x, int y, int w, int h, uint32_t *cell);
