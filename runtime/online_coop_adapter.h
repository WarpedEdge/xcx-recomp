// The game side of the online co-op add-on for the pinned XCX DE 2.0.0 build: the per-pass tick and the node copy hook. Both run on
// the game thread, so reading and writing game memory here needs no lock. rt_native_ui_hook (native_ui_adapter.c) calls this for the
// two hook ids in recomp/native_ui.py. With no co-op setting the add-on does nothing and opens no socket.
#pragma once
#include "cpu.h"

// The hook entry. Always returns 0, so the game's own function still runs after it.
int rt_online_coop_hook(unsigned id, Cpu *c);

// Teleport actions between the two games. The player's menu (not built yet) calls online_coop_command from any thread; the game thread
// runs it in its next pass. "Ask" versions send a question the other player answers with OC_CMD_ACCEPT or OC_CMD_DECLINE (it waits while that player
// is in a fight the asker is not in, and expires after 30 s). "Force" versions need no answer.
enum {
    OC_CMD_GO = 1,            // ask to go to the other player
    OC_CMD_BRING = 2,         // ask the other player to come here
    OC_CMD_FORCE_GO = 3,      // go to the other player now
    OC_CMD_FORCE_BRING = 4,   // move the other player here now
    OC_CMD_ACCEPT = 5,        // answer the other player's question: yes
    OC_CMD_DECLINE = 6        // answer the other player's question: no
};
void online_coop_command(unsigned cmd);

// The squad session, as the native UI registry knows it (native_ui_adapter.c reads it each pass on the game thread). mode is the
// squad details mode of the last report (MOD_NATIVE_MODE_CREATED 0, MOD_NATIVE_MODE_JOINED 1) or a negative number when no session is up;
// code is the squad's passcode. A session opens the pose link on the ports coop_session_ports gives; no session closes it. A link set
// by the XCX_COOP_POSE_* settings is left alone.
void online_coop_session(int mode, const char *code);

