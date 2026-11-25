// Extra hairstyles in the character creator's list (docs/character-creator.md; the game side is recomp/avatar_hair.py).
//
// XCX_HAIR_EXTRA lists extras as "face:row" pairs separated by commas: row is a row of ITM_AvatarHairList (1 to 141),
// face the base face (0 to 13) whose list gets it. The game builds the list in row order, so an extra sits among the
// face's own rows by its row number; the story characters' rows are then moved to the end (sort_list).
#include "avatar_hair.h"
#include "romfs_mods.h"
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "input.h"
#include "input_glyphs.h"
#include <math.h>

#define MAX_EXTRA 256
#define PLACEHOLDER_ICON 1   // the first hair icon, for a story character's row that has no colour

static struct { uint32_t faces, row; int logged; } extra[MAX_EXTRA];   // faces: bit mask, bit n = base face n
static int n_extra, parsed;
static int page, custom;
// Size and place of a hair of the other gender (from the part's and the wearer's head joint heights), about the wearer's head joint: the
// other gender's hair caps are smaller than this skull. Tuned with XCX_HAIR_TUNE in the review run; XCX_HAIR_SCALE/_LIFT/_FORWARD set them.
static double tune_s = 1.0, tune_dy = 0.0, tune_dz = 0.0;   // set by XCX_HAIR_SCALE/_LIFT/_FORWARD or the review's tune sets
static int tune_ovr;   // 1: tune_* replace the defaults below
static double tune_c[3];   // scale, lift, forward of the head joint's own bone (the scalp cap of a hair) when tune_cset: it may need more than the strands
static int tune_cset;
static double tune_sd[3];   // scale, lift, forward of the side locks' bones (DJ_hairS...: the part of a hair that frames the ears) when tune_sdset
static int tune_sdset;
static const double tune_sm[3] = {1.0, 0.005, -0.043};
static const double tune_cm[3] = {1.06, 0.0, -0.028};   // scale, lift, forward of the scalp cap's bone of a female hair on a male head   // scale, lift, forward of the side locks' bones of a female hair on a male head
static int force_cross;   // XCX_HAIR_FORCE=1: treat every hair as cut for the other gender (tuning tests)
static double tune_m[3] = {1.0, 0.0, 0.0}, tune_f[3] = {1.0, 0.0, 0.0}, tune_x[3] = {1.1, 0.01, 0.0};   // scale, lift, forward of a female hair on a male head and of a male hair on a female head
static volatile uint32_t sw_gen = 1;   // another hairstyle came under the cursor: the binds decided for that hairstyle's key are decided again (the ones of the hairstyle that was left keep what they had)
static volatile uint32_t ib_gen = 1;   // page: 0 the face's gender, 1 the other gender, 2 the story characters; custom: XCX_HAIR_EXTRA is set
static uint32_t cur_face = 0xffffffffu;

