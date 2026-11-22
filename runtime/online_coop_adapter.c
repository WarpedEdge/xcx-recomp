// Shows another player's position in this game. Each pass of the game's main loop the local player's node position and the two
// area words go out as a pose message, and the newest pose from the peer is held on the second character node (a party member), so
// it stands where the other player stands. Positions are only shown when both players are in the same area.
//
// Settings (xcx.ini or the environment):
//   XCX_COOP_POSE_PEER=<ipv4>:<port>   where to send this game's pose
//   XCX_COOP_POSE_PORT=<port>          the local UDP port to listen on
// Either one turns the pose link on. Without both of them nothing is sent or shown.
//   XCX_COOP_TIMER=normal              the death countdown runs as in the game. Without it the countdown is held while the other
//                                      player is connected (a downed player stays down until the party revives them or all are down)
//   XCX_COOP_FOLLOW=1                  off by default. When the other player has been in another area for 3 s, this game
//                                      jumps to that area, then to the other player's position (for tests; a joining action
//                                      the player chooses comes later)
#include "online_coop_adapter.h"
#include "cfg.h"
#include "host.h"
#include "online_coop_link.h"
#include "online_coop_pose.h"
#include <stdlib.h>
#include <string.h>

extern const char rt_game_name[];
#define MAIN 0x7000000000ull
// Guest addresses that were read from the game and checked in play (research notes 13, 17, 21 of the co-op project).
#define NODE_ARRAY 0x7001dc1860ull   // 24-byte entries; the first word of each is a pointer to a character node
#define NODE_TYPE 0x7001c33200ull    // the first word of a character node
#define NODE_ENTRIES 24
#define NODE_POS 0x40                // three floats; the rotation rows are the twelve floats from +0x10
#define NODE_ROT 0x10
#define AREA_WORD 0x7001df5944ull    // and the next word: both must be equal to the peer's
#define AREA_WORD2 0x7001df5948ull
// The game's node copy f_70001ec150(node, source) copies 0x50 bytes from the source block into the node. The source holds the
// rotation rows at +0x00 to +0x30 and the position at +0x30, so a value put there before the copy is what the node ends up with.
#define SRC_ROT 0x00
#define SRC_POS 0x30
#define SEND_INTERVAL_NS 50000000ull   // 20 poses a second
#define STALE_NS 1000000000ull         // a pose older than this is no longer shown
// The jump (research notes 15 and 17 of the co-op project). The executor f_70002e269c(manager, 0, 0, row, 1) enters an area by a row of the
// game's jump table; the same request object the game's own scripts fill can then move the player to a position.
#define JUMP_EXECUTOR (MAIN + 0x2e269c)
#define JUMP_RUNNER (MAIN + 0x2f7d20)
#define JUMP_MANAGER_PTR 0x7001d58318ull   // points at a pointer to the manager
#define JUMP_REQUEST_PTR 0x7001d587d0ull   // points at a pointer to the request object
// The death countdown (research note 19): a pointer at COUNTDOWN_PTR to an object whose state word at +8 is 3 while the countdown runs and whose
// float at +0xc counts down from 30.0.
#define COUNTDOWN_PTR 0x7001f91e30ull
#define COUNTDOWN_START 30.0f
#define MISMATCH_NS 3000000000ull          // how long the other player is in another area before this game follows
#define JUMP_GAP_NS 15000000000ull         // the shortest time between two jumps to another area
#define POS_JUMP_GAP_NS 4000000000ull      // after the area jump, the time to let the area load before the position jump
#define JUMP_TRIES 3                       // area jumps per area the other player is in

enum { HOOK_COOP_TICK = 11, HOOK_COOP_NODE_COPY = 12 };

static struct {
    int init, active;
    unsigned seq, sent, received, rejected;
    uint64_t last_send_ns, last_log_ns, remote_ns;
    CoopPose remote;
    int have_remote, holding;
    uint64_t hold_node;   // the node being held, so the node copy hook can tell it from the others
    int follow, pending_pos, hold_timer;
    unsigned timer_holds;
    uint64_t mismatch_ns, last_jump_ns;
    uint32_t tried_area; unsigned tries;
} st;

extern uint64_t (*rt_native_ui_read64)(uint64_t);

static int readable_writable(uint64_t p, uint64_t n) {
    RtMemoryInfo i;
    return p && n && p <= UINT64_MAX - n && !mem_query(p, &i) && p >= i.base && p - i.base <= i.size &&
           n <= i.size - (p - i.base) && (i.perm & 3) == 3;
}
static float getf(uint64_t a) { uint32_t b = (uint32_t)LD32(a); float f; memcpy(&f, &b, 4); return f; }
static void putf(uint64_t a, float f) { uint32_t b; memcpy(&b, &f, 4); ST32(a, b); }

