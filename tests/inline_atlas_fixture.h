// Synthetic inline-icon source used by the unit and GPU tests: the 48-record table is the closed form recorded in
// runtime/inline_glyph.h, so no game data is involved. The Python test checks its SHA-256 against INLINE_RECORDS_SHA256.
#pragma once
#include <stdint.h>
#include <string.h>
#include "../runtime/inline_glyph.h"

static void inline_atlas_records(uint8_t *out) {   // INLINE_RECORDS * INLINE_RECORD_BYTES
    for (int i = 0; i < INLINE_RECORDS; i++) {
        int col = i % INLINE_COLUMNS, row = i / INLINE_COLUMNS;
        float w[6] = {1.0f, 1.0f, (float)(45.0 / 368.0), (float)(149.0 / 900.0),
                      (float)((col * 46 + 0.5) / 368.0), (float)((row * 50 + 0.5) / 300.0)};
        uint16_t sz[2] = {46, 50};
        memcpy(out + i * INLINE_RECORD_BYTES, w, sizeof w);
        memcpy(out + i * INLINE_RECORD_BYTES + sizeof w, sz, sizeof sz);
    }
}
