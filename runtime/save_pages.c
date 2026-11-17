// Save slot pages, see save_pages.h.
#include "save_pages.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "host.h"

static volatile int page;   // read by guest threads (fs_host.c), written by the Save/Load screen's thread

int save_pages_current(void) { return __atomic_load_n(&page, __ATOMIC_RELAXED); }

int save_pages_set(int p) {
    if (p < 0) p = 0;
    if (p >= SAVE_PAGE_COUNT) p = SAVE_PAGE_COUNT - 1;
    return __atomic_exchange_n(&page, p, __ATOMIC_RELAXED) != p;
}

int save_pages_step(int delta) {
    int p = ((save_pages_current() + delta) % SAVE_PAGE_COUNT + SAVE_PAGE_COUNT) % SAVE_PAGE_COUNT;
    return save_pages_set(p);
}

void save_pages_label(char *out, size_t cap) {
    snprintf(out, cap, "Page %d/%d", save_pages_current() + 1, SAVE_PAGE_COUNT);
}

int save_pages_remap_path(const char *rest, char *out, size_t cap) {
    static const char prefix[] = "/sts/game/gamedata";
    int p = save_pages_current();
    if (!p || strncmp(rest, prefix, sizeof prefix - 1)) return 0;
    const char *d = rest + sizeof prefix - 1;
    if (d[0] < '0' || d[0] > '9' || d[1] < '0' || d[1] > '9') return 0;
    int slot = (d[0] - '0') * 10 + (d[1] - '0');
    if (slot < 1 || slot > SAVE_PAGE_SLOTS) return 0;
    const char *tail = d + 2;
    if (*tail && strcmp(tail, ".tmb")) return 0;
    return snprintf(out, cap, "%s%02d%s", prefix, slot + p * SAVE_PAGE_SLOTS, tail) < (int)cap;
}

// Fields of the screen object, offsets from its base (found from the screen's update code and a state trace).
enum {
    SCENE_ROWS = 0x438,        // 4 rows of ROW_SIZE bytes: u32 state at +0 (0 idle, 1 requested, 4 loaded), u16 slot at +4, valid byte at +9
    ROW_SIZE = 0xc5b0,
    SCENE_COUNT = 0xf1af8,     // u32 number of rows
    SCENE_LOADING = 0x121b08,  // u32, 1 while the rows load
    SCENE_READY = 0x121b0c,    // u32, bit 0 once the screen is ready for input
};
#define W32(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define W8(a, v) (*(volatile uint8_t *)(uintptr_t)(a) = (v))

void save_pages_reload(uint64_t scene) {
    for (int i = 1; i < 4; i++) {   // row 0 is the auto save, which no page changes
        uint64_t row = scene + SCENE_ROWS + (uint64_t)i * ROW_SIZE;
        W8(row + 9, 0);
        W32(row, 1);
    }
    W32(scene + SCENE_READY, 0);
    W32(scene + SCENE_LOADING, 1);
}

// The game's button bits in the mask of buttons pressed this frame.
enum { PAD_L = 0x10, PAD_R = 0x20 };
static int sound_pending;   // a page change waits for rt_save_pages_update to play its sound
// The game's sound effect queue: the manager's accessor and the request (manager, effect, 0). Effect 5 is the one the
// game's own screens play when L or R switches a tab or page.
#define MAIN 0x7000000000ull
enum { F_SOUND_MANAGER = 0x208554, F_PLAY_SOUND = 0x40bc48, SOUND_TAB_SWITCH = 5 };

void rt_save_pages_pad(uint64_t scene, uint32_t pad) {
    int delta = pad & PAD_R ? 1 : pad & PAD_L ? -1 : 0;
    if (pad & PAD_L && pad & PAD_R) delta = 0;
    if (delta && save_pages_step(delta)) {
        save_pages_reload(scene);
        sound_pending = 1;
    }
}

void rt_save_pages_update(Cpu *c) {
    if (!sound_pending) return;
    sound_pending = 0;
    uint64_t saved[19];
    memcpy(saved, c->x, sizeof saved);
    uint64_t sound = rt_call(c, MAIN + F_SOUND_MANAGER, 0);
    if (sound) rt_call(c, MAIN + F_PLAY_SOUND, 3, sound, SOUND_TAB_SWITCH, 0);
    memcpy(c->x, saved, sizeof saved);
}