// ---- Modify: the player's live adjustment of a hair worn on the other gender's head (the row under the creator's category list, opened with
// Minus). Every part of the hair (the whole hair, the scalp cap, the front, the sides, the tails, the other strands) has a scale, a shift
// left/right, up/down and back/forward (metres, in the model's frame about the wearer's head joint) and a tilt (degrees about the left/right axis);
// the values are changes on top of what the fit already does. Each change is logged with the absolute values, to be taken over as the fit of that hair.
enum { MG_ALL, MG_CAP, MG_FRONT, MG_SIDE, MG_TAIL, MG_REST, MG_N };
enum { MP_SCALE, MP_X, MP_Y, MP_Z, MP_TILT, MP_N };
static double mod_v[MG_N][MP_N];
// The player's changes belong to the hairstyle they were made on: each hairstyle (its row, and the wearer's gender) keeps its own set; mod_v is the set of the hairstyle under the cursor.
#define MOD_STORE 192
static struct { uint32_t key; double v[MG_N][MP_N]; } mod_store[MOD_STORE];
static int mod_nstore;
static double mod_b[MG_N][MP_N];   // what the fit of the hairstyle under the cursor starts from (avatar_hair_fits.inc); the player's changes in mod_v come on top
#include "avatar_hair_fits.inc"
static uint32_t mod_cur_key = 0xffffffffu, mod_last_key = 0xffffffffu;
static volatile uint32_t seen_acc;   // the parts the bones decided since the last change belong to (published to mod_seen a few frames later)
static volatile unsigned seen_cnt[6], seen_root;
static volatile int mod_open, mod_grp, mod_row, mod_any, mod_force;
static volatile int mod_cross = 1;   // the hairstyle under the cursor was decided to be cut for the other gender: the fit's own tuning applies to it (otherwise only the player's changes do)
static volatile uint32_t mod_seen;   // the parts (bit per MG_) the bones of the hairstyle under the cursor belong to; 0 when not known yet   // mod_force: XCX_HAIR_MODOPEN, the panel drawn open for a test shot
static const char *const mg_name[MG_N] = {"All", "Main", "Front", "Sides", "Tails", "Other strands"};
static const char *const mp_name[MP_N] = {"Scale", "Left / right", "Up / down", "Back / forward", "Tilt"};
static int cap_fix_wanted(void);
static int modify_on(void);
// The category list shows at most 7 rows (its widget has 7 row slots). On a face that already has 7 rows the Modify row is the 8th: the game's item
// vector then holds a window of 7 of the 8 rows and the window is moved by one row when the cursor wraps (avatar_hair_tick, avatar_hair_cats).
static volatile int virt_active;       // the list is windowed (8 rows, 7 shown)
static volatile int virt_off;          // the first row of the window: 0 (Style to Tips Color) or 1 (Special to Modify)
static volatile int virt_rebuild;      // the next build of the list is a scroll: use virt_off and virt_cursor
static volatile uint32_t virt_cursor;  // the window row the cursor takes after a scroll
static volatile int dialog_hide;       // the game's "Discard current edits" question is probably up (B on the category list): the overlay stays away until A or B answers it
static volatile int snd_req;           // a creator sound the UI update plays: 1 the confirm sound, 2 the back sound (ui + 0x610 / + 0x628, played by main+0x7788dc)
static volatile unsigned snd_check;    // tick_count of a check that the game itself took the A press (then it has played the confirm sound already)
static volatile unsigned tick_count;   // UI updates seen by avatar_hair_tick
static volatile unsigned dn_edge, up_edge;   // tick_count + 1 at the last Down / Up press the pad gave (0: none); read by avatar_hair_tick
static void modify_any_update(void);
static int mod_dirty;   // the values of the hairstyle under the cursor changed since they were last written to the record
// The total of the start values and the player's changes: scale as a factor of factors, the rest summed.
static double mvc(int g, int k) { return k == MP_SCALE ? (1.0 + mod_b[g][k]) * (1.0 + mod_v[g][k]) - 1.0 : mod_b[g][k] + mod_v[g][k]; }
// The record of the hand-made fits: one line per hairstyle each time its window closes or the cursor leaves it, appended to <profile>/hair-modify-record.txt.
// A later line of a hairstyle replaces an earlier one. The values are totals (start values and changes), scale as a factor, shifts in cm, tilt in degrees.
static uint32_t grid_order[192], grid_n;   // (defined again below) the rows of the grid in the order it shows them
static void mod_flush(void) {
    if (!mod_dirty || mod_cur_key == 0xffffffffu) { mod_dirty = 0; return; }
    mod_dirty = 0;
    const char *dir = rt_config()->profile;
    if (!dir) return;
    char path[1100];
    if (snprintf(path, sizeof path, "%s/hair-modify-record.txt", dir) >= (int)sizeof path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    // where the hairstyle sits in the grid of the page (4 icons per line), for the notes: "grid row 3 item 2" is the second icon of the third line
    char at[40] = "";
    for (uint32_t i = 0; i < grid_n; i++) if (grid_order[i] == mod_cur_key / 2) snprintf(at, sizeof at, " (grid row %u item %u)", i / 4 + 1, i % 4 + 1);
    fprintf(f, "row %u%s wearer %s face %u page %d:", mod_cur_key / 2, at, (mod_cur_key & 1) ? "female" : "male", (unsigned)cur_face, page + 1);
    int any = 0;
    for (int g = 0; g < MG_N; g++) {
        int nz = 0;
        for (int k = 0; k < MP_N; k++) nz |= mvc(g, k) != 0.0;
        if (!nz) continue;
        any = 1;
        fprintf(f, " | %s scale %.4f x %.2f y %.2f z %.2f cm tilt %.1f deg", mg_name[g], 1.0 + mvc(g, MP_SCALE), mvc(g, MP_X) * 100, mvc(g, MP_Y) * 100, mvc(g, MP_Z) * 100, mvc(g, MP_TILT));
    }
    fprintf(f, "%s\n", any ? "" : " default");
    fclose(f);
    rt_log("avatar hair modify: recorded row %u (%s wearer) in %s", mod_cur_key / 2, (mod_cur_key & 1) ? "female" : "male", path);
}
static int own_hair_row(uint32_t face, uint32_t row);   // one of the eight hairstyles made for this face (defined with the face table)
static void mod_load_baked(uint32_t key) {   // the start values of a hairstyle (avatar_hair_fits.inc); zero for one that has none
    memset(mod_b, 0, sizeof mod_b);
    // fitted by hand on the first male face and taken over for the faces 0 to 2 (presets 1 to 6), except for the hairstyles the face was made with: they sit right on it
    uint32_t face = cur_face;
    if (face > 13 || own_hair_row(face, key / 2)) return;
    for (int i = 0; mod_baked[i].key != 0xffffffffu; i++)
        if (mod_baked[i].key == key && (mod_baked[i].faces >> face & 1u)) {   // the first entry of the hairstyle for this face
            memcpy(mod_b, mod_baked[i].v, sizeof mod_b);
            static int logged;
            if (logged++ < 12) rt_log("avatar hair: start values of hairstyle %u loaded (face %u)", key / 2, face);
            break;
        }
}
static void mod_switch(uint32_t key) {   // another hairstyle under the cursor: keep the changes of the old one, take the new one's
    if (key == mod_cur_key) return;
    mod_flush();
    if (mod_cur_key != 0xffffffffu) {
        int at = -1;
        for (int i = 0; i < mod_nstore; i++) if (mod_store[i].key == mod_cur_key) at = i;
        if (at < 0 && mod_nstore < MOD_STORE) { at = mod_nstore++; mod_store[at].key = mod_cur_key; }
        if (at >= 0) memcpy(mod_store[at].v, mod_v, sizeof mod_v);
    }
    memset(mod_v, 0, sizeof mod_v);
    for (int i = 0; i < mod_nstore; i++) if (mod_store[i].key == key) memcpy(mod_v, mod_store[i].v, sizeof mod_v);
    mod_load_baked(key);
    mod_cur_key = key;
    modify_any_update();
}

// What part g gets in total now: the fit's own value plus the player's changes (scale as a factor, shifts in metres, tilt in degrees).
static void mod_eff(int g, double o[5]) {
    int capfix = cap_fix_wanted();
    const double *d = capfix ? tune_x : cur_face >= 7 ? tune_f : tune_m;
    double s = d[0], dy = d[1], dz = d[2];
    if (!capfix && cur_face < 7) {
        if (g == MG_CAP) { s = tune_cm[0]; dy = tune_cm[1]; dz = tune_cm[2]; }
        else if (g == MG_SIDE) { s = tune_sm[0]; dy = tune_sm[1]; dz = tune_sm[2]; }
    }
    if (!mod_cross) { s = 1.0; dy = dz = 0.0; }   // a hair of the wearer's own gender gets no tuning of the fit, only the player's
    o[MP_SCALE] = s * (1.0 + mod_v[MG_ALL][MP_SCALE]) * (g == MG_ALL ? 1.0 : 1.0 + mod_v[g][MP_SCALE]);
    for (int k = MP_X; k <= MP_TILT; k++) o[k] = (g == MG_ALL ? 0.0 : mod_v[g][k]) + mod_v[MG_ALL][k];
    o[MP_Y] += dy; o[MP_Z] += dz;
}

static void mod_log(const char *what, uint32_t row) {
    char line[700]; int n = snprintf(line, sizeof line, "avatar hair modify: %s: page %d row %u, %s wearer face %u:", what, page, row, cur_face < 7 ? "male" : "female", cur_face);
    for (int g = 0; g < MG_N && n < (int)sizeof line - 90; g++) {
        double e[5]; mod_eff(g, e);
        n += snprintf(line + n, sizeof line - (size_t)n, " | %s scale %.3f x %.1f y %.1f z %.1f cm tilt %.1f deg", mg_name[g], e[0], e[1] * 100, e[2] * 100, e[3] * 100, e[4]);
    }
    rt_log("%s", line);
}
static void set_overlay(uint32_t face);

#define MALE_FACES 0x007fu
#define FEMALE_FACES 0x3f80u
#define NPC_FIRST 114   // the story characters' rows, 114 to 132 (no icon, no creator face)
#define NPC_LAST 132

// The built-in extras (in use when XCX_HAIR_EXTRA is not set), part 1: hairstyles the creator lists on some faces only, shown
// on every face of the same gender. The story characters' hairstyles (rows 114 to 132) are not in the built-in list: their
// icons are solid colours painted on icon cells, and every cell is a creator hairstyle that now shows on every face (see
// XCX_HAIR_EXTRA to add them anyway, with the placeholder icon).
static const struct { uint32_t faces, row; } builtin[] = {
    {MALE_FACES, 134}, {MALE_FACES, 135}, {MALE_FACES, 136},
    {FEMALE_FACES, 133}, {FEMALE_FACES, 137}, {FEMALE_FACES, 138},
    {FEMALE_FACES, 139}, {FEMALE_FACES, 140}, {FEMALE_FACES, 141},
};

// Part 2: the creator's own hairstyles. Every face has eight hairstyles of its own (rows r0 to r0 + n - 1 of
// ITM_AvatarHairList). The three faces of a head family have the same file names for them, but the designs are not the
// same on every face (the first male preset's list and the older presets' lists each have designs the other lacks), so
// every face lists the rows of every face of its gender. group and var: from the names of the faces' hairstyle models;
// icon0: the IconID of the first row.
typedef struct { uint8_t group, var, r0, n, icon0; } FaceInfo;
static const FaceInfo fi[14] = {
    {1, 2, 1, 8, 1},    {2, 1, 9, 8, 10},   {2, 2, 17, 8, 19},  {2, 3, 25, 8, 28},  {3, 1, 33, 8, 37},
    {3, 2, 41, 8, 46},  {3, 3, 49, 8, 55},  {21, 2, 57, 8, 64}, {21, 3, 65, 8, 74}, {21, 1, 73, 8, 83},
    {22, 2, 82, 7, 93}, {22, 1, 89, 8, 102}, {23, 1, 98, 8, 111}, {23, 2, 106, 8, 120},
};
static int own_hair_row(uint32_t face, uint32_t row) { return face < 14 && row >= fi[face].r0 && row < (uint32_t)fi[face].r0 + fi[face].n; }

// The creator's hair grid is shown in pages (XCX_HAIR_PAGE): 0 the hairstyles of the face's gender (the icons are the game's own),
// 1 the other gender's (their sheet's art, see avatar_icon_patch), 2 the story characters' (solid colours). A page's rows replace
// the face's own eight, which avatar_hair_avt hides on pages 1 and 2. Rows 115, 120 and 122 render bald or as a bare patch of
// scalp, so they stay out.
static void build_default(void) {
    n_extra = 0;
    for (uint32_t row = 1; row <= 141 && n_extra < MAX_EXTRA; row++) {
        if (row == 115 || row == 120 || row == 122) continue;
        int npc_row = row >= NPC_FIRST && row <= NPC_LAST, male_row = row <= 56 || (row >= 134 && row <= 136);
        uint32_t mask = 0;
        for (int f = 0; f < 14; f++) {
            int same = male_row == (f < 7);
            if (page == 2 ? npc_row : (!npc_row && same == (page == 0))) mask |= 1u << f;
        }
        if (!mask) continue;
        extra[n_extra].faces = mask;
        extra[n_extra].row = row;
        extra[n_extra].logged = 0;
        n_extra++;
    }
}

// The face part of a pair is a number (0 to 13), or a letter for several faces: m = the male faces 0 to 6,
// f = the female faces 7 to 13, a = all. XCX_HAIR_EXTRA=none turns the extras off.
static void parse(void) {
    if (parsed) return;
    parsed = 1;
    const char *s = cfg_get("XCX_HAIR_EXTRA");
    custom = s != NULL;
    if (cfg_get("XCX_HAIR_SCALE")) { tune_s = atof(cfg_get("XCX_HAIR_SCALE")); tune_ovr = 1; }
    if (cfg_get("XCX_HAIR_LIFT")) { tune_dy = atof(cfg_get("XCX_HAIR_LIFT")); tune_ovr = 1; }
    if (cfg_get("XCX_HAIR_FORWARD")) { tune_dz = atof(cfg_get("XCX_HAIR_FORWARD")); tune_ovr = 1; }
    if (cfg_get("XCX_HAIR_FORCE")) force_cross = 1;
    if (cfg_get("XCX_HAIR_MODTEST")) {   // XCX_HAIR_MODTEST="group,value,value,value,value,value;...": Modify values at the start (scale change, x, y, z in metres, tilt in degrees); XCX_HAIR_MODOPEN=1 opens the panel
        const char *q = cfg_get("XCX_HAIR_MODTEST");
        while (q && *q) {
            char *e; int g = (int)strtol(q, &e, 10);
            double v[5] = {0, 0, 0, 0, 0};
            for (int k = 0; k < 5 && *e == ','; k++) v[k] = strtod(e + 1, &e);
            if (g >= 0 && g < MG_N) for (int k = 0; k < MP_N; k++) mod_v[g][k] = v[k];
            q = *e == ';' ? e + 1 : NULL;
        }
        modify_any_update();
    }
    if (cfg_get("XCX_HAIR_MODOPEN")) mod_force = 1;
    if (cfg_get("XCX_HAIR_SIDE")) { if (sscanf(cfg_get("XCX_HAIR_SIDE"), "%lf,%lf,%lf", &tune_sd[0], &tune_sd[1], &tune_sd[2]) == 3) tune_sdset = 1, tune_ovr = 1; }
    if (cfg_get("XCX_HAIR_CAP")) { if (sscanf(cfg_get("XCX_HAIR_CAP"), "%lf,%lf,%lf", &tune_c[0], &tune_c[1], &tune_c[2]) == 3) tune_cset = 1, tune_ovr = 1; }
    const char *pg = cfg_get("XCX_HAIR_PAGE");
    page = pg && *pg >= '0' && *pg <= '2' ? *pg - '0' : 0;
    if (!s) {
        build_default();
        s = "";
    } else if (!strcmp(s, "none")) {
        s = "";
    }
    while (s && *s && n_extra < MAX_EXTRA) {
        char *end;
        uint32_t mask;
        if (*s == 'm') { mask = 0x007fu; end = (char *)s + 1; }
        else if (*s == 'f') { mask = 0x3f80u; end = (char *)s + 1; }
        else if (*s == 'a') { mask = 0x3fffu; end = (char *)s + 1; }
        else {
            unsigned long face = strtoul(s, &end, 10);
            if (end == s || face > 13) break;
            mask = 1u << face;
        }
        if (*end != ':') break;
        s = end + 1;
        unsigned long row = strtoul(s, &end, 10);
        if (end == s || row == 0 || row > 141) break;
        extra[n_extra].faces = mask;
        extra[n_extra].row = (uint32_t)row;
        n_extra++;
        s = end;
        if (*s == ',') s++;
    }
    if (n_extra) rt_log("avatar hair: %d extra hairstyle rows", n_extra);
}

// ---- the story characters' rows: no icon, so a solid colour on an icon cell that this face's list does not show

typedef struct { uint32_t row; uint8_t icon, cell, r, g, b; } Npc;
static Npc npc[16];
static int n_npc;

// IconNo (the cell of the icon sheet) of each IconID, rows of MNU_AvrIconHairList; 16 cells per sheet row
static const uint8_t icon_no[129] = {
    0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 0, 35, 36, 37, 38, 39, 40, 41, 42, 0, 51, 52, 53, 54, 55, 56, 57, 58, 0, 43, 44, 45, 46, 47, 48, 49,
    50, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 19, 20, 21, 22, 23, 24, 25, 26, 27, 10, 11, 12, 13, 14, 15, 16, 17, 18, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 0, 55, 56, 57,
    58, 59, 60, 61, 62, 0,
};
static const uint8_t palette[16][3] = {   // odd values only: a BC7 block's p-bit makes them odd (bc7_solid)
    {255, 1, 1}, {255, 127, 1}, {255, 255, 1}, {127, 255, 1}, {1, 191, 1}, {1, 191, 127}, {1, 255, 255}, {1, 127, 255},
    {1, 1, 255}, {127, 1, 255}, {255, 1, 191}, {255, 127, 191}, {255, 255, 255}, {127, 127, 127}, {127, 63, 1}, {1, 1, 1},
};

static int listed(uint32_t face, uint32_t row) {
    for (int i = 0; i < n_extra; i++)
        if (extra[i].row == row && face <= 13 && (extra[i].faces >> face & 1u)) return 1;
    return 0;
}

// The colours of face's list: one per story-character row it lists, on icon cells of hairstyles it does not list (the rows
// of the faces of its gender that are not in its list). With the built-in list every creator hairstyle is listed, so
// there is no free cell and no colour.
static void assign_npc(uint32_t face) {
    n_npc = 0;
    if (face > 13) return;
    int shown[14] = {0};
    int lo = face < 7 ? 0 : 7, hi = face < 7 ? 7 : 14;
    for (int s = lo; s < hi; s++)
        shown[s] = (uint32_t)s == face || listed(face, fi[s].r0);
    uint32_t donor[64];
    int nd = 0;
    for (int s = lo; s < hi; s++)
        for (int r = 0; !shown[s] && r < fi[s].n && nd < 64; r++) donor[nd++] = (uint32_t)(fi[s].icon0 + r);
    for (uint32_t row = NPC_FIRST; row <= NPC_LAST && n_npc < 16; row++) {
        if (!listed(face, row)) continue;
        if (n_npc >= nd) break;
        Npc *p = &npc[n_npc];
        p->row = row;
        p->icon = (uint8_t)donor[n_npc];
        p->cell = icon_no[p->icon];
        const uint8_t *c = palette[n_npc % 16];
        p->r = c[0], p->g = c[1], p->b = c[2];
        n_npc++;
    }
}

static uint64_t cur_builder;   // the hair list builder's object (list face at +0xbe0), noted at each build
static void driver_start(void);

void avatar_hair_face(uint64_t self, uint64_t face) {
    parse();
    cur_builder = self;
    driver_start();
    if ((uint32_t)face != cur_face) {
        if (cur_face != 0xffffffffu && page != 0 && !custom) { page = 0; build_default(); }   // another base face (the gender or the face was changed): its own page first (a page change by the player marks the face unknown, see page_apply)
        rt_log("avatar hair: list built for face %u", (uint32_t)face);
        cur_face = (uint32_t)face;
        assign_npc(cur_face);
        set_overlay(cur_face);
    }
    (void)self;
}

// The face whose own list the game puts a row on: the eight rows of each face, and the bonus rows (133 to 141).
static int native_face(uint32_t row) {
    static const int8_t bonus[9] = {10, 0, 1, 2, 9, 7, 8, 7, 10};   // rows 133 to 141
    for (int f = 0; f < 14; f++) if (row >= fi[f].r0 && row < (uint32_t)fi[f].r0 + fi[f].n) return f;
    return row >= 133 && row <= 141 ? bonus[row - 133] : -1;
}

int avatar_hair_avt(Cpu *c) {
    if (!n_extra || cur_face == 0xffffffffu) return 0;
    uint32_t row = LD32(c->x[0]);
    if (page && !custom && cur_face <= 13 && native_face(row) == (int)cur_face && !listed(cur_face, row)) {
        c->x[0] = 0xffff;   // the face's own rows are not on this page: another face
        return 1;
    }
    for (int i = 0; i < n_extra; i++) {
        if (cur_face > 13 || !(extra[i].faces >> cur_face & 1u) || extra[i].row != row) continue;
        if (!extra[i].logged && row >= NPC_FIRST && row <= NPC_LAST) {
            extra[i].logged = 1;
            rt_log("avatar hair: row %u is in the list of face %u (loop index %u)", row, cur_face, (unsigned)c->x[22]);
        }
        c->x[0] = cur_face;
        return 1;
    }
    return 0;
}

// The IconID reader: a story character's row answers with the icon of its colour's cell. c->x[0] points at the row's
// handle (its first word is the row number).
int avatar_hair_iconid(Cpu *c) {
    if (!n_npc || cur_face > 13) return 0;
    uint32_t row = LD32(c->x[0]);
    for (int i = 0; i < n_npc; i++) {
        if (npc[i].row != row) continue;
        c->x[0] = npc[i].icon;
        return 1;
    }
    return 0;
}

// The instruction after the list builder's IconID answer: 0 (a row with no icon and no colour) becomes the first icon.
uint64_t avatar_hair_icon(uint64_t icon) {
    return n_extra && (icon & 0xffff) == 0 ? PLACEHOLDER_ICON : icon;
}

// The creator's grid builds its own array of items, one 12-byte entry {u16 kind, u32 row, u16 flags} per listed row in row
// order (the loop at main+0x76ea00, array at the grid object +0x850 to +0x858), and then selects the entry of the current
// hairstyle by its position in that order (the UI's word +0x70c, handed to the widget at main+0x76de70). Moves the story
// characters' entries to the end, keeping the order of the others, and returns the position the current hairstyle then has.
#define ITEM_BYTES 12
#define MAX_ITEMS 192
// 0 = a row of the face's gender (the creator's own rows and the bonus rows), 1 = the other gender's, 2 = a story character's.
static int row_group(uint32_t face, uint32_t row) {
    if (row >= NPC_FIRST && row <= NPC_LAST) return 2;
    int male_row = row <= 56 || (row >= 134 && row <= 136);
    return male_row == (face < 7) ? 0 : 1;
}
static uint32_t natural_rows(uint32_t face, uint32_t *out) {   // the rows of face's list, ascending
    uint32_t n = 0;
    for (uint32_t r = 1; r <= 141 && n < MAX_ITEMS; r++)
        if ((!page && r >= fi[face].r0 && r < fi[face].r0 + fi[face].n) || listed(face, r)) out[n++] = r;
    return n;
}

// Entry of the HigeType reader (x0 = the base face's row handle). The creator's category list asks it (return address
// main+0x76d814) which beard categories a face has: 0 none, 1 Mustache, 2 Mustache and Beard. A male face answers 2 so
// every male face has both. Returns 1 when it answered (the reader must return at once). Off unless XCX_HAIR_BEARDS is set:
// the beard and mustache are part of each head's own assets (the older faces show them, the faces 0 to 2 lack some), so the
// extra tabs only offer choices that do not show on those heads.
int avatar_hair_hige(Cpu *c) {
    if (c->x[30] != 0x700076d814ull) return 0;
    // On when XCX_HAIR_BEARDS is set (0 turns it off), else when the beard mod (tools/beard_mod.py) is installed: its head
    // models give faces 0 to 2 the facial hair they lack, so the tabs then show something on every male face.
    const char *env = cfg_get("XCX_HAIR_BEARDS");
    char probe[256];
    if (env ? *env == '0' : !romfs_mod_file("chr/fc/fc111002.wimdo", probe, sizeof probe)) return 0;
    uint32_t face = LD32(c->x[0]);
    if (face > 6) return 0;
    c->x[0] = 2;
    return 1;
}

static uint64_t cur_grid;   // the creator's grid widget (UI + 0x3f38), noted when its items are built

// Entry of the grid widget's "set the cursor" call: for the creator's grid the scroll flag (w2) is raised, so a cursor below
// the first page of a long list scrolls into view. The game's own lists fit one page and every caller passes 0 or 1.
static uint64_t cur_ui;   // the creator's UI object (defined again below)
void avatar_hair_setcursor(Cpu *c) {
    static int logged_calls;
    if (cur_grid && c->x[0] == cur_grid && (cfg_get("XCX_HAIR_WATCH") || logged_calls++ < 60))
        rt_log("avatar hair: set cursor item %u flag %u (the UI's position word %u)", (unsigned)c->x[1], (unsigned)c->x[2], cur_ui ? LD32(cur_ui + 0x70c) : 0u);
    if (cur_grid && c->x[0] == cur_grid && (uint32_t)c->x[2] == 0) c->x[2] = 1;
}

static uint64_t cur_ui;   // the creator's UI object, noted when the grid's items are built
static uint32_t grid_order[192], grid_n;   // the rows of the grid in the order it shows them
static void watch_start(void);

static volatile int fresh_pick = 1;   // the preset's hairstyle has not been looked up yet: the game's position word is still the one it worked out from the preset (in the face's own list)

uint64_t avatar_hair_items(uint64_t ui, uint64_t index) {
    uint32_t face = LD32(ui + 0x7c8);
    cur_ui = ui;
    watch_start();
    cur_grid = face <= 13 ? LD64(ui + 0x3f38) : 0;
    if (!n_extra || face > 13 || face != cur_face) return index;
    uint64_t grid = LD64(ui + 0x3f38);
    if (!grid) return index;
    uint64_t b = LD64(grid + 0x850), e = LD64(grid + 0x858);
    if (e <= b || (e - b) % ITEM_BYTES || (e - b) / ITEM_BYTES > MAX_ITEMS) return index;
    uint32_t n = (uint32_t)((e - b) / ITEM_BYTES), want[MAX_ITEMS], nw = natural_rows(face, want);
    if (n != nw) return index;                    // not the hairstyle grid of this face
    uint32_t row[MAX_ITEMS], w[MAX_ITEMS][3], k = 0;
    int natural = 1, ordered = 1;
    for (uint32_t i = 0; i < n; i++) row[i] = LD32(b + ITEM_BYTES * (uint64_t)i + 4);
    for (uint32_t i = 0; i < n; i++) natural &= row[i] == want[i];
    // the order this function leaves: the face's gender, then the other gender, then the story characters' rows
    uint32_t order[MAX_ITEMS];
    for (int g = 0; g < 3; g++)
        for (uint32_t i = 0; i < nw; i++)
            if (row_group(face, want[i]) == g) order[k++] = want[i];
    for (uint32_t i = 0; i < n; i++) ordered &= row[i] == order[i];
    grid_n = nw;
    for (uint32_t i = 0; i < nw; i++) grid_order[i] = order[i];
    if (!natural && !ordered) return index;       // some other arrangement: leave it alone
    if (natural) {
        for (uint32_t i = 0; i < n; i++) {        // rewrite the entries in the new order
            uint32_t from = 0;
            while (row[from] != order[i]) from++;
            for (int q = 0; q < 3; q++) w[i][q] = LD32(b + ITEM_BYTES * (uint64_t)from + 4u * q);
        }
        for (uint32_t i = 0; i < n; i++)
            for (int q = 0; q < 3; q++) ST32(b + ITEM_BYTES * (uint64_t)i + 4u * q, w[i][q]);
        static int logged;
        if (!logged++) rt_log("avatar hair: the grid's entries ordered by gender and story characters (%u entries)", n);
    }
    // The word is written back below, so a later read of it (the cursor when the focus enters the grid) sees the position in this list too; a call that finds its
    // own value there has nothing to translate.
    static uint64_t stored_ui;
    static uint32_t stored_face = ~0u, stored_val = ~0u;
    if (!page && ui == stored_ui && face == stored_face && index == stored_val) return index;
    if (index >= n) return index;
    // The game found the position of the preset's hairstyle among the face's own list (its eight rows, then the bonus rows), not in the longer list built here:
    // the face's own rows come first, so position k is row r0 + k, and after them the bonus rows of its gender. On the first face that is the same as the
    // position in row order, on every other face it is not (the check mark of the preset 6 of the first screen sat on a hairstyle made for the first face).
    uint32_t target = want[index];
    if (!page && fresh_pick) {   // only the first build after a preset was chosen: later the word is a position in this list (the game writes it when a hairstyle is picked)
        static const uint8_t bonus_m[] = {134, 135, 136}, bonus_f[] = {133, 137, 138, 139, 140, 141};
        const uint8_t *bonus = face < 7 ? bonus_m : bonus_f;
        uint32_t nb = face < 7 ? sizeof bonus_m : sizeof bonus_f;
        if (index < fi[face].n) target = fi[face].r0 + index;
        else if (index - fi[face].n < nb) target = bonus[index - fi[face].n];
    }
    for (uint32_t i = 0; i < n; i++)
        if (order[i] == target) {
            static uint32_t last_logged = ~0u;
            if (last_logged != (face << 16 | i)) { last_logged = face << 16 | i; rt_log("avatar hair: face %u, the game's position %u of the worn hairstyle is row %u, at position %u of the list", face, (unsigned)index, target, i); }
            if (!page && fresh_pick) { ST32(ui + 0x70c, i); stored_ui = ui; stored_face = face; stored_val = i; fresh_pick = 0; }
            return i;
        }
    return index;
}

// ---- the painted cells

#define SHEET_W 1664
#define SHEET_H 416
#define CELLS_PER_ROW 16
#define CELL_BLOCKS 26   // a cell is 104 texels, 26 BC7 blocks, at level 0

static PlatMutex ov_lock = PLAT_MUTEX_INIT;
static struct { uint8_t cell, r, g, b; } ov[16];
static uint8_t ov_num[64];   // per icon cell, the first of the two presets the hairstyle belongs to (1 to 13; 0 none): painted as two numbers at the bottom left
static uint8_t ov_bad[40];   // the cells of the hairstyles that still do not fit, marked with an X
static int n_ov, n_bad, ov_sex = -1, ov_page;
static uint32_t ov_gen = 1;
// FNV-1a of level 0 of the male and the female icon sheet as the 2.0.0 game stores it (read from the game, see the log line
// below); only these two textures are painted.
static const uint64_t sheet_hash[2] = {0x51de74fc5fa76839ull, 0xc542aeaf3f7ce8daull};

// Rows of ITM_AvatarHairList that still do not sit right (docs/character-creator.md, "Fit of other-gender hair"): the scalp cap of a hair cut for
// the other gender is inside the skull and shows a bare patch. They get an X on their icon so a hair that looks wrong without one is a fit
// that did not apply. The buzz cuts show bare scalp with a sparkle on every head, the game's own glitch.
static const uint8_t bad_female_on_male[] = {0};   // none: since the fix for hairs with bangs every female hair fits a male head
static const uint8_t bad_male_on_female[] = {26, 33, 44, 45, 46, 49, 50, 53, 54, 56};   // the hair's own scalp shading is missing: skin shows where a male head shows stubble
static const uint8_t bad_buzz[] = {34, 47, 55};

// The two presets of the creator's first screen (numbered 1 to 14 in the order of its grid, per gender) that share each face (docs/character-creator.md): the
// first one's number, the second is the next.
static const uint8_t face_preset[14] = {1, 3, 5, 7, 9, 13, 11, 1, 5, 3, 7, 9, 11, 13};

static int row_cell(uint32_t row) {   // the cell of a creator row's icon in its gender's sheet, -1 for a row without one
    for (int f = 0; f < 14; f++)
        if (row >= fi[f].r0 && row < (uint32_t)fi[f].r0 + fi[f].n) return icon_no[fi[f].icon0 + (row - fi[f].r0)];
    return -1;
}

// The overlay for the list of face: the cells of its story-character colours, and of the hairstyles to mark. The sheet on show is the
// face's own gender's on page 0 and the other's on page 1.
static void set_overlay(uint32_t face) {
    typeof(ov) next;
    int n = n_npc;
    for (int k = 0; k < n && k < 16; k++) next[k].cell = npc[k].cell, next[k].r = npc[k].r, next[k].g = npc[k].g, next[k].b = npc[k].b;
    uint8_t bad[sizeof ov_bad];
    int nb = 0;
    if (page != 2) {
        int wearer_male = face < 7, sheet_male = (page == 0) == wearer_male;
        const uint8_t *tab = wearer_male ? bad_female_on_male : bad_male_on_female;
        size_t len = page == 1 ? (wearer_male ? sizeof bad_female_on_male : sizeof bad_male_on_female) : 0;
        for (size_t i = 0; i < len && nb < (int)sizeof bad; i++) { int c = row_cell(tab[i]); if (c >= 0) bad[nb++] = (uint8_t)c; }
        if (sheet_male)
            for (size_t i = 0; i < sizeof bad_buzz && nb < (int)sizeof bad; i++) { int c = row_cell(bad_buzz[i]); if (c >= 0) bad[nb++] = (uint8_t)c; }
    }
    uint8_t num[sizeof ov_num];
    memset(num, 0, sizeof num);
    if (page != 2) {   // the story characters' page reuses cells for its colours: no numbers there
        int sheet_male = (page == 0) == (face < 7);
        for (int f = sheet_male ? 0 : 7; f < (sheet_male ? 7 : 14); f++)
            for (int i = 0; i < fi[f].n; i++) { unsigned c = icon_no[fi[f].icon0 + i]; if (c < sizeof num) num[c] = face_preset[f]; }
    }
    plat_mutex_lock(&ov_lock);
    if (n != n_ov || memcmp(next, ov, sizeof next[0] * (size_t)n) || ov_sex != (face > 6) || ov_page != page || nb != n_bad || memcmp(bad, ov_bad, (size_t)nb) || memcmp(num, ov_num, sizeof num)) {
        memcpy(ov_num, num, sizeof num);
        memcpy(ov, next, sizeof next[0] * (size_t)n);
        n_ov = n;
        memcpy(ov_bad, bad, (size_t)nb);
        n_bad = nb;
        ov_sex = face > 6;
        ov_page = page;
        ov_gen++;
    }
    plat_mutex_unlock(&ov_lock);
}

// The other gender's icon sheet (page 1). The game uploads only the sheet of the creator's gender, so each sheet seen is kept
// (levels 0 and 1, as uploaded) and written next to the exe, avatar-hair-sheet-0 (male) or -1 (female); a later run reads the
// file when it needs the other sheet. Opening the other gender's creator once saves it.
static uint8_t *saved[2][2];
static size_t saved_len[2][2];
static int saved_written[2];

static int sheet_path(int sheet, char *out, size_t cap) {
    char exe[PLAT_PATH_MAX * 2];
    int n = plat_exe_path(exe, sizeof exe);
    if (!n || (size_t)n >= sizeof exe) return 0;
    char *cut = plat_last_sep(exe);
    if (!cut) return 0;
    *cut = 0;
    snprintf(out, cap, "%s/avatar-hair-sheet-%d", exe, sheet);
    return 1;
}

static void sheet_save(int sheet) {   // under ov_lock
    char path[PLAT_PATH_MAX * 2 + 40];
    if (saved_written[sheet] || !saved[sheet][0] || !saved[sheet][1] || !sheet_path(sheet, path, sizeof path)) return;
    saved_written[sheet] = 1;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t hd[3] = {0x31534948u, (uint32_t)saved_len[sheet][0], (uint32_t)saved_len[sheet][1]};
    fwrite(hd, sizeof hd, 1, f);
    fwrite(saved[sheet][0], 1, saved_len[sheet][0], f);
    fwrite(saved[sheet][1], 1, saved_len[sheet][1], f);
    fclose(f);
    rt_log("avatar hair: the %s icon sheet is saved (%s)", sheet ? "female" : "male", path);
}

static void sheet_load(int sheet) {   // under ov_lock
    char path[PLAT_PATH_MAX * 2 + 40];
    if (saved[sheet][0] || !sheet_path(sheet, path, sizeof path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint32_t hd[3];
    if (fread(hd, sizeof hd, 1, f) == 1 && hd[0] == 0x31534948u && hd[1] && hd[2] && hd[1] < (1u << 26) && hd[2] < (1u << 26)) {
        uint8_t *a = malloc(hd[1]), *b = malloc(hd[2]);
        if (a && b && fread(a, 1, hd[1], f) == hd[1] && fread(b, 1, hd[2], f) == hd[2]) {
            saved[sheet][0] = a; saved[sheet][1] = b;
            saved_len[sheet][0] = hd[1]; saved_len[sheet][1] = hd[2];
            saved_written[sheet] = 1;
            rt_log("avatar hair: the %s icon sheet is read from %s", sheet ? "female" : "male", path);
        } else { free(a); free(b); }
    }
    fclose(f);
}

uint32_t avatar_icon_gen(void) { return __atomic_load_n(&ov_gen, __ATOMIC_RELAXED); }

// A BC7 block that is one colour: mode 6, both endpoints the colour (its p-bits are 1, so the 8-bit channels are odd),
// every index 0.
static void bc7_solid(uint8_t *b, uint8_t r, uint8_t g, uint8_t bl) {
    unsigned __int128 v = (unsigned __int128)1 << 6;
    unsigned c[8] = {r >> 1, r >> 1, g >> 1, g >> 1, bl >> 1, bl >> 1, 127, 127};
    for (int i = 0; i < 8; i++) v |= (unsigned __int128)c[i] << (7 + 7 * i);
    v |= (unsigned __int128)3 << 63;
    for (int i = 0; i < 16; i++) b[i] = (uint8_t)(v >> (8 * i));
}

// A BC7 block of two colours (mode 6, p-bits 1, so the endpoints are odd values): pixel i is colour b when bit i of mask is set. Index 15 is
// endpoint 1 and 0 endpoint 0; the first pixel has a 3-bit index, so a block whose first pixel is colour b swaps the endpoints.
static void bc7_two(uint8_t *blk, const uint8_t a[3], const uint8_t b[3], unsigned mask) {
    int swap = mask & 1;
    const uint8_t *e0 = swap ? b : a, *e1 = swap ? a : b;
    unsigned __int128 v = (unsigned __int128)1 << 6;
    unsigned c[8] = {e0[0] >> 1, e1[0] >> 1, e0[1] >> 1, e1[1] >> 1, e0[2] >> 1, e1[2] >> 1, 127, 127};
    for (int i = 0; i < 8; i++) v |= (unsigned __int128)c[i] << (7 + 7 * i);
    v |= (unsigned __int128)3 << 63;
    for (int p = 1; p < 16; p++)
        if ((mask >> p & 1) ^ (unsigned)swap) v |= (unsigned __int128)15 << (68 + 4 * (p - 1));
    for (int i = 0; i < 16; i++) blk[i] = (uint8_t)(v >> (8 * i));
}

// The marker of a hairstyle that does not fit yet: a red square with a white border and a white X, nb x nb blocks at the bottom left of a cell.
static void badge_paint(uint8_t *dst, uint32_t rb, uint32_t x0, uint32_t y0, uint32_t nb) {
    static const uint8_t red[3] = {191, 11, 11}, white[3] = {255, 255, 255};
    float n = (float)(4 * nb);
    for (uint32_t by = 0; by < nb; by++)
        for (uint32_t bx = 0; bx < nb; bx++) {
            unsigned mask = 0;
            for (uint32_t py = 0; py < 4; py++)
                for (uint32_t px = 0; px < 4; px++) {
                    float u = ((float)(bx * 4 + px) + 0.5f) / n, v = ((float)(by * 4 + py) + 0.5f) / n;
                    int ink = u < 0.1f || u > 0.9f || v < 0.1f || v > 0.9f || fabsf(u - v) < 0.12f || fabsf(u + v - 1.0f) < 0.12f;
                    if (ink) mask |= 1u << (py * 4 + px);
                }
            bc7_two(dst + (size_t)(y0 + by) * rb + (size_t)(x0 + bx) * 16, red, white, mask);
        }
}

// A number badge: the digits of n (3 x 5 dots, each dot s x s texels) in white. The blocks that hold the digits are two-colour blocks of the icon's own
// dark background, so only the digits show; the badge is as small as its digits (with a texel of margin). *bw and *bh give its size in blocks.
static void number_size(unsigned n, int s, uint32_t *bw, uint32_t *bh) {
    int nd = n >= 10 ? 2 : 1;
    *bw = (uint32_t)((nd * 3 * s + (nd - 1) * s + 2 + 3) / 4);
    *bh = (uint32_t)((5 * s + 2 + 3) / 4);
}
static void number_paint(uint8_t *dst, uint32_t rb, uint32_t x0, uint32_t y0, unsigned n, int s) {
    static const uint8_t font[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    static const uint8_t bg[3] = {27, 31, 39}, ink[3] = {255, 255, 255};   // the icon sheet's background is a dark grey-blue
    int digits[2], nd = 0;
    if (n >= 10) digits[nd++] = (int)(n / 10 % 10);
    digits[nd++] = (int)(n % 10);
    uint32_t nbw, nbh;
    number_size(n, s, &nbw, &nbh);
    int tw = nd * 3 * s + (nd - 1) * s, tx = ((int)nbw * 4 - tw) / 2, ty = ((int)nbh * 4 - 5 * s) / 2;
    for (uint32_t by = 0; by < nbh; by++)
        for (uint32_t bx = 0; bx < nbw; bx++) {
            unsigned mask = 0;
            for (int py = 0; py < 4; py++)
                for (int px = 0; px < 4; px++) {
                    int X = (int)bx * 4 + px, Y = (int)by * 4 + py, on = 0;
                    int row = (Y - ty) / s;
                    if (Y >= ty && row < 5)
                        for (int d = 0; d < nd; d++) {
                            int dx = X - (tx + d * 4 * s);
                            if (dx >= 0 && dx < 3 * s && (font[digits[d]][row] >> (2 - dx / s) & 1)) on = 1;
                        }
                    if (on) mask |= 1u << (py * 4 + px);
                }
            bc7_two(dst + (size_t)(y0 + by) * rb + (size_t)(x0 + bx) * 16, bg, ink, mask);
        }
}

uint32_t avatar_icon_patch(uint32_t w, uint32_t h, int bpb, int bw, unsigned level, uint8_t *dst, uint32_t rb, uint32_t rows) {
    static __thread int sheet = -2;   // -2 not a hair icon sheet, 0 male, 1 female
    static __thread uint32_t gen;
    static __thread int n;
    static __thread typeof(ov) mine;
    static __thread uint8_t mybad[sizeof ov_bad], mynum[sizeof ov_num];
    static __thread int sex, mypage, nbad;
    if (level == 0) {
        sheet = -2;
        if (!n_extra || w != SHEET_W || h != SHEET_H || bpb != 16 || bw != 4) return 0;
        uint64_t hash = 1469598103934665603ull;
        for (size_t i = 0, len = (size_t)rb * rows; i < len; i++) hash = (hash ^ dst[i]) * 1099511628211ull;
        sheet = hash == sheet_hash[0] ? 0 : hash == sheet_hash[1] ? 1 : -2;
        static uint64_t seen[4];
        int fresh = 1;
        for (int i = 0; i < 4; i++) fresh &= seen[i] != hash;
        if (fresh) {
            for (int i = 3; i > 0; i--) seen[i] = seen[i - 1];
            seen[0] = hash;
            rt_log("avatar hair: icon-sized texture %016llx (%s)", (unsigned long long)hash,
                   sheet < 0 ? "not a hair icon sheet" : sheet ? "female hair icons" : "male hair icons");
        }
        plat_mutex_lock(&ov_lock);
        gen = ov_gen, n = n_ov, sex = ov_sex, mypage = ov_page, nbad = n_bad;
        memcpy(mine, ov, sizeof mine);
        memcpy(mybad, ov_bad, sizeof mybad);
        memcpy(mynum, ov_num, sizeof mynum);
        plat_mutex_unlock(&ov_lock);
    }
    if (sheet == -2) return 0;
    if (level <= 1) {
        size_t len = (size_t)rb * rows;
        plat_mutex_lock(&ov_lock);
        if (!saved[sheet][level] || saved_len[sheet][level] != len) {   // keep the sheet as the game uploads it
            free(saved[sheet][level]);
            saved[sheet][level] = malloc(len);
            saved_len[sheet][level] = len;
            if (saved[sheet][level]) memcpy(saved[sheet][level], dst, len);
            else saved_len[sheet][level] = 0;
        }
        sheet_save(sheet);
        if (mypage == 1 && sheet == sex) {   // the other gender's page: that gender's sheet in place of this one
            sheet_load(1 - sheet);
            if (saved[1 - sheet][level] && saved_len[1 - sheet][level] == len) memcpy(dst, saved[1 - sheet][level], len);
            else if (level == 0) rt_log("avatar hair: the %s icon sheet is not known yet (open the %s creator once)", sheet ? "male" : "female", sheet ? "male" : "female");
        }
        plat_mutex_unlock(&ov_lock);
    }
    if (level <= 1 && n && sheet == sex) {
        uint32_t cb = CELL_BLOCKS >> level;
        for (int k = 0; k < n; k++) {
            uint32_t x0 = (mine[k].cell % CELLS_PER_ROW) * cb, y0 = (mine[k].cell / CELLS_PER_ROW) * cb;
            if ((x0 + cb) * 16 > rb || y0 + cb > rows) continue;
            for (uint32_t y = y0; y < y0 + cb; y++)
                for (uint32_t x = x0; x < x0 + cb; x++) bc7_solid(dst + (size_t)y * rb + (size_t)x * 16, mine[k].r, mine[k].g, mine[k].b);
        }
    }
    if (level <= 1 && nbad && sheet == sex) {
        uint32_t cb = CELL_BLOCKS >> level, nbk = level ? 4 : 7;
        for (int k = 0; k < nbad; k++) {
            uint32_t x0 = (mybad[k] % CELLS_PER_ROW) * cb + cb - nbk, y0 = (mybad[k] / CELLS_PER_ROW) * cb + cb - nbk;   // at the bottom right: the numbers take the left
            if ((x0 + nbk) * 16 > rb || y0 + nbk > rows) continue;
            badge_paint(dst, rb, x0, y0, nbk);
        }
    }
    if (level <= 1 && sheet == sex) {   // the presets the hairstyle belongs to: two small numbers at the bottom left of its icon
        uint32_t cb = CELL_BLOCKS >> level;
        int sc = level ? 1 : 3;
        for (uint32_t c = 0; c < sizeof mynum; c++) {
            if (!mynum[c]) continue;
            uint32_t w1, h1, w2, h2;
            number_size(mynum[c], sc, &w1, &h1);
            number_size(mynum[c] + 1u, sc, &w2, &h2);
            uint32_t x0 = (c % CELLS_PER_ROW) * cb + 1, y0 = (c / CELLS_PER_ROW) * cb + cb - h1 - 1;
            if ((x0 + w1 + 2 + w2) * 16 > rb || y0 + h2 > rows) continue;
            number_paint(dst, rb, x0, y0, mynum[c], sc);
            number_paint(dst, rb, x0 + w1 + 2, y0 + h1 - h2, mynum[c] + 1u, sc);   // two blocks apart: "1 2" does not read as twelve
        }
    }
    return level == 0 ? gen + 1 : 0;
}


// ---- pages and the review run: one helper thread (started with the first list) watches the page button (Switch Y: Triangle on a
// PlayStation pad), switches the page and has the game build its list again. The hair tab is active
// while the UI word at +0x5fc is 5 and its style grid has the focus while the word at +0x758 is 1; the grid's cursor is its
// word at +0xc (found with XCX_HAIR_WATCH).
#define B_L (1ull << 6)
#define B_R (1ull << 7)
#define B_A 1ull
#define B_Y_BIT (1ull << 3)
#define B_RIGHT (1ull << 14)
#define B_LEFT (1ull << 12)

#define SND_BACK 0x61c     // the UI object's field of the sound the player recognised as the creator's "back" (id 4; the widgets at main+0x779200 to +0x7796cc play it)
#define SND_SLIDER 0x62c   // the field of the sound the Size tab's sliders play at each step (id 107, played by main+0x76a2bc / +0x76a4b4 in the log of a run of slider steps); id 5 (+0x620) is the tab switch

static volatile int driver_on;   // the helper thread runs
static int hair_tab_active(void) { return cur_ui && LD16(cur_ui + 0x5fc) == 5; }
static int grid_focus(void) { return cur_ui && LD32(cur_ui + 0x758) == 1; }

// Entry of main+0x40bc48, the game's "play sound" (sound manager in x0, sound id in w1): while the hair tab is open, each sound it plays is logged with its return
// address, so the ids of the creator's own sounds (a category's A and B, a slider step) can be read off the log and the Modify window can use the same ones. Our own plays
// come from main+0x7788dc (the return address is that).
void avatar_hair_snd(Cpu *c) {
    static int n;
    if (!cur_ui || n >= 1500 || c->x[30] == 0x700040bc48ull) return;   // every tab of the creator (the body tab's slider too); not our own plays
    n++;
    rt_log("avatar hair: sound %u played from %#llx (tab %u, category %u, grid focus %u)", (unsigned)c->x[1], (unsigned long long)c->x[30], (unsigned)LD16(cur_ui + 0x5fc), LD32(cur_ui + 0x708), (unsigned)grid_focus());
}

// The game's "Discard current edits" question is up: the UI object's state word (+0x68) reads 41 while it is on screen (0 otherwise; found by pressing B over and over on the
// Modify row and logging the word, which the earlier guess from the B presses alone lost track of when the presses came faster than the question opened and closed).
static int dialog_up(void) { return cur_ui && hair_tab_active() && LD32(cur_ui + 0x68) == 41; }
int avatar_hair_dialog(void) { return dialog_up(); }

// A new page: its rows, colours and sheet. The list is built again when the tab is entered again (page_bounce).
static void page_apply(void) {
    if (custom) return;
    build_default();
    cur_face = 0xffffffffu;   // the next build notes its face again: colours and the icon sheet follow the page
    if (cur_builder) ST32(cur_builder + 0xbe0, 0xffffffffu);
}

static void page_bounce(void) {   // out of the tab and back, so the game builds the list and the grid again (the review run and XCX_HAIR_BOUNCE)
    input_inject(B_R, 4);
    plat_sleep_ms(700);
    input_inject(B_L, 4);
    plat_sleep_ms(900);
}

// The hair tab built again where it stands: the game's per-frame UI update (main+0x768d50) calls avatar_hair_tick, which, when a page change
// is waiting, runs the tab's own entry routine (main+0x76d45c: the tab word, the category list with the hair list, the grid) on the game's
// thread. The tab is not left, so the camera and the other panels stay as they are.
static volatile int refresh_req, refresh_runs, focus_req;   // focus_req: the category list takes the focus back (state 0xd of the same routine), after the Modify window
static volatile uint32_t refresh_state = 0xb;   // the UI state the entry routine is given (XCX_HAIR_STATE)

int avatar_hair_tick(Cpu *c) {
    if (!cur_ui || c->x[0] != cur_ui) return 0;
    static uint32_t last_state = ~0u, last_cat = ~0u, last_focus = ~0u;
    static unsigned ticks;
    ticks++;
    if (ticks <= 2 || ticks % 3600 == 0) rt_log("avatar hair: ui update %u (state %u, tab %u)", ticks, LD32(cur_ui + 0x68), (unsigned)LD16(cur_ui + 0x5fc));
    if (hair_tab_active()) {   // what the UI does while the tab is open, for the log
        uint32_t st = LD32(cur_ui + 0x68), cat = LD32(cur_ui + 0x708), fo = LD32(cur_ui + 0x758);
        if (st != last_state || cat != last_cat || fo != last_focus) {
            last_state = st; last_cat = cat; last_focus = fo;
            rt_log("avatar hair: ui state %u, category %u, grid focus %u", st, cat, fo);
        }
    }
    tick_count++;
    {   // the parts of the hairstyle: after a change the bones are decided again; a few frames later the parts they belong to are the ones the window offers
        static uint32_t lg; static unsigned lt;
        if (ib_gen + sw_gen != lg) { lg = ib_gen + sw_gen; lt = tick_count; seen_acc = 0; memset((void *)seen_cnt, 0, sizeof seen_cnt); seen_root = 0; }
        else if (lt && tick_count >= lt + 10) {
            lt = 0;
            if (seen_acc) mod_seen |= seen_acc;   // only ever adds: a decision round that reached some bones only must not take a part (Front, other strands) out of the window and put it back on the next change
            if (mod_any) rt_log("avatar hair modify: bones in use: cap %u front %u sides %u tails %u other %u, root %u; parts offered %#x", seen_cnt[MG_CAP], seen_cnt[MG_FRONT], seen_cnt[MG_SIDE], seen_cnt[MG_TAIL], seen_cnt[MG_REST], seen_root, (unsigned)mod_seen);
        }
    }
    {   // back at the base or gender selection (the UI's tab word is 1 or 2 there; the tabs are 3 and above): the hair fit and the player's Modify values belong to the face that was
        // being edited, so they end here. Left on, the fit goes on using the old face's skeleton and values for the next gender's model until the hair tab is built again.
        static int selecting;
        uint32_t tw = LD16(cur_ui + 0x5fc);
        if (tw < 3 && !selecting) {
            selecting = 1;
            int had = mod_any || cur_face != 0xffffffffu;
            mod_flush();
            memset(mod_v, 0, sizeof mod_v); memset(mod_b, 0, sizeof mod_b);
            mod_nstore = 0; mod_cur_key = mod_last_key = 0xffffffffu;
            mod_any = 0; mod_seen = 0; mod_open = 0; mod_cross = 1;
            cur_face = 0xffffffffu; cur_grid = 0;
            fresh_pick = 1;
            ib_gen++;
            if (had) rt_log("avatar hair: back at the base selection (tab word %u): the fit and the Modify values are cleared", tw);
        } else if (tw >= 3) selecting = 0;
    }
    {   // the hair tab was left: the next visit starts on the page of the wearer's own gender
        static int was_hair;
        int now = hair_tab_active();
        if (was_hair && !now && page != 0 && !custom) { page = 0; page_apply(); rt_log("avatar hair: the tab was left, the next visit starts on page 0"); }
        was_hair = now;
    }
    if (snd_check && tick_count >= snd_check) { snd_check = 0; if (mod_open && !grid_focus()) snd_req = 1; }   // A opened the window but the game did not take it: the confirm sound is ours
    if (hair_tab_active()) {   // once: the sound ids the UI keeps for its widgets (the log names the sounds the game plays itself, see avatar_hair_snd)
        static int slots_logged;
        if (!slots_logged) {
            slots_logged = 1;
            rt_log("avatar hair: UI sound ids +0x610 %u +0x614 %u +0x618 %u +0x61c %u +0x620 %u +0x624 %u +0x628 %u +0x62c %u +0x630 %u",
                   LD32(cur_ui + 0x610), LD32(cur_ui + 0x614), LD32(cur_ui + 0x618), LD32(cur_ui + 0x61c), LD32(cur_ui + 0x620), LD32(cur_ui + 0x624), LD32(cur_ui + 0x628), LD32(cur_ui + 0x62c), LD32(cur_ui + 0x630));
        }
    }
    if (snd_req) {
        int snd = snd_req;
        snd_req = 0;
        Cpu keep = *c;
        if (snd >= 2) {   // 2: B left the window (the sound the player heard as the creator's own "back": the slot +0x61c, id 4); 3: a value changed (the slider's slot, a guess the log checks)
            uint64_t mgr = LD64(0x7001d57598ull);
            uint32_t id = LD32(cur_ui + (snd == 2 ? SND_BACK : SND_SLIDER));
            if (mgr && id) {
                c->x[0] = mgr; c->x[1] = id; c->x[2] = 0;
                c->x[30] = 0x700040bc48ull;
                c->pc = 0x700040bc48ull;
                rt_dispatch(c);
            }
        } else {
            c->x[0] = cur_ui;
            c->x[1] = 0;
            c->x[30] = 0x70007788dcull;
            c->pc = 0x70007788dcull;
            rt_dispatch(c);
        }
        *c = keep;
    }
    if (virt_active && modify_on() && hair_tab_active() && !grid_focus() && !mod_open && !refresh_req && !focus_req) {   // the cursor wrapped past an end of the window: scroll it
        static uint32_t last_c = ~0u;
        uint32_t cc = LD32(cur_ui + 0x708);
        int wrap = last_c != ~0u && ((last_c == 6 && cc == 0) || (last_c == 0 && cc == 6));
        // a list that stops at its ends instead of wrapping: a Down at the last or an Up at the first row that moved nothing
        if (!wrap && last_c == cc && ((dn_edge && cc == 6 && tick_count > dn_edge + 2) || (up_edge && cc == 0 && tick_count > up_edge + 2))) wrap = 1;
        if (cc != last_c || wrap) dn_edge = up_edge = 0;
        if (wrap) {
            virt_off = virt_off ? 0 : 1;
            virt_cursor = virt_off ? 6 : 0;
            virt_rebuild = 1;
            refresh_req = 1;
            rt_log("avatar hair: the category list scrolls to rows %d to %d", virt_off + 1, virt_off + 7);
        }
        last_c = wrap ? ~0u : cc;
    }
    int back = focus_req;
    if (!refresh_req && !back) return 0;
    if (back) focus_req = 0; else refresh_req = 0;
    if (!hair_tab_active()) return 0;
    static int env_done;
    if (!env_done) { env_done = 1; if (cfg_get("XCX_HAIR_STATE")) refresh_state = (uint32_t)strtoul(cfg_get("XCX_HAIR_STATE"), NULL, 0); }
    Cpu keep = *c;
    uint32_t st_now = back ? 0xdu : refresh_state;
    if (st_now != 0xffffffffu) ST32(cur_ui + 0x68, st_now);
    c->x[0] = cur_ui;
    c->x[30] = 0x700076d45cull;
    c->pc = 0x700076d45cull;
    rt_dispatch(c);
    *c = keep;
    if (!back) refresh_runs++;
    rt_log("avatar hair: the hair tab was %s (state %u now %u)", back ? "given its focus back to the category list" : "built again in place", st_now, LD32(cur_ui + 0x68));
    return 0;
}

static int refresh_in_place(void) {   // from the helper thread: 1 once the game has done it
    static int bounce = -1;
    if (bounce < 0) bounce = cfg_get("XCX_HAIR_BOUNCE") != NULL;
    if (bounce) { page_bounce(); return 1; }
    int runs = refresh_runs;
    refresh_req = 1;
    for (int t = 0; t < 100 && refresh_runs == runs; t++) plat_sleep_ms(16);
    if (refresh_runs == runs) { refresh_req = 0; rt_log("avatar hair: the page change was not picked up by the UI update"); return 0; }
    return 1;
}

// After a page change the entry routine leaves the focus on the categories: press A until the grid has it (an A that comes while the UI is
// still busy with the change is lost, so it is pressed again).
static void regrid(void) {
    for (int tries = 0; tries < 6 && !grid_focus(); tries++) {
        plat_sleep_ms(tries ? 1000 : 1500);
        if (grid_focus()) break;
        input_inject(B_A, 4);
        for (int t = 0; t < 12 && !grid_focus(); t++) plat_sleep_ms(100);
    }
}

static void review_run(void) {
    const char *name = cfg_get("XCX_HAIR_REVIEW");
    double tune[16][9];   // XCX_HAIR_TUNE="scale,lift,forward;scale,lift,forward;...": each hairstyle is photographed once per set
    int ntune = 0;
    for (const char *s = cfg_get("XCX_HAIR_TUNE"); s && *s && ntune < 16;) {
        char *e;
        tune[ntune][0] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
        tune[ntune][1] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
        tune[ntune][2] = strtod(s, &e);
        tune[ntune][3] = tune[ntune][4] = tune[ntune][5] = tune[ntune][6] = tune[ntune][7] = tune[ntune][8] = 0;
        if (*e == ',') {   // a second triple: the scalp cap's own scale, lift and forward
            s = e + 1;
            tune[ntune][3] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
            tune[ntune][4] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
            tune[ntune][5] = strtod(s, &e);
        }
        if (*e == ',') {   // a third triple: the side locks' bones
            s = e + 1;
            tune[ntune][6] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
            tune[ntune][7] = strtod(s, &e); if (*e == ',') s = e + 1; else break;
            tune[ntune][8] = strtod(s, &e);
        }
        ntune++;
        if (*e == ';') s = e + 1; else break;
    }
    static double msets[16][MG_N][MP_N];   // XCX_HAIR_MODSETS="g,scale,x,y,z,tilt+g,...;...": candidate start values (g the part: 0 whole hair, 1 scalp cap, 2 front, 3 sides, 4 tails, 5 other strands; scale change, metres, degrees); each hairstyle is photographed once per set
    int nmod = 0;
    for (const char *s = cfg_get("XCX_HAIR_MODSETS"); s && *s && nmod < 16;) {
        char *e = (char *)s;
        memset(msets[nmod], 0, sizeof msets[nmod]);
        for (;;) {
            int g = (int)strtol(s, &e, 10);
            double v[MP_N] = {0};
            for (int k = 0; k < MP_N && *e == ','; k++) v[k] = strtod(e + 1, &e);
            if (g >= 0 && g < MG_N) for (int k = 0; k < MP_N; k++) msets[nmod][g][k] = v[k];
            if (*e == '+') s = e + 1; else break;
        }
        nmod++;
        if (*e == ';') s = e + 1; else break;
    }
    uint32_t only[128]; int nonly = 0;   // XCX_HAIR_ROWS="98,101,...": only these rows are visited
    for (const char *s = cfg_get("XCX_HAIR_ROWS"); s && *s && nonly < 128;) { char *e; only[nonly++] = (uint32_t)strtoul(s, &e, 10); if (*e == ',') s = e + 1; else break; }
    int pages = cfg_get("XCX_HAIR_REVIEW_PAGES") ? atoi(cfg_get("XCX_HAIR_REVIEW_PAGES")) : 1, first = page;
    for (int t = 0; t < 600 && !(hair_tab_active() && grid_focus() && grid_n); t++) plat_sleep_ms(250);
    if (!(hair_tab_active() && grid_focus() && grid_n)) { rt_log("avatar hair review: the style grid never got the focus (tab word %x, focus word %x)", cur_ui ? LD16(cur_ui + 0x5fc) : 0, cur_ui ? LD32(cur_ui + 0x758) : 0); return; }
    plat_sleep_ms(5000);
    if (cfg_get("XCX_HAIR_REVIEW_DEFAULT")) {   // XCX_HAIR_REVIEW_DEFAULT=<n>: the hairstyle the preset came with, untouched, photographed from n directions (the right stick turns the model between shots)
        int views = atoi(cfg_get("XCX_HAIR_REVIEW_DEFAULT"));
        for (int view = 0; view < views; view++) {
            if (view) { input_inject_rstick(32767, 0, 30); plat_sleep_ms(1800); }
            char shot[160];
            snprintf(shot, sizeof shot, "%s_default_v%d", name, view);
            boot_shot_request(shot, 0.50f, 0.10f, 0.82f, 0.75f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
        }
        rt_log("avatar hair review: default hairstyle done");
        if (cfg_get("XCX_HAIR_REVIEW_STAY")) return;
        plat_sleep_ms(1500);
        _exit(0);
    }
    for (int pg = 0; pg < pages; pg++) {
        if (pg) {
            page = (first + pg) % 3;
            page_apply();
            refresh_in_place();
            plat_sleep_ms(700);
            regrid();
            plat_sleep_ms(3000);
        }
        uint32_t n = grid_n;
        if (cfg_get("XCX_HAIR_REVIEW_MAX") && n > (uint32_t)atoi(cfg_get("XCX_HAIR_REVIEW_MAX"))) n = (uint32_t)atoi(cfg_get("XCX_HAIR_REVIEW_MAX"));   // only the first items of each page
        if (pg == 0 && cfg_get("XCX_HAIR_REVIEW_SKIPFIRST")) n = 0;   // the first page is only the way to the next one
        rt_log("avatar hair review: page %d, %u items", page, n);
        for (uint32_t k = cfg_get("XCX_HAIR_REVIEW_FROM") ? (uint32_t)atoi(cfg_get("XCX_HAIR_REVIEW_FROM")) : 0; k < n; k++) {
            if (nonly) { int in = 0; for (int q = 0; q < nonly; q++) in |= only[q] == grid_order[k]; if (!in) continue; }
            // move the cursor to item k, one press at a time, waiting for each to show (a press that is not waited for lands twice)
            for (int step = 0; step < 300 && LD32(cur_grid + 0xc) != k; step++) {
                uint32_t was = LD32(cur_grid + 0xc);
                input_inject(was < k ? B_RIGHT : B_LEFT, 4);
                for (int t = 0; t < 40 && LD32(cur_grid + 0xc) == was; t++) plat_sleep_ms(100);
                plat_sleep_ms(250);
            }
            uint32_t idx = LD32(cur_grid + 0xc), row = idx < n ? grid_order[idx] : 0;
            mod_seen = 0; mod_switch(row * 2u + (cur_face >= 7 ? 1u : 0u)); ib_gen++;   // the hairstyle's start values (avatar_hair_fits.inc), as the creator loads them
            plat_sleep_ms(3500);   // the hairstyle loads and settles
            if (cfg_get("XCX_HAIR_TURN"))   // XCX_HAIR_TURN=<n>: the model is turned n times by the right stick before the shots (a side or back view for the tuning sets)
                for (int a = 0; a < atoi(cfg_get("XCX_HAIR_TURN")); a++) { input_inject_rstick(32767, 0, 30); plat_sleep_ms(1500); }
            char shot[160];
            int views = cfg_get("XCX_HAIR_VIEWS") ? atoi(cfg_get("XCX_HAIR_VIEWS")) : 1;   // XCX_HAIR_VIEWS=<n>: every tuning set is photographed from n directions, the model turned one step between them
            if (nmod) {
              for (int view = 0; view < (cfg_get("XCX_HAIR_VIEWS") ? atoi(cfg_get("XCX_HAIR_VIEWS")) : 1); view++) {
                if (view) { input_inject_rstick(32767, 0, 30); plat_sleep_ms(1800); }
                for (int k = 0; k < nmod; k++) {
                    memcpy(mod_b, msets[k], sizeof mod_b);
                    modify_any_update(); ib_gen++;
                    plat_sleep_ms(900);
                    snprintf(shot, sizeof shot, "%s_v%d_t%d_p%d_i%03u_r%03u", name, view, k, page, idx, row);
                    boot_shot_request(shot, 0.50f, 0.10f, 0.82f, 0.75f);
                    for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
                }
                mod_load_baked(row * 2u + (cur_face >= 7 ? 1u : 0u)); modify_any_update(); ib_gen++;
              }
            } else if (ntune) {
              for (int view = 0; view < views; view++) {
                if (view) { input_inject_rstick(32767, 0, 30); plat_sleep_ms(1800); }
                for (int k = 0; k < ntune; k++) {
                    tune_ovr = 1;
                    tune_s = tune[k][0]; tune_dy = tune[k][1]; tune_dz = tune[k][2];
                    tune_cset = tune[k][3] != 0; tune_c[0] = tune[k][3]; tune_c[1] = tune[k][4]; tune_c[2] = tune[k][5];
                    tune_sdset = tune[k][6] != 0; tune_sd[0] = tune[k][6]; tune_sd[1] = tune[k][7]; tune_sd[2] = tune[k][8];
                    ib_gen++;
                    plat_sleep_ms(900);
                    snprintf(shot, sizeof shot, "%s_v%d_t%d_p%d_i%03u_r%03u", name, view, k, page, idx, row);
                    if (cfg_get("XCX_HAIR_REVIEW_WIDE")) boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f); else boot_shot_request(shot, 0.50f, 0.10f, 0.82f, 0.75f);
                    for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
                }
                tune_s = tune[0][0]; tune_dy = tune[0][1]; tune_dz = tune[0][2]; ib_gen++;
              }
            } else {
                snprintf(shot, sizeof shot, "%s_p%d_i%03u_r%03u", name, page, idx, row);
                if (cfg_get("XCX_HAIR_REVIEW_WIDE")) boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f); else boot_shot_request(shot, 0.50f, 0.10f, 0.82f, 0.75f);
                for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
            }
            if (cfg_get("XCX_HAIR_TURN") && cfg_get("XCX_HAIR_UNTURN"))
                for (int a = 0; a < atoi(cfg_get("XCX_HAIR_TURN")); a++) { input_inject_rstick(-32767, 0, 30); plat_sleep_ms(1500); }
            rt_log("avatar hair review: page %d item %u row %u", page, idx, row);
            if (cfg_get("XCX_HAIR_ANGLES")) {   // XCX_HAIR_ANGLES=<n>: the model is turned by the right stick n more times, a shot after each (the sides and the back of the hair)
                for (int a = 1; a <= atoi(cfg_get("XCX_HAIR_ANGLES")); a++) {
                    input_inject_rstick(32767, 0, 30);
                    plat_sleep_ms(1500);
                    snprintf(shot, sizeof shot, "%s_p%d_i%03u_r%03u_a%d", name, page, idx, row, a);
                    boot_shot_request(shot, 0.50f, 0.10f, 0.82f, 0.75f);
                    for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
                }
                if (cfg_get("XCX_HAIR_UNTURN"))   // XCX_HAIR_UNTURN=1: the model is turned back by as many steps, so every hairstyle starts from the same view
                    for (int a = 1; a <= atoi(cfg_get("XCX_HAIR_ANGLES")); a++) { input_inject_rstick(-32767, 0, 30); plat_sleep_ms(1500); }
            }
        }
    }
    rt_log("avatar hair review: done");
    if (cfg_get("XCX_HAIR_REVIEW_STAY")) return;   // XCX_HAIR_REVIEW_STAY=1: the game stays open on the last hairstyle for someone to look at
    plat_sleep_ms(1500);
    _exit(0);   // the review is the whole run: free the harness
}

// The Style category's grid is the page grid: the tab is the hair tab and the category cursor is on Style.
static int style_open(void) { return hair_tab_active() && LD32(cur_ui + 0x708) == 0; }

// The page button is Switch Y: the creator uses X (Save/Load Avatar Data) and the sticks, not Y. On a PlayStation or Xbox pad (the Xbox layout)
// the pad's top button is Y, so it is Triangle there; with the Nintendo layout it is the west button, and on the keyboard whatever Y is bound
// to (the left mouse button by default). The glyph system draws the glyph of whatever Y is bound to.
static int page_symbol(void) { return SYM_Y; }

// Modify's controls (read from the raw pad, the game's own input is blocked while the window is open): the window opens on the Modify row of the category
// list (A on the row; faces whose list is full use Minus). Up and Down walk the rows of all parts, Left and Right change the value (held: repeating; ZL for
// fine steps, ZR for coarse ones), L and R jump to the previous or next part, Y sets the row back, X all values, B closes. The window names the buttons by the
// glyphs of the controller in use.
#define MB_B (1ull << 1)
#define MB_X (1ull << 2)
#define MB_ZL (1ull << 8)
#define MB_ZR (1ull << 9)
#define MB_MINUS (1ull << 11)
#define MB_UP (1ull << 13)
#define MB_DOWN (1ull << 15)

static void modify_any_update(void) {
    int any = 0;
    for (int g = 0; g < MG_N; g++) for (int k = 0; k < MP_N; k++) if (mod_v[g][k] != 0.0 || mod_b[g][k] != 0.0) any = 1;
    mod_any = any;
}

// ---- the Modify row of the hair tab's category list
//
// The category list is a widget (UI + 0x2918) over a vector of 36-byte items, one per category: a message id for the label at +0, a kind at +0xa
// (6 Style, 7 Special, 8 Mustache, 9 Beard, 0xa to 0xc the colours). The tab's builder (main+0x76d7b0) fills the vector and then lays the widget out for
// as many rows as there are items; avatar_hair_cats adds one more item of kind KIND_MODIFY at that point, which makes the list longer exactly as
// the Mustache and Beard rows do on the faces that have them. The game empties the grid for a kind it does not know. The window of the row is drawn by
// the host overlay (runtime/ui/menu.cpp, modify_draw); the item's label is our own text (avatar_hair_text).
#define KIND_MODIFY 0xd
#define CAT_BYTES 36
#define MODIFY_MESSAGE 0x120f   // the message id the Modify item carries: the game's "Load Game", which no row of the hair tab uses; the text setter swaps in "Modify"
static const char modify_label[] = "Modify";
static uint64_t modify_text_guest;   // guest copy of the label (the text setter takes a guest pointer)
static int cats_logged;

static int modify_on(void) {
    const char *e = cfg_get("XCX_HAIR_MODIFY");
    return !custom && !(e && *e == '0');
}

// The kind of the row whose picture the Modify row shows: Style (6) unless another row was confirmed last (the Mustache and Beard rows, once their styles can be fitted);
// XCX_HAIR_MODICON=0 keeps the colour swatch the row had at first. Read once per build of the tab (no per-frame or per-draw work).
static uint32_t icon_kind;
static uint32_t modify_icon_kind(void) {
    const char *e = cfg_get("XCX_HAIR_MODICON");
    if (e && *e == '0') return 12;
    return icon_kind ? icon_kind : 6;
}

uint64_t avatar_hair_cats(uint64_t ui, uint64_t cursor) {
    cur_ui = ui;
    virt_active = 0;
    uint32_t face = LD32(ui + 0x7c8);
    if (!modify_on() || face > 13) return cursor;
    uint64_t w = LD64(ui + 0x2918);
    if (!w) return cursor;
    uint64_t b = LD64(w + 0x498), e = LD64(w + 0x4a0), cap = LD64(w + 0x4a8);
    if (e <= b || (e - b) % CAT_BYTES || (e - b) / CAT_BYTES > 7) return cursor;
    uint32_t n = (uint32_t)((e - b) / CAT_BYTES);
    if (!cats_logged) {   // once: the items as the game built them
        cats_logged = 1;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t it = b + (uint64_t)i * CAT_BYTES;
            rt_log("avatar hair: category item %u: id %#x count %u w8 %u kind %u wc %u b14 %u b32 %u", i, LD16(it), LD32(it + 4), LD16(it + 8), LD16(it + 0xa), LD16(it + 0xc), LD8(it + 0xe), LD8(it + 0x20));
        }
        rt_log("avatar hair: category vector begin %#llx end %#llx cap %#llx", (unsigned long long)b, (unsigned long long)e, (unsigned long long)cap);
    }
    if (LD16(e - CAT_BYTES + 0xa) == KIND_MODIFY) return cursor;
    // the template of the Modify item: the row whose picture it should show (modify_icon_kind: Style, later the Mustache or Beard row the player confirmed), else the Roots & Tips
    // Color item (a row with a colour swatch), else the first one; plain data, copied by value like the game's own vector code does. Only a swatch template has its count zeroed.
    uint32_t tpl = 0, want = modify_icon_kind(), found = 0;
    for (uint32_t i = 0; i < n && !found; i++) if (LD16(b + (uint64_t)i * CAT_BYTES + 0xa) == want) { tpl = i; found = 1; }
    for (uint32_t i = 0; i < n && !found; i++) if (LD16(b + (uint64_t)i * CAT_BYTES + 0xa) == 12) { tpl = i; found = 1; }
    int swatch = LD8(b + (uint64_t)tpl * CAT_BYTES + 0x20) != 0;
    if (n < 7) {   // room in the widget: the Modify row is one more row
        if (e + CAT_BYTES > cap) { rt_log("avatar hair: no room for the Modify item (%u items, capacity %llu)", n, (unsigned long long)((cap - b) / CAT_BYTES)); return cursor; }
        for (uint32_t i = 0; i < CAT_BYTES; i += 4) ST32(e + i, LD32(b + (uint64_t)tpl * CAT_BYTES + i));
        ST16(e + 0, MODIFY_MESSAGE); if (swatch) ST32(e + 4, 0); ST16(e + 0xa, KIND_MODIFY);
        ST64(w + 0x4a0, e + CAT_BYTES);
        rt_log("avatar hair: the Modify row was added to the category list (now %u rows)", n + 1);
        return cursor;
    }
    // 7 rows: the widget cannot hold an 8th (row slots, panes mc_List01 to 07 and the layouts lay01 to lay05 are all there is). The list becomes 8 rows of which 7 show.
    uint8_t all[8][CAT_BYTES];
    for (uint32_t i = 0; i < 7; i++) for (uint32_t k = 0; k < CAT_BYTES; k++) all[i][k] = LD8(b + (uint64_t)i * CAT_BYTES + k);
    memcpy(all[7], all[tpl], CAT_BYTES);
    all[7][0] = MODIFY_MESSAGE & 0xff; all[7][1] = MODIFY_MESSAGE >> 8;
    if (swatch) memset(all[7] + 4, 0, 4);
    all[7][0xa] = KIND_MODIFY; all[7][0xb] = 0;
    int off = virt_rebuild ? virt_off : 0;
    uint32_t cur = virt_rebuild ? virt_cursor : (uint32_t)cursor;
    virt_rebuild = 0;
    if (cur > 6) cur = 0;
    for (uint32_t i = 0; i < 7; i++) for (uint32_t k = 0; k < CAT_BYTES; k++) ST8(b + (uint64_t)i * CAT_BYTES + k, all[off + i][k]);
    virt_off = off; virt_active = 1;
    ST32(ui + 0x708, cur);
    rt_log("avatar hair: the category list has 8 rows, showing rows %d to %d, cursor on row %u", off + 1, off + 7, cur);
    return cur;
}

// Entry of the text setter that takes a text table and a message id (x0 the text pane, x1 the descriptor with the id in its low 16 bits at +8). The category
// list's widget calls it for every row's label (return address main+0x5ad6a0); for the Modify item, which carries MODIFY_MESSAGE, our own text is
// set through the game's raw-text setter (main+0x91720c), so the font, size and colour are the game's. XCX_HAIR_TRACE logs the calls.
#define F_SET_RAW_TEXT 0x700091720cull
int avatar_hair_text(Cpu *c) {
    if (!cur_ui || !hair_tab_active()) return 0;
    static int shown;
    if (cfg_get("XCX_HAIR_TRACE") && shown < 40) {
        shown++;
        rt_log("avatar hair: text setter x0 %#llx x1 %#llx lr %#llx id %#x", (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[30], LD16(c->x[1] + 8));
    }
    if (c->x[30] != 0x70005ad6a0ull || LD16(c->x[1] + 8) != MODIFY_MESSAGE) return 0;
    if (!modify_text_guest) {
        void *g = mem_kernel_alloc(sizeof modify_label + 8, 16);
        if (!g) return 0;
        memcpy(g, modify_label, sizeof modify_label);
        modify_text_guest = (uint64_t)(uintptr_t)g;
    }
    uint64_t saved[19], lr = c->x[30];
    memcpy(saved, c->x, sizeof saved);
    rt_call(c, F_SET_RAW_TEXT, 2, saved[0], modify_text_guest);
    memcpy(c->x, saved, sizeof saved);
    c->x[30] = lr;
    return 1;
}

static int modify_has_row(void) {   // the category list has a Modify row
    if (!cur_ui) return 0;
    uint64_t w = LD64(cur_ui + 0x2918);
    if (!w) return 0;
    uint64_t b = LD64(w + 0x498), e = LD64(w + 0x4a0);
    return e > b + CAT_BYTES - 1 && LD16(e - CAT_BYTES + 0xa) == KIND_MODIFY;
}
static int modify_cursor(void) {   // the category cursor is on the Modify row
    if (!hair_tab_active()) return 0;
    uint64_t w = LD64(cur_ui + 0x2918);
    if (!w) return 0;
    uint64_t b = LD64(w + 0x498), e = LD64(w + 0x4a0);
    uint32_t idx = LD32(cur_ui + 0x708);
    return e > b && (uint64_t)idx * CAT_BYTES + CAT_BYTES <= e - b && LD16(b + (uint64_t)idx * CAT_BYTES + 0xa) == KIND_MODIFY;
}

// The window's rows: every part the hair under the cursor has, each with its five values, in one list the player walks with Up and Down.
static volatile int mod_idx;   // the selected row of that list
static const double mod_lo[MP_N] = {-0.5, -0.15, -0.15, -0.15, -180.0};   // what the player's change of a row may reach (the scale is a change of the factor: x0.5 to x1.5; 15 cm each way; 180 degrees each way: the whole turn)
static const double mod_hi[MP_N] = {0.5, 0.15, 0.15, 0.15, 180.0};
static const double sl_lo[MP_N] = {0.5, -0.15, -0.15, -0.15, -180.0};   // the ends of the slider drawn for a row's total value
static const double sl_hi[MP_N] = {1.5, 0.15, 0.15, 0.15, 180.0};
static int mod_parts(int *vis) {
    int n = 0;
    for (int g = 0; g < MG_N; g++)
        if (g == MG_ALL || g == MG_CAP || !mod_seen || (mod_seen & (1u << g))) vis[n++] = g;
    return n;
}
static void mod_select(int idx) {
    int vis[MG_N], n = mod_parts(vis);
    idx = (idx % (n * MP_N) + n * MP_N) % (n * MP_N);
    mod_idx = idx;
    mod_grp = vis[idx / MP_N];
    mod_row = idx % MP_N;
}

static uint32_t last_style_row;   // the hairstyle of the grid's cursor the last time the Style category was open (the grid is empty while Modify is)
static uint32_t modify_row(void) {
    if (style_open()) {
        uint32_t idx = cur_grid ? LD32(cur_grid + 0xc) : 0;
        last_style_row = idx < grid_n ? grid_order[idx] : 0;
    }
    return last_style_row;
}

static int modify_via_minus;      // the window was opened with Minus (a face whose category list is full): it is independent of the category cursor
static int modify_wait_release;   // after a close the game still shows its grid focus for a moment: not a new A

static void modify_close(void) {
    mod_open = 0;
    modify_wait_release = 1;
    snd_req = 2;                    // B is ours while the window is open: the game's back sound
    input_block_view(0);
    input_block(0);
    mod_log("closed", modify_row());
    mod_flush();
    if (!modify_via_minus && grid_focus()) focus_req = 1;   // the game took A into its (empty) grid: the UI update hands the focus back to the category list (an injected B did not reach the game)
    modify_via_minus = 0;
}

// The window opens when the game takes A on the Modify row (its grid then has the focus, empty) or when A is pressed on the row; while it is open the
// game's input is blocked, except that the right stick still turns the character. Up and Down walk the rows, Left and Right change the value (held:
// repeating; ZL fine, ZR coarse), L and R jump to the previous or next part, Y sets the row back, X all rows, B closes.
static void modify_input(uint64_t rb, uint64_t md) {
    static int hold;
    static uint64_t dlg_ns;   // the last time the "Discard current edits" dialog was up
    if (dialog_up()) dlg_ns = plat_now_ns();
    int dlg_recent = dlg_ns && plat_now_ns() - dlg_ns < 300000000ull;   // the A that answers the dialog (Cancel, or Return to Base Selection) is the game's: it must not also open the window on the Modify row under it, even when the game closes the dialog before this thread sees the press
    if (!mod_open) {
        if (modify_wait_release) { if (!grid_focus()) modify_wait_release = 0; else return; }
        int minus = (md & MB_MINUS) && hair_tab_active() && (!modify_has_row() || virt_active) && !dlg_recent;   // when the Modify row is not on screen too
        if (minus || (modify_cursor() && (grid_focus() || ((md & B_A) && !dlg_recent)))) {
            modify_via_minus = minus;
            mod_open = 1; hold = 0;
            snd_check = tick_count + 4;   // the game plays its own sound when it takes the A press; if it has not by then, ours does
            input_block(1); input_block_view(1);
            mod_select(mod_idx);
            mod_log("opened", modify_row());
        }
        return;
    }
    if ((!modify_via_minus && !modify_cursor()) || !hair_tab_active()) { mod_open = 0; modify_via_minus = 0; input_block_view(0); input_block(0); return; }
    if (md & (MB_B | (modify_via_minus ? MB_MINUS : 0))) { modify_close(); return; }
    int changed = 0;
    if (md & (B_L | B_R)) {   // the first row of the previous or next part
        int vis[MG_N], n = mod_parts(vis), at = mod_idx / MP_N;
        mod_select(((md & B_R) ? (at + 1) % n : (at + n - 1) % n) * MP_N);
    }
    {   // Up and Down walk the rows, held: repeating like Left and Right
        static int vhold;
        int vd = ((rb & MB_DOWN) ? 1 : 0) - ((rb & MB_UP) ? 1 : 0);
        if (vd && (vhold == 0 || (vhold > 18 && (vhold - 18) % 3 == 0))) mod_select(mod_idx + vd);
        vhold = vd ? vhold + 1 : 0;
    }
    if ((md & B_Y_BIT) && mod_v[mod_grp][mod_row] != 0.0) { mod_v[mod_grp][mod_row] = 0.0; changed = 1; }   // a reset of what is already at its start changes nothing (no new round of decisions for the bones)
    if (md & MB_X) { for (size_t q = 0; q < sizeof mod_v / sizeof(double); q++) if (((double *)mod_v)[q] != 0.0) changed = 1; memset(mod_v, 0, sizeof mod_v); }
    int dir = ((rb & B_RIGHT) ? 1 : 0) - ((rb & B_LEFT) ? 1 : 0);
    if (dir && (hold == 0 || (hold > 18 && (hold - 18) % 4 == 0))) {
        static const double step[MP_N] = {0.01, 0.002, 0.002, 0.002, 1.0};
        double k = ((rb & MB_ZL) ? 0.2 : (rb & MB_ZR) ? 5.0 : 1.0) * (hold > 100 ? 4.0 : hold > 50 ? 2.0 : 1.0), v = mod_v[mod_grp][mod_row] + dir * step[mod_row] * k;
        v = (double)(long)(v * 10000.0 + (v < 0 ? -0.5 : 0.5)) / 10000.0;
        if (v < mod_lo[mod_row]) v = mod_lo[mod_row];
        if (v > mod_hi[mod_row]) v = mod_hi[mod_row];
        if (v != mod_v[mod_grp][mod_row]) snd_req = 3;   // the slider's sound, only for a step that moved it
        mod_v[mod_grp][mod_row] = v;
        changed = 1;
    }
    hold = dir ? hold + 1 : 0;
    if (changed) { modify_any_update(); ib_gen++; mod_dirty = 1; mod_log("changed", modify_row()); }
}

static void driver_fn(void *arg) {
    (void)arg;
    if (cfg_get("XCX_HAIR_REVIEW")) { review_run(); return; }
    uint64_t prev = 0, mprev = 0;
    for (;;) {
        plat_sleep_ms(16);
        {
            uint64_t rb; int32_t lx, ly, rx, ry;
            input_poll_ui(&rb, &lx, &ly, &rx, &ry);
            // the left stick (WASD on the keyboard) moves the creator's lists and sliders as the d-pad does (keys 1 to 4 on the keyboard): both steer the Modify window
            if (lx < -16000) rb |= B_LEFT;
            if (lx > 16000) rb |= B_RIGHT;
            if (ly > 16000) rb |= MB_UP;
            if (ly < -16000) rb |= MB_DOWN;
            uint64_t md = rb & ~mprev;
            mprev = rb;
            if (dialog_hide) { if ((md & (B_A | MB_B)) || !hair_tab_active()) dialog_hide = 0; }
            else if ((md & MB_B) && !mod_open && !modify_wait_release && hair_tab_active() && !grid_focus() && cur_face <= 13) dialog_hide = 1;
            if (md & MB_DOWN) dn_edge = tick_count + 1;
            if (md & MB_UP) up_edge = tick_count + 1;
            if (modify_on() && cur_ui && cur_face <= 13) modify_input(rb, md);
            {   // another hairstyle under the cursor: its parts are known again once its bones are decided
                uint32_t idx = cur_grid ? LD32(cur_grid + 0xc) : 0;
                uint32_t key = (idx < grid_n ? grid_order[idx] : 0) * 2u + (cur_face >= 7 ? 1u : 0u);   // the hairstyle under the cursor (its row) and the wearer's gender
                if (style_open() && key != mod_last_key) { mod_seen = 0; mod_switch(key); sw_gen++; mod_last_key = key; }   // a hairstyle already loaded is decided again, so its parts are known again; the one that was left keeps its values while it fades out   // not while Modify is open: its grid is empty, the hairstyle stays the same
            }
            if (mod_open) { prev = input_last_buttons(); continue; }
        }
        uint64_t b = input_last_buttons(), down = b & ~prev;
        prev = b;
        if (custom || !(down & B_Y_BIT) || !style_open()) continue;
        int in_grid = grid_focus();
        page = (page + 1) % 3;
        rt_log("avatar hair: page %d", page);
        page_apply();
        if (refresh_in_place() && in_grid) regrid();   // the entry routine leaves the focus on the categories: back into the grid
        prev = input_last_buttons();
    }
}


// The page hint (drawn by the host overlay, runtime/ui/menu.cpp, hint_draw): 1 while the Style grid of the hair tab is open and paging is on.
// page 0 is the creator's gender, 1 the other gender, 2 the story characters; source and family are what the glyph system detects
// (GLYPH_SOURCE_*, GLYPH_PAD_*); symbol is the glyph symbol of the page button (SYM_X or SYM_Y), which the glyph system draws for the style in use.
int avatar_hair_hint(int *page_out, int *male, int *source, int *family, int *symbol) {
    if (!driver_on || custom || !cur_ui || !style_open() || cur_face > 13 || dialog_up()) return 0;
    InputMap m;
    int layout;
    input_glyph_snapshot(source, family, &layout, &m);
    *page_out = page;
    *male = cur_face < 7;
    *symbol = page_symbol();
    return 1;
}

// XCX_HAIR_PAGETEST=<name>: once the Style grid has the focus, presses the page button XCX_HAIR_PAGETEST_N times (default 4), 6 seconds apart,
// and saves a shot of the model after each (name_<k>); the host overlay's own shots come from XCX_HAIR_HINT_SHOT.
static void pagetest_fn(void *arg) {
    (void)arg;
    const char *name = cfg_get("XCX_HAIR_PAGETEST");
    int n = cfg_get("XCX_HAIR_PAGETEST_N") ? atoi(cfg_get("XCX_HAIR_PAGETEST_N")) : 4;
    for (int t = 0; t < 600 && !(hair_tab_active() && grid_focus() && grid_n); t++) plat_sleep_ms(250);
    if (!(hair_tab_active() && grid_focus() && grid_n)) { rt_log("avatar hair pagetest: the style grid never got the focus"); return; }
    plat_sleep_ms(4000);
    for (int k = 0; k <= n; k++) {
        char shot[160];
        snprintf(shot, sizeof shot, "%s_%d", name, k);
        boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f);
        for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
        rt_log("avatar hair pagetest: shot %d, page %d, grid focus %u, cursor %u, tab %u", k, page, (unsigned)grid_focus(), LD32(cur_grid + 0xc), (unsigned)LD16(cur_ui + 0x5fc));
        if (k == n) break;
        input_inject(B_Y_BIT, 4);
        plat_sleep_ms(6000);
        if (cfg_get("XCX_HAIR_PAGETEST_PICK")) {   // pick a hairstyle on the new page: into the grid, two to the right, A
            if (!grid_focus()) { input_inject(B_A, 4); plat_sleep_ms(2500); }
            for (int m = 0; m < 2; m++) { input_inject(B_RIGHT, 4); plat_sleep_ms(1500); }
            input_inject(B_A, 4);
            plat_sleep_ms(5000);
            snprintf(shot, sizeof shot, "%s_pick%d", name, k);
            boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
            {
                uint64_t gb = LD64(cur_grid + 0x850), ge = LD64(cur_grid + 0x858);
                uint32_t idx = LD32(cur_grid + 0xc), cnt = ge > gb ? (uint32_t)((ge - gb) / ITEM_BYTES) : 0;
                char rows[200]; int o = 0;
                for (uint32_t q = 0; q < cnt && q < 14; q++) o += snprintf(rows + o, sizeof rows - (size_t)o, "%u ", LD32(gb + ITEM_BYTES * (uint64_t)q + 4));
                rt_log("avatar hair pagetest: picked on page %d, grid focus %u, cursor %u (entry row %u of %u entries), ui+0x70c %u, first rows: %s", page, (unsigned)grid_focus(), idx,
                       idx < cnt ? LD32(gb + ITEM_BYTES * (uint64_t)idx + 4) : 0, cnt, LD32(cur_ui + 0x70c), rows);
            }
        }
    }
    rt_log("avatar hair pagetest: done");
    plat_sleep_ms(1500);
    _exit(0);
}

// XCX_HAIR_MODNAV=<name>: once the Style grid has the focus, walks to the Modify row the way a player does (B to the category list, Down to the last row),
// presses A, moves in the window and changes values, and saves a shot of each step (name_<step>, with the host overlay: XCX_HAIR_HINT_SHOT); then exits.
static char shot_tag[64];   // a shot of the host overlay is wanted under this name (the overlay draws it, avatar_hair_shot_tag hands the name over)
const char *avatar_hair_shot_tag(void) {
    static char out[64];
    if (!shot_tag[0]) return NULL;
    snprintf(out, sizeof out, "%s", shot_tag);
    shot_tag[0] = 0;
    return out;
}
static void modnav_shot(const char *name, const char *step) {
    char shot[160];
    snprintf(shot_tag, sizeof shot_tag, "%s_%s_overlay", name, step);
    for (int t = 0; t < 60 && shot_tag[0]; t++) plat_sleep_ms(100);
    snprintf(shot, sizeof shot, "%s_%s", name, step);
    boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f);
    for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
    rt_log("avatar hair modnav: shot %s (tab %u, category %u, grid focus %u, inside %d, row %d)", step, (unsigned)LD16(cur_ui + 0x5fc), LD32(cur_ui + 0x708), (unsigned)grid_focus(), (int)mod_open, (int)mod_idx);
}
static void modnav_fn(void *arg) {
    (void)arg;
    const char *name = cfg_get("XCX_HAIR_MODNAV");
    for (int t = 0; t < 600 && !(hair_tab_active() && grid_focus() && grid_n); t++) plat_sleep_ms(250);
    if (!(hair_tab_active() && grid_focus() && grid_n)) { rt_log("avatar hair modnav: the style grid never got the focus"); return; }
    plat_sleep_ms(4000);
    modnav_shot(name, "0style");
    input_inject(MB_B, 4); plat_sleep_ms(2000);
    uint64_t w = LD64(cur_ui + 0x2918);
    uint32_t rows = (uint32_t)((LD64(w + 0x4a0) - LD64(w + 0x498)) / CAT_BYTES);
    rt_log("avatar hair modnav: %u category rows", rows);
    if (modify_has_row()) {
        for (uint32_t i = 0; i + 1 < rows; i++) { input_inject(MB_DOWN, 4); plat_sleep_ms(1500); }
        modnav_shot(name, "1cursor");
        if (cfg_get("XCX_HAIR_MODNAV_MASH")) {   // B pressed over and over on the Modify row (XCX_HAIR_MODNAV_MASH=<presses>, XCX_HAIR_MODNAV_GAP=<ms between>), the state logged after each
            int n = atoi(cfg_get("XCX_HAIR_MODNAV_MASH")), gap = cfg_get("XCX_HAIR_MODNAV_GAP") ? atoi(cfg_get("XCX_HAIR_MODNAV_GAP")) : 150;
            for (int k = 0; k < n; k++) {
                input_inject(MB_B, 3);
                plat_sleep_ms(gap);
                rt_log("avatar hair mash: press %d: category %u, grid focus %u, dialog %d, inside %d, state %u", k + 1, LD32(cur_ui + 0x708), (unsigned)grid_focus(), (int)dialog_hide, (int)mod_open, LD32(cur_ui + 0x68));
                if (k % 3 == 2) { char st[16]; snprintf(st, sizeof st, "mash%02d", k + 1); modnav_shot(name, st); }
            }
            plat_sleep_ms(2500);
            modnav_shot(name, "1mash_end");
        }
        input_inject(B_A, 4); plat_sleep_ms(3000);
    } else {   // a full category list: the window opens with Minus
        modnav_shot(name, "1cursor");
        input_inject(MB_MINUS, 4); plat_sleep_ms(3000);
    }
    modnav_shot(name, "2inside");
    for (int k = 0; k < 3; k++) { input_inject(MB_DOWN, 4); plat_sleep_ms(900); }
    for (int k = 0; k < 4; k++) { input_inject(B_RIGHT, 4); plat_sleep_ms(900); }
    modnav_shot(name, "3changed");
    for (int k = 0; k < 9; k++) { input_inject(MB_DOWN, 4); plat_sleep_ms(500); }
    modnav_shot(name, "4scrolled");
    input_inject(MB_B, 4); plat_sleep_ms(2500);
    modnav_shot(name, "5back");
    rt_log("avatar hair modnav: done");
    plat_sleep_ms(1500);
    _exit(0);
}

// XCX_HAIR_MODVIS=<name>: once the Style grid has the focus, sets one Modify value after the other (no input needed) and photographs the hair after each, front and
// from the side (the right stick turns the model), so what each part's sliders do can be looked at. The shots are name_<step>_<view>.
static void modvis_fn(void *arg) {
    (void)arg;
    const char *name = cfg_get("XCX_HAIR_MODVIS");
    for (int t = 0; t < 600 && !(hair_tab_active() && grid_focus() && grid_n); t++) plat_sleep_ms(250);
    if (!(hair_tab_active() && grid_focus() && grid_n)) { rt_log("avatar hair modvis: the style grid never got the focus"); return; }
    plat_sleep_ms(5000);
    if (cfg_get("XCX_HAIR_PAGEVIS")) {   // the pages: a full-window shot of each, then a hairstyle confirmed on page 1 and the pages turned again (where does the check mark stay?)
        static const char *what[] = {"p0_start", "p1", "p2", "p0_back", "p1_again"};
        for (int k = 0; k < 5; k++) {
            char shot[160];
            snprintf(shot, sizeof shot, "%s_%d_%s", name, k, what[k]);
            boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
            rt_log("avatar hair pagevis: %s (page %d)", what[k], page);
            if (k < 4) { input_inject(B_Y_BIT, 4); plat_sleep_ms(4500); }
        }
        input_inject(B_RIGHT, 4); plat_sleep_ms(1500);   // on page 1: the second hairstyle, confirmed
        input_inject(B_A, 4); plat_sleep_ms(3500);
        static const char *what2[] = {"c_p1_confirmed", "c_p2", "c_p0", "c_p1_return"};
        for (int k = 0; k < 4; k++) {
            char shot[160];
            snprintf(shot, sizeof shot, "%s_%d_%s", name, 5 + k, what2[k]);
            boot_shot_request(shot, 0.0f, 0.0f, 1.0f, 1.0f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
            rt_log("avatar hair pagevis: %s (page %d)", what2[k], page);
            if (k < 3) { input_inject(B_Y_BIT, 4); plat_sleep_ms(4500); }
        }
        rt_log("avatar hair modvis: done");
        plat_sleep_ms(1000);
        _exit(0);
    }
    static const struct { const char *tag; int g, k; double v; } steps[] = {
        {"base", 0, 0, 0}, {"all_scale", MG_ALL, MP_SCALE, 0.4}, {"all_tilt", MG_ALL, MP_TILT, 30}, {"all_up", MG_ALL, MP_Y, 0.06}, {"all_back", MG_ALL, MP_Z, 0.06},
        {"all_side", MG_ALL, MP_X, 0.06}, {"cap_scale", MG_CAP, MP_SCALE, 0.4}, {"front_scale", MG_FRONT, MP_SCALE, 0.4}, {"front_up", MG_FRONT, MP_Y, 0.06},
        {"sides_scale", MG_SIDE, MP_SCALE, 0.4}, {"tails_scale", MG_TAIL, MP_SCALE, 0.4}, {"rest_scale", MG_REST, MP_SCALE, 0.4}};
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        memset(mod_v, 0, sizeof mod_v);
        mod_v[steps[i].g][steps[i].k] = steps[i].v;
        modify_any_update(); ib_gen++; mod_seen = 0;
        plat_sleep_ms(3000);
        for (int view = 0; view < 2; view++) {
            if (view) { input_inject_rstick(32767, 0, 30); plat_sleep_ms(1800); }
            char shot[160];
            snprintf(shot, sizeof shot, "%s_%02u_%s_v%d", name, i, steps[i].tag, view);
            boot_shot_request(shot, 0.50f, 0.08f, 0.85f, 0.60f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
        }
        input_inject_rstick(-32767, 0, 30); plat_sleep_ms(1800);   // back to the front
        rt_log("avatar hair modvis: step %s done, parts seen %#x", steps[i].tag, (unsigned)mod_seen);
    }
    if (cfg_get("XCX_HAIR_MODVIS_SPECIAL")) {   // the Special category: its two rows previewed, with the All change on
        memset(mod_v, 0, sizeof mod_v); mod_v[MG_ALL][MP_SCALE] = 0.3; modify_any_update(); ib_gen++;
        input_inject(MB_B, 4); plat_sleep_ms(2000);
        input_inject(MB_DOWN, 4); plat_sleep_ms(1500);
        input_inject(B_A, 4); plat_sleep_ms(3500);
        for (int k = 0; k < 2; k++) {
            char shot[160];
            snprintf(shot, sizeof shot, "%s_special_%d", name, k);
            boot_shot_request(shot, 0.50f, 0.08f, 0.85f, 0.60f);
            for (int t = 0; t < 40 && boot_shot_pending(); t++) plat_sleep_ms(100);
            input_inject(B_RIGHT, 4); plat_sleep_ms(3500);
        }
    }
    rt_log("avatar hair modvis: done");
    plat_sleep_ms(1000);
    _exit(0);
}

static void driver_start(void) {
    static int started;
    const char *pg = cfg_get("XCX_HAIR_PAGING");
    if (started || (pg && !strcmp(pg, "0")) || (custom && !cfg_get("XCX_HAIR_REVIEW"))) return;
    started = 1;
    driver_on = !cfg_get("XCX_HAIR_REVIEW");
    plat_thread_spawn(driver_fn, NULL, "avatar hair pages", 1 << 20);
    if (cfg_get("XCX_HAIR_PAGETEST")) plat_thread_spawn(pagetest_fn, NULL, "avatar hair test", 1 << 20);
    if (cfg_get("XCX_HAIR_MODVIS")) plat_thread_spawn(modvis_fn, NULL, "avatar hair modvis", 1 << 20);
    if (cfg_get("XCX_HAIR_MODNAV")) plat_thread_spawn(modnav_fn, NULL, "avatar hair modnav", 1 << 20);
}

// ---- test helpers (XCX_HAIR_WATCH=1): what changes in the creator's UI object and its grid, to find the cursor and the active tab
static void watch_fn(void *arg) {
    (void)arg;
    static uint32_t pu[0x5000 / 4], pg[0x1000 / 4];
    int first = 1;
    for (;;) {
        plat_sleep_ms(250);
        uint64_t ui = cur_ui, grid = cur_grid;
        if (!ui || !grid) continue;
        int lines = 0;
        for (uint32_t i = 0; i < 0x5000 / 4; i++) {
            uint32_t v = LD32(ui + 4ull * i);
            if (!first && v != pu[i] && lines++ < 30) rt_log("avatar hair watch: ui+%x %x -> %x", 4 * i, pu[i], v);
            pu[i] = v;
        }
        for (uint32_t i = 0; i < 0x1000 / 4; i++) {
            uint32_t v = LD32(grid + 4ull * i);
            if (!first && v != pg[i] && lines++ < 60) rt_log("avatar hair watch: grid+%x %x -> %x", 4 * i, pg[i], v);
            pg[i] = v;
        }
        first = 0;
    }
}

static void watch_start(void) {
    static int started;
    if (started || !cfg_get("XCX_HAIR_WATCH")) return;
    started = 1;
    plat_thread_spawn(watch_fn, NULL, "avatar hair watch", 1 << 20);
}

// ---- fit of a part to the wearer's skeleton ----------------------------------------------------------------------------------------------------
// A part (hair) is skinned as pose(wearer bone) * inverseBind(part). The inverse bind comes from the part's own model file, made for the
// skeleton of one gender, so a hair of the other gender sits where that skeleton's hair bones are, not where the wearer's are. The per-frame pose
// feed (main+0x3177a0) hands each skin bone's pose matrix to the part (stack, sp+0x80, four columns of four floats); the hook after its last
// store multiplies the pose by F = inverseBind(wearer's own bind pose) * inverse(inverseBind(part)), so the palette becomes
// pose * inverseBind(wearer). F is the identity when the part was made for the wearer's skeleton.
// XCX_HAIR_FIT=log writes what the hook sees, XCX_HAIR_FIT=1 applies it.
typedef struct { double m[4][4]; } M4;

static float gf(uint64_t a) { uint32_t v = LD32(a); float f; memcpy(&f, &v, 4); return f; }
static void sf(uint64_t a, float f) { uint32_t v; memcpy(&v, &f, 4); ST32(a, v); }
static M4 m4_from_mem(uint64_t a) {   // four columns of four floats, translation in the last column
    M4 r;
    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) r.m[i][j] = gf(a + 16 * j + 4 * i);
    return r;
}
static M4 m4_mul(M4 a, M4 b) {
    M4 r;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j]; r.m[i][j] = s; }
    return r;
}
static M4 m4_inv(M4 a) {   // affine inverse
    double (*m)[4] = a.m;
    double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1], c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2], c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    M4 r = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    if (det > -1e-12 && det < 1e-12) return r;
    double d = 1.0 / det;
    r.m[0][0] = c00 * d; r.m[1][0] = c01 * d; r.m[2][0] = c02 * d;
    r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * d; r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * d; r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * d;
    r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * d; r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * d; r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * d;
    for (int i = 0; i < 3; i++) r.m[i][3] = -(r.m[i][0] * m[0][3] + r.m[i][1] * m[1][3] + r.m[i][2] * m[2][3]);
    return r;
}