static void init(void) {
    st.init = 1;
    if (strcmp(rt_game_name, "2.0.0")) return;
    const char *peer = cfg_get("XCX_COOP_POSE_PEER"), *port = cfg_get("XCX_COOP_POSE_PORT");
    if ((!peer || !*peer) && (!port || !*port)) return;
    const char *fol = cfg_get("XCX_COOP_FOLLOW");
    st.follow = fol && !strcmp(fol, "1");
    const char *tm = cfg_get("XCX_COOP_TIMER");
    st.hold_timer = !(tm && !strcmp(tm, "normal"));
    if (coop_link_open(peer && *peer ? peer : NULL, port && *port ? (unsigned)atoi(port) : 0)) {
        st.active = 1;
        rt_log("online co-op: pose link on, peer %s, local port %s, follow %d", peer && *peer ? peer : "-", port && *port ? port : "-", st.follow);
    }
}

// The character nodes of the node array, in array order. Returns how many there are and puts the first two into nodes.
static unsigned character_nodes(uint64_t nodes[2]) {
    unsigned n = 0;
    for (unsigned e = 0; e < NODE_ENTRIES; ++e) {
        uint64_t node = rt_native_ui_read64(NODE_ARRAY + 24ull * e);
        if (!readable_writable(node, 0x60) || rt_native_ui_read64(node) != NODE_TYPE) continue;
        if (n < 2) nodes[n] = node;
        ++n;
    }
    return n;
}

// The table row that enters an area. From the sweep of 199 rows: the area word is the row minus 24 for rows 29 to 101 and the row minus
// 27 for rows 219 to 410. Areas outside both ranges are not known, and 0 is returned.
static unsigned row_for_area(uint32_t area) {
    if (area >= 5 && area <= 77) return area + 24;
    if (area >= 192 && area <= 383) return area + 27;
    return 0;
}

static void jump_to_row(Cpu *c, unsigned row) {
    uint64_t p = rt_native_ui_read64(JUMP_MANAGER_PTR), mgr = p ? rt_native_ui_read64(p) : 0;
    if (!readable_writable(mgr, 0x40)) { rt_log("online co-op: follow: the jump manager is not readable"); return; }
    Cpu saved = *c;
    rt_call(c, JUMP_EXECUTOR, 5, mgr, 0ull, 0ull, (uint64_t)row, 1ull);
    *c = saved;
    rt_log("online co-op: follow: jumped to the entry of row %u", row);
}

// The request object the game's own scripts fill: area words at +0x14 and +0x18, the entry at +0x34 (bit 31: use the position), the
// position at +0x20 and the heading at +0x30, and bits 0 and 1 of the flags at +0x10 ask for the jump.
static void jump_to_position(Cpu *c, uint32_t area, uint32_t area2, const float pos[3], const float from[3]) {
    uint64_t h = rt_native_ui_read64(JUMP_REQUEST_PTR), obj = h ? rt_native_ui_read64(h) : 0;
    if (!readable_writable(obj, 0x40)) { rt_log("online co-op: follow: the jump request is not readable"); return; }
    ST32(obj + 0x14, area); ST32(obj + 0x18, area2); ST32(obj + 0x34, 0x80000000u);
    for (int k = 0; k < 3; ++k) putf(obj + 0x20 + 4 * k, pos[k]);
    putf(obj + 0x30, 0.0f);
    ST32(obj + 0x10, (uint32_t)LD32(obj + 0x10) | 3u);
    Cpu saved = *c;
    rt_call(c, JUMP_RUNNER, 1, obj);
    *c = saved;
    rt_log("online co-op: follow: moved from %.1f, %.1f, %.1f to the other player's position %.1f, %.1f, %.1f", from[0], from[1], from[2], pos[0], pos[1], pos[2]);
}

// With the follow setting on: the other player has been in another area for a while, so this game goes there, and then to the same spot.
static void follow_remote(Cpu *c, uint64_t now, int fresh, int have_area, uint32_t area, uint32_t area2, const float from[3]) {
    int same = fresh && have_area && st.remote.area == area && st.remote.area2 == area2;
    if (!fresh || !have_area || same) st.mismatch_ns = 0;
    if (fresh && have_area && !same) {
        if (!st.mismatch_ns) st.mismatch_ns = now;
        if (st.tried_area != st.remote.area) { st.tried_area = st.remote.area; st.tries = 0; }
        if (now - st.mismatch_ns > MISMATCH_NS && now - st.last_jump_ns > JUMP_GAP_NS && st.tries < JUMP_TRIES) {
            unsigned row = row_for_area(st.remote.area);
            if (!row) { st.tries = JUMP_TRIES; rt_log("online co-op: follow: no entry is known for area %u", st.remote.area); return; }
            ++st.tries; st.last_jump_ns = now; st.pending_pos = 1;
            rt_log("online co-op: follow: the other player is in area %u/%u, this game in %u/%u", st.remote.area, st.remote.area2, area, area2);
            jump_to_row(c, row);
        }
    } else if (same && st.pending_pos && now - st.last_jump_ns > POS_JUMP_GAP_NS) {
        st.pending_pos = 0;
        jump_to_position(c, area, area2, st.remote.pos, from);
    }
}