// Game functions that find a child widget by name, take it as a text widget and set its string (the row code at
// main+0x6665c0 does the same for the date), and the name of the label that marks the auto save row. Every row has
// one; the manual rows keep theirs hidden, and this code shows it with the slot number.
enum { F_FIND_CHILD = 0x55f16c, F_AS_TEXT = 0x915110, F_SET_TEXT = 0x91720c, S_TYPE_LABEL = 0x17ce2ee };
enum { ROW_GROUPS = 0xd8, ROW_GROUP_SIZE = 0x10 };   // per row: a widget handle that holds that row's labels

static void row_label(Cpu *c, uint64_t scene, int row, const char *text) {
    uint64_t label = rt_call(c, MAIN + F_FIND_CHILD, 2, scene + ROW_GROUPS + (uint64_t)row * ROW_GROUP_SIZE, MAIN + S_TYPE_LABEL);
    uint64_t t = label ? rt_call(c, MAIN + F_AS_TEXT, 1, label) : 0;
    if (!t) return;
    rt_call(c, MAIN + F_SET_TEXT, 2, t, (uint64_t)(uintptr_t)text);
    rt_call(c, LD64(LD64(t) + 0x20), 2, t, 1);   // virtual +0x20: show or hide
}

enum { TITLE_NONE, TITLE_ARMED, TITLE_HAVE };
static struct { int state, busy; uint64_t widget; char text[64], empty[48]; } title;   // touched only by the screen's guest thread
enum { F_TEXT_CALLER_END = 0x55f5c0 };   // return address inside the by-message-id label setter the frame uses
enum { F_EMPTY_CALLER_END = 0x665bb8, S_EMPTY_LABEL = 0x17c6589, S_EMPTY_LABEL2 = 0x17a936d, EMPTY_GROUPS = 0x120 };
// The text of a row without a save is set by the row code at main+0x665bb4, in rect_TextNon and rect_TextNon02 of the
// row's second widget group (one per row from EMPTY_GROUPS). The first row's text is kept, so each empty row can show
// it with the slot number after it in whatever language the game runs in.

void rt_save_pages_open(void) {
    title.state = TITLE_ARMED;
    title.widget = 0;
}

void rt_save_pages_text(Cpu *c) {
    uint64_t str = c->x[1];
    if (c->x[30] == MAIN + F_EMPTY_CALLER_END && str > 0x10000 && !title.busy)   // the first empty-row label's text
        snprintf(title.empty, sizeof title.empty, "%.*s", (int)sizeof title.empty - 1, (const char *)(uintptr_t)str);
    if (title.state != TITLE_ARMED || title.busy || str < 0x10000 || c->x[30] != MAIN + F_TEXT_CALLER_END) return;
    title.widget = c->x[0];
    snprintf(title.text, sizeof title.text, "%.*s", (int)sizeof title.text - 1, (const char *)(uintptr_t)str);
    title.state = TITLE_HAVE;
}

static void title_label(Cpu *c) {
    if (title.state != TITLE_HAVE) return;
    char page[24], label[160];
    save_pages_label(page, sizeof page);
    snprintf(label, sizeof label, "%s   [ST:icon p1=l] %s [ST:icon p1=r]", title.text, page);
    title.busy = 1;
    rt_call(c, MAIN + F_SET_TEXT, 2, title.widget, (uint64_t)(uintptr_t)label);
    title.busy = 0;
}

void rt_save_pages_rows(Cpu *c) {
    uint64_t scene = c->x[0], saved[19];
    memcpy(saved, c->x, sizeof saved);
    for (int row = 1; row <= SAVE_PAGE_SLOTS; row++) {
        char text[24];
        snprintf(text, sizeof text, "No. %02d", save_pages_current() * SAVE_PAGE_SLOTS + row);
        row_label(c, scene, row, text);
    }
    for (int row = 1; row <= SAVE_PAGE_SLOTS && title.empty[0]; row++) {
        char text[96];
        snprintf(text, sizeof text, "%s   No. %02d", title.empty, save_pages_current() * SAVE_PAGE_SLOTS + row);
        for (int k = 0; k < 2; k++) {
            uint64_t w = rt_call(c, MAIN + F_FIND_CHILD, 2, scene + EMPTY_GROUPS + (uint64_t)row * ROW_GROUP_SIZE,
                                 MAIN + (k ? S_EMPTY_LABEL2 : S_EMPTY_LABEL));
            uint64_t t = w ? rt_call(c, MAIN + F_AS_TEXT, 1, w) : 0;
            if (t) {
                title.busy = 1;
                rt_call(c, MAIN + F_SET_TEXT, 2, t, (uint64_t)(uintptr_t)text);
                title.busy = 0;
            }
        }
    }
    title_label(c);
    memcpy(c->x, saved, sizeof saved);
}