typedef struct { uint64_t rig; int n; M4 *w; uint32_t *h; double head_y; } RigW;
static RigW rigs[16];
static int n_rigs;

// Bind-pose world matrices of a skeleton file (header: BC, bone count at +0x58, parent table offset at +0x50, names at +0x60, local transforms at +0x70 (0x120 or 0x130
// by file) as position, quaternion, scale in 48 bytes). Cached by address and checked by size, so a reused address is read again.
static RigW *rig_world(uint64_t rig) {
    int n = (int)LD32(rig + 0x58);
    if (n <= 0 || n > 400) return NULL;
    for (int k = 0; k < n_rigs; k++) if (rigs[k].rig == rig && rigs[k].n == n) return &rigs[k];
    RigW *r = n_rigs < 16 ? &rigs[n_rigs++] : &rigs[15];
    free(r->w); free(r->h);
    r->w = calloc((size_t)n, sizeof(M4));
    r->h = calloc((size_t)n, sizeof(uint32_t));
    r->head_y = 0;
    r->rig = rig; r->n = n;
    uint32_t poff = LD32(rig + 0x50);
    int8_t *done = calloc((size_t)n, 1);
    for (int pass = 0; pass < n; pass++) {
        int left = 0;
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            int p = (int16_t)LD16(rig + poff + 2 * (uint64_t)i);
            if (p >= n || (p >= 0 && !done[p])) { left++; continue; }
            uint64_t l = rig + LD32(rig + 0x70) + 48 * (uint64_t)i;
            double q[4] = {gf(l + 16), gf(l + 20), gf(l + 24), gf(l + 28)}, s[3] = {gf(l + 32), gf(l + 36), gf(l + 40)};
            double x = q[0], y = q[1], z = q[2], w = q[3];
            M4 m = {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), gf(l)},
                     {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), gf(l + 4)},
                     {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), gf(l + 8)}, {0, 0, 0, 1}}};
            for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) m.m[a][b] *= s[b];
            r->w[i] = p < 0 ? m : m4_mul(r->w[p], m);
            done[i] = 1;
        }
        if (!left) break;
    }
    free(done);
    uint32_t noff = LD32(rig + 0x60);   // names: 16-byte entries whose first word is the file offset of the string
    for (int i = 0; i < n; i++) {
        uint32_t so = LD32(rig + noff + 16 * (uint64_t)i), hh = 2166136261u;
        char nm[24] = {0};
        for (int k = 0; k < 23 && so < 0x100000; k++) { uint8_t ch = (uint8_t)LD8(rig + so + (uint64_t)k); if (!ch) break; nm[k] = (char)ch; hh = (hh ^ ch) * 16777619u; }
        r->h[i] = hh;
        if (!strcmp(nm, "JU_head")) r->head_y = r->w[i].m[1][3];
    }
    return r;
}

