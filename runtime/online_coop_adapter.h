// The game side of the online co-op add-on for the pinned XCX DE 2.0.0 build: the per-pass tick and the node copy hook. Both run on
// the game thread, so reading and writing game memory here needs no lock. rt_native_ui_hook (native_ui_adapter.c) calls this for the
// two hook ids in recomp/native_ui.py. With no co-op setting the add-on does nothing and opens no socket.
#pragma once
#include "cpu.h"

// The hook entry. Always returns 0, so the game's own function still runs after it.
int rt_online_coop_hook(unsigned id, Cpu *c);
