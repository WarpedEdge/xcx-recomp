// The save slot pages of runtime/save_pages.c: the file name each page maps the game's three manual slots to, the
// page range and the labels. Built and run by tests/save_pages_check.py; prints each failure and exits 1 on any.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "host.h"

// save_pages.c calls the guest through these; the checks below never reach them.
uint64_t rt_call(Cpu *c, uint64_t fn, int n, ...) { (void)c, (void)fn, (void)n; return 0; }
void rt_log(const char *fmt, ...) { (void)fmt; }

#include "../runtime/save_pages.c"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void expect_map(const char *in, const char *want) {
    char out[96] = "";
    int r = save_pages_remap_path(in, out, sizeof out);
    if (want) CHECK(r && !strcmp(out, want), "%s -> '%s' (want %s)", in, out, want);
    else CHECK(!r, "%s was mapped to '%s'", in, out);
}

int main(void) {
    CHECK(save_pages_current() == 0, "starts on the first page");
    expect_map("/sts/game/gamedata01", NULL);   // page 1 is the game's own naming
    expect_map("/sts/game/gamedata03.tmb", NULL);

    CHECK(save_pages_set(1), "page 2 is a change");
    CHECK(!save_pages_set(1), "page 2 again is not");
    expect_map("/sts/game/gamedata01", "/sts/game/gamedata04");
    expect_map("/sts/game/gamedata02.tmb", "/sts/game/gamedata05.tmb");
    expect_map("/sts/game/gamedata03", "/sts/game/gamedata06");
    expect_map("/sts/game/gamedataa", NULL);        // the auto save never moves
    expect_map("/sts/game/gamedataa.tmb", NULL);
    expect_map("/sts/game/gamedata04", NULL);       // only the game's own three names map
    expect_map("/sts/game/gamedata00", NULL);
    expect_map("/sts/game/gamedata01.bak", NULL);
    expect_map("/sts/game/gamedata1", NULL);
    expect_map("/sts/game/systemdata", NULL);
    expect_map("/sts/game/socialdata", NULL);
    expect_map("/sts/other/gamedata01", NULL);

    CHECK(save_pages_set(SAVE_PAGE_COUNT - 1), "last page");
    expect_map("/sts/game/gamedata03.tmb", "/sts/game/gamedata30.tmb");   // 30 manual slots in all
    CHECK(save_pages_set(99) == 0 && save_pages_current() == SAVE_PAGE_COUNT - 1, "clamped high");
    CHECK(save_pages_set(-5) && save_pages_current() == 0, "clamped low");

    char out[8];
    CHECK(save_pages_set(1) && !save_pages_remap_path("/sts/game/gamedata01", out, 4), "a name that does not fit is refused");

    save_pages_set(SAVE_PAGE_COUNT - 1);
    CHECK(save_pages_step(1) && save_pages_current() == 0, "next wraps to the first page");
    CHECK(save_pages_step(-1) && save_pages_current() == SAVE_PAGE_COUNT - 1, "previous wraps to the last page");
    CHECK(save_pages_step(-3) && save_pages_current() == SAVE_PAGE_COUNT - 4, "steps by delta");

    char label[32];
    save_pages_set(1);
    save_pages_label(label, sizeof label);
    CHECK(!strcmp(label, "Page 2/10"), "label '%s'", label);

    puts(fails ? "save_pages_check: FAILED" : "save_pages_check: ok");
    return fails != 0;
}