static uint32_t fnv_name(uint64_t base, uint32_t off) {
    uint32_t hh = 2166136261u;
    for (int k = 0; k < 23 && off < 0x100000; k++) { uint8_t ch = (uint8_t)LD8(base + off + (uint64_t)k); if (!ch) break; hh = (hh ^ ch) * 16777619u; }
    return hh;
}

static uint32_t fnv_name_of(const char *s) {
    uint32_t hh = 2166136261u;
    for (; *s; s++) hh = (hh ^ (uint8_t)*s) * 16777619u;
    return hh;
}

static RigW *sex_rw[2];   // the latest skeleton seen for a male (head joint at 1.608 m) and a female wearer (1.643 m)
static RigW sex_own[2][6];   // their own copies (rig_world's cache is reused by every skeleton the game loads, a long session overwrote the wearer's)
static int sex_next[2];

// Keep a private copy of the creator's male or female skeleton; a copy is made only when its content differs from the one in use, and copies
// are never freed because the palette builder may be reading one on another thread.
static void keep_sex_rig(int k, RigW *rw) {
    RigW *cur = sex_rw[k];
    if (cur && cur->n == rw->n && !memcmp(cur->w, rw->w, (size_t)rw->n * sizeof(M4)) && !memcmp(cur->h, rw->h, (size_t)rw->n * sizeof(uint32_t))) return;
    if (sex_next[k] >= 6) return;   // a session that keeps loading different skeletons of this size keeps the last copy
    RigW *c = &sex_own[k][sex_next[k]++];
    c->rig = rw->rig; c->n = rw->n; c->head_y = rw->head_y;
    c->w = malloc((size_t)rw->n * sizeof(M4)); c->h = malloc((size_t)rw->n * sizeof(uint32_t));
    memcpy(c->w, rw->w, (size_t)rw->n * sizeof(M4)); memcpy(c->h, rw->h, (size_t)rw->n * sizeof(uint32_t));
    __atomic_store_n(&sex_rw[k], c, __ATOMIC_RELEASE);
    rt_log("avatar fit: %s wearer skeleton kept (%d bones, head joint %.3f m, copy %d)", k ? "female" : "male", c->n, c->head_y, sex_next[k]);
}

