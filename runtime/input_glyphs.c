#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_glyphs.h"
#include "cfg.h"
#include "host.h"
#include "appendage_glyph.h"

static int magnitude(int32_t value) { return value == INT32_MIN ? INT32_MAX : value < 0 ? -value : value; }
void glyph_intent_sample(GlyphIntent *s, const GlyphIntentSample *p) {
    int changed = p->device != s->device;
    if (changed && s->source == GLYPH_SOURCE_PAD) s->source = GLYPH_SOURCE_NONE;
    int baseline = !p->active || !s->active;
    if (!baseline) {
        int keyboard = 0, controller = 0;
        for (int i = 1; i < KEY_COUNT; i++) keyboard |= p->keys[i] && !s->keys[i];
        if (!changed && p->device) controller = (p->buttons & ~s->buttons) != 0;
        for (int i = 0; i < 6; i++) {
            int value = i < 4 ? magnitude(p->axes[i]) : p->axes[i];
            unsigned bit = 1u << i;
            if (i < 4 && ((p->axes[i] > 20000 && s->previous_axes[i] < -20000) ||
                          (p->axes[i] < -20000 && s->previous_axes[i] > 20000))) {
                s->axes &= ~bit; s->axis_since[i] = p->now;
            }
            if (value < 10000) { s->axes &= ~bit; s->axis_since[i] = 0; }
            else if (value > 20000 && !(s->axes & bit) && !changed && p->device) {
                if (!s->axis_since[i]) s->axis_since[i] = p->now;
                if (p->now - s->axis_since[i] >= 40000000) { s->axes |= bit; controller = 1; }
            } else if (value <= 20000) s->axis_since[i] = 0;
        }
        if (p->captured && s->captured && p->now >= s->pointer_ready) {
            uint64_t distance = (uint64_t)magnitude(p->mouse_x) + (uint64_t)magnitude(p->mouse_y);
            if (p->now - s->pointer_since > 150000000) {
                s->pointer_since = p->now; s->pointer_distance = s->pointer_samples = 0;
            }
            // A single warp/spike or incidental motion cannot change the source.
            if (distance && distance < 512) { s->pointer_distance += (unsigned)distance; s->pointer_samples++; }
            if (s->pointer_distance >= 32 && s->pointer_samples >= 2 && p->now - s->pointer_since >= 35000000) {
                keyboard = 1; s->pointer_distance = s->pointer_samples = 0; s->pointer_since = p->now;
            }
        }
        if (controller) s->source = GLYPH_SOURCE_PAD;
        if (keyboard) s->source = GLYPH_SOURCE_KEYBOARD; // A fresh key/click wins simultaneous sampling ties.
    }
    if (baseline || changed) {
        s->axes = 0;
        memset(s->axis_since, 0, sizeof s->axis_since);
        for (int i = 0; i < 6; i++) if ((i < 4 ? magnitude(p->axes[i]) : p->axes[i]) >= 10000) s->axes |= 1u << i;
    }
    if (baseline || p->captured != s->captured) {
        s->pointer_ready = p->now + 250000000;
        s->pointer_since = p->now; s->pointer_distance = s->pointer_samples = 0;
    }
    memcpy(s->previous_axes, p->axes, sizeof s->previous_axes);
    memcpy(s->keys, p->keys, sizeof s->keys);
    s->buttons = p->buttons; s->device = p->device; s->active = p->active; s->captured = p->captured;
}
static const char *const names[] = {"Original", "Automatic", "PlayStation", "Xbox", "Steam Controller", "Keyboard / Mouse"};
static const char *const values[] = {"original", "auto", "playstation", "xbox", "steam", "keyboard"};
int glyph_style_parse(const char *value) {
    for (int i = 0; value && i < GLYPH_STYLE_COUNT; i++) if (!strcmp(value, values[i])) return i;
    return GLYPH_ORIGINAL;
}
const char *glyph_style_name(int style) { return style >= 0 && style < GLYPH_STYLE_COUNT ? names[style] : names[0]; }
static GlyphDecision decide(int preference, int source, int family, int layout, const InputMap *map, const int *actions, int count) {
    GlyphDecision d = {.style = preference, .art = {-1,-1,-1}, .reason = NULL};
    if (preference < 0 || preference >= GLYPH_STYLE_COUNT) d.style = GLYPH_ORIGINAL;
    if (d.style == GLYPH_AUTO) {
        if (source == GLYPH_SOURCE_KEYBOARD) d.style = GLYPH_KEYBOARD;
        else if (source == GLYPH_SOURCE_PAD) {
            const int styles[] = {GLYPH_ORIGINAL,GLYPH_ORIGINAL,GLYPH_PLAYSTATION,GLYPH_XBOX,GLYPH_STEAM};
            d.style = family >= 0 && family < 5 ? styles[family] : GLYPH_ORIGINAL;
            if (family == GLYPH_PAD_UNKNOWN) d.reason = "Original virtual labels: controller family is unknown";
        } else { d.style = GLYPH_ORIGINAL; d.reason = "Original virtual labels: waiting for intentional input"; }
    }
    // Original, including Automatic on Nintendo controllers, retains native virtual artwork.
    if (d.style == GLYPH_ORIGINAL) return d;
    if (d.style != GLYPH_KEYBOARD && (layout < 0 || layout >= PAD_LAYOUT_COUNT)) {
        d.reason = "Original virtual labels: no exposed controller bindings"; goto fallback;
    }
    for (int n = 0; n < count; n++) {
        uint8_t inputs[INPUT_BINDING_MAX];
        int count = d.style == GLYPH_KEYBOARD ? input_map_keys_of(map, actions[n], inputs, sizeof inputs) :
                    input_map_pads_of(map, actions[n], layout, inputs, sizeof inputs);
        if (count != 1) { d.reason = "Original virtual labels: a guide action is unbound or has multiple bindings"; goto fallback; }
        if (d.style == GLYPH_KEYBOARD && inputs[0] == KEY_NONE) { d.reason = "Original virtual labels: unsupported keyboard label"; goto fallback; }
        if (d.style == GLYPH_KEYBOARD) {
            for (int i = 0; i < map->nkeys; i++) if (map->keys[i].key == inputs[0] && map->keys[i].action != actions[n]) {
                d.reason = "Original virtual labels: a key is shared by different actions"; goto fallback;
            }
        } else {
            for (int i = 0; i < map->npads[layout]; i++) if (map->pads[layout][i].pad == inputs[0] && map->pads[layout][i].action != actions[n]) {
                d.reason = "Original virtual labels: a controller input is shared by different actions"; goto fallback;
            }
        }
        d.art[n] = glyph_art_find(d.style, inputs[0]);
        if (d.art[n] < 0) { d.reason = "Original virtual labels: this binding has no verified glyph"; goto fallback; }
    }
    return d;
fallback:
    d.style = GLYPH_ORIGINAL;
    for (int i = 0; i < 3; i++) d.art[i] = -1;
    return d;
}
GlyphDecision glyph_decide(int preference, int source, int family, int layout, const InputMap *map) {
    static const int actions[3] = {B_R,B_Y,B_A};
    return decide(preference,source,family,layout,map,actions,3);
}
GlyphDecision glyph_decide_button(int preference, int source, int family, int layout, const InputMap *map, int action) {
    // Only the verified shared-widget actions are supported by this adapter.
    if (action != B_X && action != B_A && action != B_R && action != B_ZL) return (GlyphDecision){.style=GLYPH_ORIGINAL,.art={-1,-1,-1},.reason="Original virtual label: unsupported guide action"};
    return decide(preference,source,family,layout,map,&action,1);
}
static const int inline_actions[GLYPH_INLINE_SET] = {B_A, B_B, B_X, B_Y, B_L, B_R, B_ZL, B_ZR};
void glyph_decide_inline_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int8_t art[GLYPH_INLINE_SET]) {
    for (int i = 0; i < GLYPH_INLINE_SET; i++) {
        GlyphDecision d = decide(preference, source, family, layout, map, &inline_actions[i], 1);
        art[i] = d.style != GLYPH_ORIGINAL && d.art[0] >= 0 ? (int8_t)glyph_art_variant(d.art[0], appearance) : -1;
    }
}
void glyph_current_inline_set(int8_t art[GLYPH_INLINE_SET]) {
    int source, family, layout; InputMap map;
    input_glyph_snapshot(&source, &family, &layout, &map);
    glyph_decide_inline_set(glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")), source, family, layout, &map,
                            glyph_appearance_parse(cfg_value("XCX_GLYPH_APPEARANCE")), art);
}
int glyph_build_supported(void) {
    for (int i = 0; i < rt_exefs_sha256_count; i++)
        if (!strcmp(rt_exefs_sha256[i].file,"main"))
            return !strcmp(rt_exefs_sha256[i].sha256,"0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026");
    return 0;
}
int glyph_appearance_parse(const char *value) { return value && !strcmp(value,"colored") ? GLYPH_COLORED : GLYPH_STANDARD; }
static GlyphDecision appearance(GlyphDecision d) {
    int variant = glyph_appearance_parse(cfg_value("XCX_GLYPH_APPEARANCE"));
    for (int i=0;i<3;i++) if (d.art[i]>=0) d.art[i]=glyph_art_variant(d.art[i],variant);
    return d;
}
GlyphDecision glyph_current_decision(void) {
    int source, family, layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    return appearance(glyph_decide(glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")),source,family,layout,&map));
}
GlyphDecision glyph_current_button(int action) {
    int source,family,layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    return appearance(glyph_decide_button(glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")),source,family,layout,&map,action));
}
const char *glyph_status(void) {
    static _Thread_local char status[1280];
    int source,family,layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    int preference = glyph_style_parse(cfg_value("XCX_GLYPH_STYLE"));
    GlyphDecision d = glyph_decide(preference,source,family,layout,&map);
    GlyphDecision weapon = glyph_decide_button(preference,source,family,layout,&map,B_X);
    GlyphDecision target = glyph_decide_button(preference,source,family,layout,&map,B_R);
    GlyphDecision board = glyph_decide_button(preference,source,family,layout,&map,B_ZL);
    GlyphDecision field = glyph_decide_button(preference,source,family,layout,&map,B_A);
    const char *note = preference != GLYPH_ORIGINAL ? appendage_glyph_runtime_note() : NULL;
    if (note) snprintf(status,sizeof status,"Verified HUD guides — %s",note);
    else if (d.reason || weapon.reason || field.reason || target.reason || board.reason)
        snprintf(status,sizeof status,"Appendage: %s; weapon: %s; field A: %s; Target: %s; Board: %s",
            d.reason ? d.reason : glyph_style_name(d.style),
            weapon.reason ? weapon.reason : glyph_style_name(weapon.style),
            field.reason ? field.reason : glyph_style_name(field.style),
            target.reason ? target.reason : glyph_style_name(target.style),
            board.reason ? board.reason : glyph_style_name(board.style));
    else snprintf(status,sizeof status,"Verified HUD guides — %s",glyph_style_name(d.style));
    return status;
}
