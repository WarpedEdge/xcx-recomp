// Display policy for the verified Appendage guide. Never changes input mapping or emitted inputs.
#pragma once
#include <stdint.h>
#include "input_map.h"
enum { GLYPH_ORIGINAL, GLYPH_AUTO, GLYPH_PLAYSTATION, GLYPH_XBOX, GLYPH_STEAM, GLYPH_KEYBOARD, GLYPH_STYLE_COUNT };
enum { GLYPH_SOURCE_NONE, GLYPH_SOURCE_KEYBOARD, GLYPH_SOURCE_PAD };
enum { GLYPH_PAD_UNKNOWN, GLYPH_PAD_SWITCH, GLYPH_PAD_PS, GLYPH_PAD_XBOX, GLYPH_PAD_STEAM };
enum { GLYPH_ART_MAX = 64 };
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
const char *glyph_status(void); // Thread-local copied status for the experimental Controls row.
int glyph_art_find(int style, int physical);
const char *glyph_art_name(unsigned art);
unsigned glyph_art_count(void);
int glyph_art_pixels(unsigned art, uint32_t *pixels); // 512x192 owned destination, all three verified UV tiles.