void avatar_hair_pose(Cpu *c, uint64_t sp, uint64_t slot, uint64_t wrap, uint64_t arg) {
    static int mode = -1;   // 0 off, 1 apply, 2 log
    static int logged;
    if (mode < 0) {
        const char *v = cfg_get("XCX_HAIR_FIT");
        mode = !v ? 1 : !strcmp(v, "0") ? 0 : !strcmp(v, "log") ? 2 : !strcmp(v, "lift") ? 3 : 1;   // on unless XCX_HAIR_FIT=0
    }
    if (!mode) return;
    uint64_t self = LD64(wrap), chr = c->x[19];
    uint64_t data = self ? LD64(self + 0x40) : 0;
    if (mode == 2) {
        static int calls;
        calls++;
        if (calls <= 3)
            rt_log("avatar fit: call %d wrap %llx [wrap] %llx [wrap+8] %llx data %llx magic %08x x19 %llx x20 %llu slot %llx [slot+8] %llx", calls,
                   (unsigned long long)wrap, (unsigned long long)self, (unsigned long long)LD64(wrap + 8), (unsigned long long)data, data ? LD32(data) : 0,
                   (unsigned long long)chr, (unsigned long long)c->x[20], (unsigned long long)slot, (unsigned long long)LD64(slot + 8));
    }
    if (!data || LD32(data) != 0x4d584d44) { if (mode == 2 && logged++ < 40) rt_log("avatar fit: no model at %llx (x0 %llx)", (unsigned long long)self, (unsigned long long)wrap); return; }
    uint32_t i = (uint32_t)c->x[20];
    uint64_t tbl = LD64(self + 0x130), map = LD64(self + 0x70), idx = LD64(slot + 8);
    static int why[8];
    if (!tbl || !map || !idx) { if (mode == 2 && why[0]++ < 20) rt_log("avatar fit: early exit, model %llx data %llx bone %u tbl %llx map %llx idx %llx", (unsigned long long)self, (unsigned long long)data, i, (unsigned long long)tbl, (unsigned long long)map, (unsigned long long)idx); return; }
    uint64_t elem = tbl + (uint64_t)LD16(map + 2 * (uint64_t)i) * 0xb8;
    int w = (int16_t)LD16(idx + 2 * (uint64_t)i);
    if (mode == 3) {   // test: lift every bone of every part of the male wearer (rig of 149 bones) by 30 cm
        uint64_t rg = LD64(LD64(chr + 0x28) + 8);
        static int lifted;
        if (rg && LD16(rg) == 0x4342 && LD32(rg + 0x58) == 149) {
            M4 p = m4_from_mem(sp + 0x80);
            p.m[1][3] += 0.3;
            for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) sf(sp + 0x80 + 16 * (uint64_t)j + 4 * (uint64_t)k, (float)p.m[k][j]);
            if (lifted++ < 3 || (lifted & 0xfff) == 0) rt_log("avatar fit: lifted bone %u of model %llx (chr %llx), %d so far", i, (unsigned long long)self, (unsigned long long)chr, lifted);
        }
        return;
    }
    uint64_t cand[3] = {LD64(chr + 0x300), LD64(chr + 0x2f8), LD64(LD64(chr + 0x28) + 8)}, rig = 0;
    int which = -1;
    for (int k = 0; k < 3; k++) if (cand[k] && LD16(cand[k]) == 0x4342) { rig = cand[k]; which = k; break; }
    M4 ib = m4_from_mem(elem + 0x38);
    if (!rig) {
        if (mode == 2 && logged++ < 8)
            rt_log("avatar fit: no skeleton file for %llx (candidates %llx %llx %llx, first words %08x %08x %08x)", (unsigned long long)chr,
                   (unsigned long long)cand[0], (unsigned long long)cand[1], (unsigned long long)cand[2],
                   cand[0] ? LD32(cand[0]) : 0, cand[1] ? LD32(cand[1]) : 0, cand[2] ? LD32(cand[2]) : 0);
        return;
    }
    if (mode == 2) {
        static uint64_t dumped[8]; static int nd;
        int have = 0;
        for (int k = 0; k < nd; k++) if (dumped[k] == rig) have = 1;
        if (!have && nd < 8 && LD32(rig + 0x58) == 153) {
            dumped[nd++] = rig;
            char b[400];
            for (int blk = 0; blk < 3; blk++) {
                uint64_t base = blk == 0 ? rig : blk == 1 ? rig + 0x100 : rig + 0x120 + 48 * 32;   // header, before the locals, JU_head's entry
                int o = 0;
                for (int k = 0; k < 24; k++) o += snprintf(b + o, sizeof b - (size_t)o, "%08x ", LD32(base + 4 * (uint64_t)k));
                rt_log("avatar fit: rig %llx +%x: %s", (unsigned long long)rig, (unsigned)(base - rig), b);
            }
            uint64_t base = rig + 0x120 + 48 * 32; char b2[300]; int o2 = 0;
            for (int k = 0; k < 12; k++) o2 += snprintf(b2 + o2, sizeof b2 - (size_t)o2, "%.4f ", gf(base + 4 * (uint64_t)k));
            rt_log("avatar fit: JU_head local as floats: %s", b2);
        }
    }
    RigW *rw = rig_world(rig);
    // the creator's male skeleton has its head joint at 1.608 m and the female at 1.643 m; other skeletons of 150 bones (a creature's, a
    // story character's) must not take their place
    if (rw && rw->n > 100) {
        if (rw->head_y > 1.595 && rw->head_y < 1.62) keep_sex_rig(0, rw);
        else if (rw->head_y > 1.635 && rw->head_y < 1.655) keep_sex_rig(1, rw);
    }
    if (!rw || w >= rw->n) { if (mode == 2 && why[2]++ < 20) rt_log("avatar fit: bone %d out of the skeleton (%d bones)", w, rw ? rw->n : -1); return; }
    M4 iw = m4_inv(rw->w[w]), hb = m4_inv(ib);
    M4 f = m4_mul(iw, hb);
    double dev = 0;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 4; b++) { double d = f.m[a][b] - (a == b); if (d < 0) d = -d; if (d > dev) dev = d; }
    if (mode == 2 && rw->n == 149) {
        static uint64_t dk[200]; static int dn, per[200];
        char pn[40] = "?";
        uint32_t po = LD32(elem);
        for (int k = 0; k < 39 && po < 0x4000; k++) { uint8_t ch = (uint8_t)LD8(tbl + po + (uint64_t)k); pn[k] = (char)ch; pn[k + 1] = 0; if (!ch) break; }
        int e = -1;
        for (int k = 0; k < dn; k++) if (dk[k] == data) e = k;
        if (e < 0 && dn < 200) { dk[dn] = data; e = dn++; }
        if (e >= 0 && per[e] < 6 && !strncmp(pn, "DJ_hair", 7)) {
            per[e]++;
            rt_log("avatar fit 149: self %llx data %llx (key %d) bone %u %s part y %.4f wearer y %.4f dev %.4f", (unsigned long long)self, (unsigned long long)data, e, i, pn, hb.m[1][3], rw->w[w].m[1][3], dev);
        }
    }
    if (mode == 2) {
        static uint64_t seen[2048]; static int nseen; static int total;
        int slot_i = -1;
        uint64_t key = self ^ (data << 1);
        for (int k = 0; k < nseen; k++) if (seen[k] == key) slot_i = k;
        if (slot_i < 0 && nseen < 2048) { seen[nseen] = key; slot_i = nseen++; }
        static int lines[2048];
        if (slot_i >= 0 && total < 1500 && lines[slot_i] < 60 && (lines[slot_i] < 2 || dev > 0.003)) {
            lines[slot_i]++; total++;
            char pn[40] = "?", wn[40] = "?";
            uint32_t po = LD32(elem), nb = LD32(rig + 0x58), pof = LD32(rig + 0x60) + 16 * LD32(rig + 0x58);
            for (int k = 0; k < 39 && po < 0x4000; k++) { uint8_t ch = (uint8_t)LD8(tbl + po + (uint64_t)k); pn[k] = (char)ch; pn[k + 1] = 0; if (!ch) break; }
            uint32_t wo = LD32(rig + pof - 16 * (uint64_t)nb + 16 * (uint64_t)w);
            for (int k = 0; k < 39 && wo < 0x40000; k++) { uint8_t ch = (uint8_t)LD8(rig + wo + (uint64_t)k); wn[k] = (char)ch; wn[k + 1] = 0; if (!ch) break; }
            rt_log("avatar fit: arg-sp %lld chr %llx model %llx(#%d) bone %u '%s' -> wearer %d '%s' (%d bones) part y %.4f wearer y %.4f dev %.4f", (long long)arg - (long long)sp, (unsigned long long)chr,
                   (unsigned long long)self, slot_i, i, pn, w, wn, rw->n, hb.m[1][3], rw->w[w].m[1][3], dev);
        }
        return;
    }
    (void)dev;   // XCX_HAIR_FIT=1 only keeps the wearers' skeletons for avatar_hair_ibind
}