// While the countdown runs the float is put back to its start value every pass, so it never reaches zero.
static void hold_countdown(void) {
    uint64_t obj = rt_native_ui_read64(COUNTDOWN_PTR);
    if (!obj || !readable_writable(obj, 0x20) || (uint32_t)LD32(obj + 8) != 3 || getf(obj + 0xc) >= COUNTDOWN_START - 0.1f) return;
    if (!st.timer_holds++) rt_log("online co-op: the death countdown is held");
    putf(obj + 0xc, COUNTDOWN_START);
}

static void tick(Cpu *c) {
    uint64_t now = kern_now_ns();
    uint64_t nodes[2] = {0, 0};
    unsigned n = character_nodes(nodes);
    uint32_t area = 0, area2 = 0;
    int have_area = readable_writable(AREA_WORD, 8);
    if (have_area) { area = (uint32_t)LD32(AREA_WORD); area2 = (uint32_t)LD32(AREA_WORD2); }

    if (n >= 1 && have_area && now - st.last_send_ns >= SEND_INTERVAL_NS) {
        st.last_send_ns = now;
        CoopPose p = { ++st.seq, area, area2, { getf(nodes[0] + NODE_POS), getf(nodes[0] + NODE_POS + 4), getf(nodes[0] + NODE_POS + 8) }, {0} };
        for (int k = 0; k < 12; ++k) p.rot[k] = getf(nodes[0] + NODE_ROT + 4 * k);
        uint8_t msg[COOP_POSE_BYTES];
        coop_pose_encode(&p, msg);
        if (coop_link_send(msg, sizeof msg)) ++st.sent;
    }

    uint8_t msg[COOP_POSE_BYTES + 1];
    size_t got;
    while ((got = coop_link_recv(msg, sizeof msg)) != 0) {
        CoopPose p;
        if (!coop_pose_decode(msg, got, &p)) { ++st.rejected; continue; }
        ++st.received;
        // Datagrams can arrive late or twice; an older number than the one shown is dropped.
        if (st.have_remote && (int32_t)(p.seq - st.remote.seq) <= 0) continue;
        st.remote = p; st.remote_ns = now; st.have_remote = 1;
    }

    int shown = st.have_remote && now - st.remote_ns < STALE_NS && have_area &&
                st.remote.area == area && st.remote.area2 == area2 && n >= 2;
    if (shown) {
        st.hold_node = nodes[1];
        if (!st.holding) rt_log("online co-op: showing the other player's position");
    } else if (st.holding) {
        rt_log("online co-op: the other player's position is no longer shown (in the same area %d, recent %d, party member %d)",
               have_area && st.remote.area == area && st.remote.area2 == area2, st.have_remote && now - st.remote_ns < STALE_NS, n >= 2);
    }
    st.holding = shown;
    if (st.hold_timer && st.have_remote && now - st.remote_ns < STALE_NS) hold_countdown();
    if (st.follow) {
        float here[3] = {0, 0, 0};
        if (n >= 1) for (int k = 0; k < 3; ++k) here[k] = getf(nodes[0] + NODE_POS + 4 * k);
        follow_remote(c, now, st.have_remote && now - st.remote_ns < STALE_NS, have_area, area, area2, here);
    }
    if (now - st.last_log_ns >= 5000000000ull) {
        st.last_log_ns = now;
        rt_log("online co-op: sent %u, received %u, rejected %u, area %u/%u, player at %.1f, %.1f, %.1f", st.sent, st.received, st.rejected, area, area2,
               n >= 1 ? getf(nodes[0] + NODE_POS) : 0.0f, n >= 1 ? getf(nodes[0] + NODE_POS + 4) : 0.0f, n >= 1 ? getf(nodes[0] + NODE_POS + 8) : 0.0f);
    }
}

static void node_copy(Cpu *c) {
    uint64_t node = c->x[0], src = c->x[1];
    if (!st.holding || node != st.hold_node || !readable_writable(src, 0x50)) return;
    for (int k = 0; k < 3; ++k) putf(src + SRC_POS + 4 * k, st.remote.pos[k]);
    for (int k = 0; k < 12; ++k) putf(src + SRC_ROT + 4 * k, st.remote.rot[k]);
}

int rt_online_coop_hook(unsigned id, Cpu *c) {
    if (!st.init) init();
    if (!st.active) return 0;
    if (id == HOOK_COOP_TICK) tick(c);
    else if (id == HOOK_COOP_NODE_COPY) node_copy(c);
    return 0;
}
