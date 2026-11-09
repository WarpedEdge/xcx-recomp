// Experimental, build-specific HUD glyph styles. No input or guest-state writes.
#pragma once
#include "cpu.h"

// Bits 32..38 select immutable artwork; bits 39..43 remain reserved.
#define APPENDAGE_GLYPH_TAG 0xa700000000000000ull
#define APPENDAGE_GLYPH_TAG_MASK 0xff000f8000000000ull
static inline int appendage_glyph_tagged(uint64_t h) {
    return (h & APPENDAGE_GLYPH_TAG_MASK) == APPENDAGE_GLYPH_TAG;
}
static inline unsigned appendage_glyph_art(uint64_t h) { return (unsigned)(h >> 32 & 127); }
void appendage_glyph_attach(const Cpu *c);
// Exact saved callers in the verified shared-selector frame identify supported guides.
void appendage_glyph_lookup_attach(const Cpu *c, uint64_t frame);
void appendage_glyph_retire(uint64_t node);
void appendage_glyph_bind(Cpu *c, uint64_t node, GuestFn original);
uint64_t appendage_glyph_handle(const Cpu *c, uint64_t original);
// Called with the texture lock held; copies serial/storage identity, never a guest pointer.
int appendage_glyph_source(uint64_t h, uint64_t serial, uint64_t storage);
int vk_appendage_capture_source(uint64_t handle);
// Read-only, opt-in diagnostic at the verified original BindTexture import.
void vk_glyph_resource_observe(uint64_t handle, unsigned event);
int appendage_glyph_failure(const char *stage);

void appendage_glyph_note_fallback(void);
const char *appendage_glyph_runtime_note(void);