// The palette builder (main+0x9bc940) multiplies a skin bone's pose by the model's inverse bind matrix, loaded as four 16-byte vectors from the
// bone element (v2, v3, v4, v6 after the loads at main+0x9bca10..0x9bca1c). Hook after the last load: while the creator is open, a matrix that
// differs from the one of the wearer's own skeleton (same bone name, the creator face's gender) is replaced by the wearer's, so a hair made for
// the other gender (or another head) is placed as if it were made for this one. Per bone the answer is cached by element address and checked
// against the file's matrix, so a reused address is decided again.
typedef struct { uint64_t elem; RigW *rw; float orig[16]; float fix[16]; uint8_t state; uint32_t gen; uint32_t key, sgen; } IbCache;   // state 1 keep, 2 replace
static IbCache ibc[8192];

// A transform of the model about a pivot: scale, then a tilt about the left/right axis (degrees), then a shift (metres), as the matrix of an affine map.
static M4 m4_about(double s, double tilt_deg, const double pv[3], double dx, double dy, double dz) {
    double th = tilt_deg * 3.14159265358979 / 180.0, cs = cos(th), sn = sin(th);
    double R[3][3] = {{1, 0, 0}, {0, cs, -sn}, {0, sn, cs}};
    M4 T = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) T.m[a][b] = s * R[a][b];
    double d3[3] = {dx, dy, dz};
    for (int a = 0; a < 3; a++) T.m[a][3] = pv[a] - (T.m[a][0] * pv[0] + T.m[a][1] * pv[1] + T.m[a][2] * pv[2]) + d3[a];
    return T;
}
#define HEAD_CENTER_UP 0.10   // the middle of the skull lies about this far above the head joint (the joint sits at the base of the head)

// The root of a strand part in a hair model: the highest of the bind positions of its bones (a strand hangs down from where it is rooted). The part of a bone is decided
// as in avatar_hair_ibind: the head joint is the scalp cap, DJ_hair + F, S, T the front, sides and tails, any other DJ_hair... the rest. 1 when the part has a bone.
static int group_root(uint64_t tbl, int mg, double out[3]) {
    uint32_t hj = fnv_name_of("JU_head");
    double best = -1e9;
    int found = 0;
    for (int k = 0; k < 80; k++) {
        uint64_t el = tbl + 0xb8ull * (uint64_t)k;
        uint32_t off = LD32(el);
        if (!off || off > 0x8000) break;
        char bn[12] = {0};
        for (int i = 0; i < 10; i++) { uint8_t ch = (uint8_t)LD8(tbl + off + (uint64_t)i); if (!ch) break; bn[i] = (char)ch; }
        int g = fnv_name(tbl, off) == hj ? MG_CAP : !strncmp(bn, "DJ_hair", 7) ? (bn[7] == 'F' ? MG_FRONT : bn[7] == 'S' ? MG_SIDE : bn[7] == 'T' ? MG_TAIL : MG_REST) : -1;
        if (g != mg || (g == MG_REST && !bn[7])) continue;
        M4 b = m4_inv(m4_from_mem(el + 0x38));
        if (b.m[1][3] > best) { best = b.m[1][3]; out[0] = b.m[0][3]; out[1] = b.m[1][3]; out[2] = b.m[2][3]; found = 1; }
    }
    return found;
}

// A model is a hair when its element table has bones named DJ_hair... and none named FC_... (a head lists both); only hairs are fitted.
static int is_hair_model(uint64_t tbl) {   // not remembered: the game reuses a model's address for the next model
    int hair = 0, face = 0;
    for (int k = 0; k < 80; k++) {
        uint32_t off = LD32(tbl + 0xb8ull * (uint64_t)k);
        if (!off || off > 0x8000) break;
        char nm[8] = {0};
        for (int i = 0; i < 7; i++) nm[i] = (char)LD8(tbl + off + (uint64_t)i);
        if (!strcmp(nm, "DJ_hair")) hair = 1;
        if (!strncmp(nm, "FC_face", 7)) face = 1;   // a head model lists the hair bones too, but also the face bones; a hair with bangs lists a few (FC_bw.., FC_nose, FC_jaw) and is still a hair
    }
    return hair && !face;
}

// Whether a hair is cut for the other gender than its wearer's: one of its bones that the wearer's skeleton has sits at another height
// there (the hair bones 5 cm, the head joint 3.5 cm; hairs of one gender on another head of it differ by about 1 cm).
static int other_gender_hair(uint64_t tbl, RigW *wr, int dbg) {
    int cross = 0, nm = 0, nfar = 0, head_far = 0;
    for (int k = 0; k < 80; k++) {
        uint64_t el = tbl + 0xb8ull * (uint64_t)k;
        uint32_t off = LD32(el);
        if (!off || off > 0x8000) break;
        char name[24] = {0};
        for (int i = 0; i < 23; i++) { uint8_t ch = (uint8_t)LD8(tbl + off + (uint64_t)i); if (!ch) break; name[i] = (char)ch; }
        if (strncmp(name, "DJ_hair", 7) && strcmp(name, "JU_head")) continue;   // only the hair bones and the head joint tell the gender
        uint32_t h = fnv_name(tbl, off);
        for (int i = 0; i < wr->n; i++) {
            if (wr->h[i] != h) continue;
            double d = m4_inv(m4_from_mem(el + 0x38)).m[1][3] - wr->w[i].m[1][3];
            nm++;
            if (dbg) { static int n; if (n++ < 6000) rt_log("avatar fit: hair model %llx bone %s: height in the part %.4f, in the wearer's rig %.4f", (unsigned long long)tbl, name, m4_inv(m4_from_mem(el + 0x38)).m[1][3], wr->w[i].m[1][3]); }
            if ((d > 0.025 && d < 0.2) || (d < -0.025 && d > -0.2)) { nfar++; if (!strcmp(name, "JU_head")) head_far = 1; }
            break;
        }
    }
    // The hair bones of a hair cut for the other gender all sit about 5 cm off the wearer's (and the head joint 3.5 cm), so it is the head joint or most of the bones that
    // tell. One far bone is not enough: a long ponytail's own tail bones can sit further from the shared skeleton's than that, in a hair made for this very head.
    cross = head_far || (nm > 0 && nfar * 2 > nm);
    if (dbg) { static int n; if (n++ < 3000) rt_log("avatar fit: hair model %llx: %d hair bones match, %d far, other gender %d", (unsigned long long)tbl, nm, nfar, cross); }
    return cross;
}

// Female hairs of the faces 12 and 13 (rows 98 to 113) are cut for a skull that the other female faces do not have: worn on another female
// face their scalp cap leaves a bare patch on the crown. A scale of 1.1 and a lift of 1 cm about the head joint covers it (the same values as
// a female hair on a male head). Decided from the hairstyle under the grid's cursor.
static int cap_fix_wanted(void) {
    if (cur_face < 7 || cur_face > 13 || page != 0 || !cur_grid) return 0;
    uint32_t idx = LD32(cur_grid + 0xc);
    if (idx >= grid_n) return 0;
    int nf = native_face(grid_order[idx]);
    return (nf == 12 || nf == 13) && cur_face != 12 && cur_face != 13;
}

// What the fit did to the hairstyle under the cursor, for the on-screen read-out (XCX_HAIR_HUD=1): the bones decided in the last burst (a hairstyle
// loads and its bones are decided together), how many of them were hair bones and how many were tuned, and the tuning they got.
static volatile int hud_hair, hud_tuned, hud_decided, hud_kept;
static volatile double hud_s, hud_dy, hud_dz;
static volatile uint64_t hud_last_ns;
static volatile int hud_wearer_male;
static volatile double hud_head_y;

// The Modify window for the host overlay (runtime/ui/menu.cpp): lines of "label\tvalue", a part's name as a line starting with '#', the buttons
// that work as lines starting with '@' (the last ones). Returns the number of lines, 0 when the window is not shown: it shows while the category cursor is
// on the Modify row. *open 1 when the player is inside it; *sel the selected line (-1 when none).
int avatar_hair_modify_ui(char lines[48][72], int *open, int *sel) {
    if (!driver_on || !modify_on() || !cur_ui || !hair_tab_active() || cur_face > 13) return 0;
    int op = mod_open || mod_force;
    if (!op && !modify_cursor()) return 0;
    *open = op; *sel = -1;
    int vis[MG_N], nv = mod_parts(vis), n = 0;
    for (int i = 0; i < nv && n + 7 <= 44; i++) {
        int g = vis[i];
        double e[5]; mod_eff(g, e);
        snprintf(lines[n++], 72, "#%s", mg_name[g]);
        if (op && i * MP_N + 0 <= mod_idx && mod_idx < (i + 1) * MP_N) *sel = n + mod_idx % MP_N;
        // "label\tvalue\tposition": the position is where the value lies between the ends of its slider (0 to 1)
#define SLIDER_POS(k) ((e[k] - sl_lo[k]) / (sl_hi[k] - sl_lo[k]))
        snprintf(lines[n++], 72, "%s\tx%.3f\t%.4f", mp_name[0], e[0], SLIDER_POS(0));
        for (int k = 1; k < 4; k++) snprintf(lines[n++], 72, "%s\t%+.1f cm\t%.4f", mp_name[k], e[k] * 100, SLIDER_POS(k));
        snprintf(lines[n++], 72, "%s\t%+.0f deg\t%.4f", mp_name[4], e[4], SLIDER_POS(4));
#undef SLIDER_POS
    }
    if (op) {
        snprintf(lines[n++], 72, "@%d=row;%d=change;%d+%d=part", SYM_CRSUD, SYM_CRSLR, SYM_L, SYM_R);
        snprintf(lines[n++], 72, "@%d=fine;%d=coarse;%d=reset", SYM_ZL, SYM_ZR, SYM_Y);
        snprintf(lines[n++], 72, "@%d=reset all;%d=back", SYM_X, SYM_B);
    }   // (no prompt while the cursor is on the row: A to go in is implied)
    return n;
}

// The category list's scroll bar (runtime/ui/menu.cpp draws it like the grid's): shown while the list is windowed. *pos and *len are fractions of the track.
int avatar_hair_listbar(float *pos, float *len) {
    if (!driver_on || !modify_on() || !cur_ui || !virt_active || !hair_tab_active() || cur_face > 13) return 0;
    *pos = (float)virt_off / 8.0f;
    *len = 7.0f / 8.0f;
    return 1;
}

int avatar_hair_hud(char *buf, int n) {
    static int on = -1;
    if (on < 0) on = cfg_get("XCX_HAIR_HUD") != NULL;
    if (!on || !hair_tab_active() || cur_face > 13) return 0;
    snprintf(buf, (size_t)n, "fit: page %d, %s head %d, rig head %.3f m | last hairstyle: %d bones decided, %d hair, %d tuned (scale %.2f lift %.3f fwd %.3f), %d kept as is",
             page, hud_wearer_male ? "male" : "female", (int)cur_face, hud_head_y, hud_decided, hud_hair, hud_tuned, hud_s, hud_dy, hud_dz, hud_kept);
    return 1;
}

void avatar_hair_ibind(Cpu *c, uint64_t self, uint64_t elem) {
    static int mode = -1;
    static int dbg;
    if (mode < 0) { const char *v = cfg_get("XCX_HAIR_FIT"); mode = !v || (strcmp(v, "0") && strcmp(v, "log") && strcmp(v, "lift")); dbg = v && !strcmp(v, "debug"); }   // on unless XCX_HAIR_FIT=0
    if (!mode || cur_face > 13 || !cur_grid) return;
    RigW *wr = sex_rw[cur_face >= 7];
    if (!wr) return;
    int capfix = cap_fix_wanted();
    static int last_capfix;
    if (capfix != last_capfix) { last_capfix = capfix; ib_gen++; }   // the decisions of the bones are cached per bind: decide them again for the next hairstyle
    float now[16];
    memcpy(&now[0], &c->v[2], 16); memcpy(&now[4], &c->v[3], 16); memcpy(&now[8], &c->v[4], 16); memcpy(&now[12], &c->v[6], 16);
    IbCache *e = &ibc[(elem >> 4) & 8191];
    if (e->elem != elem || e->rw != wr || e->gen != ib_gen || (e->key == mod_cur_key && e->sgen != sw_gen) || memcmp(e->orig, now, sizeof now)) {
        e->elem = elem; e->rw = wr; e->state = 1; e->gen = ib_gen; e->key = mod_cur_key; e->sgen = sw_gen;   // the key the values belong to: when the cursor leaves this hairstyle its bones keep them
        {   // a new burst of decisions: the counters of the read-out start again
            uint64_t now_ns = plat_now_ns();
            if (now_ns - hud_last_ns > 400000000ull) { hud_hair = hud_tuned = hud_decided = hud_kept = 0; }
            hud_last_ns = now_ns; hud_decided++;
            hud_wearer_male = cur_face < 7; hud_head_y = wr->head_y;
        }
        memcpy(e->orig, now, sizeof now);
        uint64_t tbl = LD64(self + 0x130);
        int hairm = tbl && is_hair_model(tbl);
        if (!hairm && tbl) {   // a small model that is not a hair (an attachment: the Special rows of the hair tab?): its bones, once, to see whether the fit should take it
            static uint64_t seen_s[32]; static int ns;
            int have = 0, cnt = 0;
            for (int q = 0; q < ns; q++) have |= seen_s[q] == tbl;
            for (int k = 0; k < 80; k++) { uint32_t off = LD32(tbl + 0xb8ull * (uint64_t)k); if (!off || off > 0x8000) break; cnt++; }
            if (!have && cnt > 0 && cnt <= 24 && ns < 32) {
                seen_s[ns++] = tbl;
                char names[300]; int o = 0;
                for (int k = 0; k < cnt && k < 14 && o < 270; k++) {
                    uint32_t off = LD32(tbl + 0xb8ull * (uint64_t)k);
                    char nm[20] = {0};
                    for (int i = 0; i < 19; i++) { uint8_t ch = (uint8_t)LD8(tbl + off + (uint64_t)i); if (!ch) break; nm[i] = (char)ch; }
                    o += snprintf(names + o, sizeof names - (size_t)o, "%s ", nm);
                }
                rt_log("avatar hair: small model %llx (not a hair, %d bones): %s", (unsigned long long)tbl, cnt, names);
            }
        }
        if (dbg && tbl) {   // the first names of the model's own table, once per model address and class
            static uint64_t seen_m[256]; static int seen_h[256], nseen_m, tl3;
            int have = 0;
            for (int q = 0; q < nseen_m; q++) have |= seen_m[q] == self && seen_h[q] == hairm;
            if (!have && nseen_m < 256 && tl3++ < 200) {
                seen_m[nseen_m] = self; seen_h[nseen_m++] = hairm;
                char names[400]; int o = 0, cnt = 0;
                for (int k = 0; k < 80 && o < 360; k++) {
                    uint32_t off = LD32(tbl + 0xb8ull * (uint64_t)k);
                    if (!off || off > 0x8000) break;
                    cnt++;
                    if (k < 14) { char nm[20] = {0}; for (int i = 0; i < 19; i++) { uint8_t ch = (uint8_t)LD8(tbl + off + (uint64_t)i); if (!ch) break; nm[i] = (char)ch; } o += snprintf(names + o, sizeof names - (size_t)o, "%s ", nm); }
                }
                rt_log("avatar fit: model %llx class hair %d: %d table entries: %s", (unsigned long long)self, hairm, cnt, names);
                if (!hairm) {   // which FC_ bones does a class-0 model list
                    char fcs[300]; int o2 = 0;
                    for (int k = 0; k < 80 && o2 < 260; k++) {
                        uint32_t off = LD32(tbl + 0xb8ull * (uint64_t)k);
                        if (!off || off > 0x8000) break;
                        char nm[20] = {0}; for (int i = 0; i < 19; i++) { uint8_t ch = (uint8_t)LD8(tbl + off + (uint64_t)i); if (!ch) break; nm[i] = (char)ch; }
                        if (!strncmp(nm, "FC_", 3)) o2 += snprintf(fcs + o2, sizeof fcs - (size_t)o2, "%s ", nm);
                    }
                    rt_log("avatar fit:    its FC_ bones: %s", fcs);
                }
            }
        }
        if (dbg && tbl) {
            static int dl;
            char nm[24] = {0};
            uint32_t no = LD32(elem);
            for (int k = 0; k < 23 && no < 0x8000; k++) { uint8_t ch = (uint8_t)LD8(tbl + no + (uint64_t)k); if (!ch) break; nm[k] = (char)ch; }
            if (dl++ < 400) rt_log("avatar fit: bone '%s' model %llx hair %d wearer rig %d bones", nm, (unsigned long long)self, hairm, wr->n);
        }
        if (hairm) {
            hud_hair++;
            uint32_t hh = fnv_name(tbl, LD32(elem));
            int w = -1;
            for (int i = 0; i < wr->n; i++) if (wr->h[i] == hh) { w = i; break; }
            M4 og;
            for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) og.m[i][j] = now[4 * j + i];
            M4 fx = og;
            int ok = 0;
            if (w >= 0) {
                fx = m4_inv(wr->w[w]);
                ok = 1;
            } else {
                // a bone the wearer's skeleton does not have: it moves with the head joint, by the offset between the part's and the
                // wearer's head joint (the corrected bind is the part's own bind times the inverse of that offset)
                uint32_t hj = fnv_name_of("JU_head");
                for (int k = 0; k < 80 && !ok; k++) {
                    uint64_t el = tbl + 0xb8ull * (uint64_t)k;
                    uint32_t off = LD32(el);
                    if (!off || off > 0x8000) break;
                    if (fnv_name(tbl, off) != hj) continue;
                    int hw = -1;
                    for (int i = 0; i < wr->n; i++) if (wr->h[i] == hj) { hw = i; break; }
                    if (hw < 0) break;
                    M4 hb = m4_inv(m4_from_mem(el + 0x38));
                    M4 d = m4_mul(wr->w[hw], m4_inv(hb));   // wearer's head joint = d * the part's head joint
                    fx = m4_mul(og, m4_inv(d));
                    ok = 1;
                }
            }
            if (dbg && !ok) { static int nk2; if (nk2++ < 100) rt_log("avatar fit: model %llx hair bone has no match in the wearer's rig and no head joint (w %d)", (unsigned long long)self, w); }
            if (ok) {
                double dev = 0;
                for (int a = 0; a < 3; a++) for (int b = 0; b < 4; b++) { double d = fx.m[a][b] - og.m[a][b]; if (d < 0) d = -d; if (d > dev) dev = d; }
                if (dbg) rt_log("avatar fit:    -> w %d ok %d dev %.4f", w, ok, dev);
                int tuned = 0;
                if (dbg) { static int tl2; if (tl2++ < 100) rt_log("avatar fit: model %llx bone %d ok, dev %.4f, tune_ovr %d s %.2f page %d", (unsigned long long)self, w, dev, tune_ovr, tune_s, page); }
                {
                    // a hair cut for the other gender (some bone of it sits at another height than in the wearer's skeleton): scale and
                    // lift it about the wearer's head joint, the whole hair alike so that its parts keep together
                    double s = tune_s, dy = tune_dy, dz = tune_dz;
                    if (!tune_ovr) { const double *d = capfix ? tune_x : cur_face >= 7 ? tune_f : tune_m; s = d[0]; dy = d[1]; dz = d[2]; }
                    int hw2 = -1;
                    uint32_t hj2 = fnv_name_of("JU_head");
                    for (int i = 0; i < wr->n; i++) if (wr->h[i] == hj2) { hw2 = i; break; }
                    if (tune_cset && hh == hj2) { s = tune_c[0]; dy = tune_c[1]; dz = tune_c[2]; }   // the head joint's bone: the scalp cap
                    else if (!tune_ovr && !capfix && cur_face < 7 && hh == hj2) { s = tune_cm[0]; dy = tune_cm[1]; dz = tune_cm[2]; }   // the scalp cap of a female hair on a male head: its edge is the ear-shaped notch
                    int root = 0;
                    int mg = -1;   // the Modify part of this bone: the scalp cap, or a strand bone by the letter after DJ_hair
                    {
                        char bn[12] = {0};
                        uint32_t bo = LD32(elem);
                        for (int k = 0; k < 10 && bo < 0x8000; k++) { uint8_t ch = (uint8_t)LD8(tbl + bo + (uint64_t)k); if (!ch) break; bn[k] = (char)ch; }
                        root = !strcmp(bn, "DJ_hair");
                        if (hh == hj2) mg = MG_CAP;
                        else if (!strncmp(bn, "DJ_hair", 7)) mg = bn[7] == 'F' ? MG_FRONT : bn[7] == 'S' ? MG_SIDE : bn[7] == 'T' ? MG_TAIL : MG_REST;
                        if (mg == MG_SIDE) {   // the side locks' bones (DJ_hairSL..., DJ_hairSR...): a male head's ear sits further back than a female hair's cutout
                            if (tune_sdset) { s = tune_sd[0]; dy = tune_sd[1]; dz = tune_sd[2]; }
                            else if (!tune_ovr && !capfix && cur_face < 7) { s = tune_sm[0]; dy = tune_sm[1]; dz = tune_sm[2]; }
                        }
                    }
                    if (mg < 0) {   // a bone of a hair that no part takes (its name, once): the next candidate for a part
                        static uint32_t nm_seen[64]; static int nn;
                        uint32_t hn = fnv_name(tbl, LD32(elem)); int have = 0;
                        for (int q = 0; q < nn; q++) have |= nm_seen[q] == hn;
                        if (!have && nn < 64) {
                            char nb[20] = {0};
                            uint32_t no2 = LD32(elem);
                            for (int q = 0; q < 19 && no2 < 0x8000; q++) { uint8_t ch = (uint8_t)LD8(tbl + no2 + (uint64_t)q); if (!ch) break; nb[q] = (char)ch; }
                            nm_seen[nn++] = hn;
                            rt_log("avatar hair: hair bone '%s' belongs to no part", nb);
                        }
                    }
                    if (mg > MG_ALL) { seen_cnt[mg]++; if (root) seen_root++; else seen_acc |= 1u << mg; }   // the bare root bone "DJ_hair" is no strand: it does not make an "Other strands" part
                    // the fit's own tuning is for a hair cut for the other gender; a hair of the wearer's own gender starts from nothing (and takes the player's changes alike)
                    int cross = page == 1 || force_cross || capfix || other_gender_hair(tbl, wr, dbg);
                    mod_cross = cross;
                    if (!cross) { s = 1.0; dy = dz = 0.0; }
                    // A hair of the wearer's own gender keeps the bind of its own model: its bones were placed by the artists against the hair's own pose (the front bones of a
                    // default hair sit a centimetre off the shared skeleton's, on purpose). Taking the wearer's bind for any bone further than 2 mm off moved the bangs and
                    // tails of every default hair the moment the hair tab opened. Only a hair cut for the other gender is moved onto the wearer's skeleton.
                    if (!cross) { fx = og; dev = 0; }
                    // The fit's own values (s, dy, dz: the cut for the other gender, scaled and lifted about the head joint) and the player's: the whole hair's and the scalp cap's about
                    // the middle of the skull, a strand part's (front, sides, tails, others) about its own root, so that a part grows from where it is rooted instead of flying away
                    // from the head. One transform per level, applied to the bone's bind (v -> part -> whole hair -> fit).
                    double ms = 1.0, mdx = 0.0, mdy = 0.0, mdz = 0.0, mtilt = 0.0, gs = 1.0, gdx = 0.0, gdy = 0.0, gdz = 0.0, gtilt = 0.0;
                    if (mod_any) {   // the player's changes (Modify): the whole hair's, then the part's
                        ms = 1.0 + mvc(MG_ALL, MP_SCALE); mdx = mvc(MG_ALL, MP_X); mdy = mvc(MG_ALL, MP_Y); mdz = mvc(MG_ALL, MP_Z); mtilt = mvc(MG_ALL, MP_TILT);
                        if (mg == MG_CAP) { ms *= 1.0 + mvc(mg, MP_SCALE); mdx += mvc(mg, MP_X); mdy += mvc(mg, MP_Y); mdz += mvc(mg, MP_Z); mtilt += mvc(mg, MP_TILT); }
                        else if (mg > MG_CAP) { gs = 1.0 + mvc(mg, MP_SCALE); gdx = mvc(mg, MP_X); gdy = mvc(mg, MP_Y); gdz = mvc(mg, MP_Z); gtilt = mvc(mg, MP_TILT); }
                    }
                    int has_g = gs != 1.0 || gdx != 0.0 || gdy != 0.0 || gdz != 0.0 || gtilt != 0.0;
                    int has_m = ms != 1.0 || mdx != 0.0 || mdy != 0.0 || mdz != 0.0 || mtilt != 0.0;
                    if ((s != 1.0 || dy != 0.0 || dz != 0.0 || has_m || has_g) && mg >= 0 && hw2 >= 0) {   // page 1 lists only the other gender's hairs; the preview's binds are already the wearer's, so the shape test cannot see it
                        double hj[3] = {wr->w[hw2].m[0][3], wr->w[hw2].m[1][3], wr->w[hw2].m[2][3]}, hc[3] = {hj[0], hj[1] + HEAD_CENTER_UP, hj[2]};
                        if (s != 1.0 || dy != 0.0 || dz != 0.0) fx = m4_mul(fx, m4_about(s, 0.0, hj, 0.0, dy, dz));
                        if (has_m) fx = m4_mul(fx, m4_about(ms, mtilt, hc, mdx, mdy, mdz));
                        double gp[3];
                        if (has_g && group_root(tbl, mg, gp)) fx = m4_mul(fx, m4_about(gs, gtilt, gp, gdx, gdy, gdz));
                        tuned = 1; hud_tuned++; hud_s = s * ms; hud_dy = dy + mdy; hud_dz = dz + mdz;
                        if (dbg) { static int tl; if (tl++ < 6000) rt_log("avatar fit: tuned model %llx bone %d: scale %.2f lift %.3f forward %.3f (page %d)", (unsigned long long)self, w, s, dy, dz, page); }
                    }
                }
                if (!((dev > 2e-3 || tuned) && dev < 0.5)) hud_kept++;
                if (dbg && !((dev > 2e-3 || tuned) && dev < 0.5)) { static int nk; if (nk++ < 300) rt_log("avatar fit: model %llx bone %d kept: dev %.4f tuned %d", (unsigned long long)self, w, dev, tuned); }
                if ((dev > 2e-3 || tuned) && dev < 0.5) {
                    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) e->fix[4 * j + i] = (float)fx.m[i][j];
                    e->state = 2;
                    static int logged;
                    if (logged++ < 400) rt_log("avatar fit: bind of model %llx bone %d replaced (dev %.4f)%s", (unsigned long long)self, w, dev, w < 0 ? " by the head joint's offset" : "");
                }
            }
        }
    }
    if (e->state != 2) return;
    memcpy(&c->v[2], &e->fix[0], 16); memcpy(&c->v[3], &e->fix[4], 16); memcpy(&c->v[4], &e->fix[8], 16); memcpy(&c->v[6], &e->fix[12], 16);
}
