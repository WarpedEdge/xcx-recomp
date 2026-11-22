// Shows another player's position in this game. Each pass of the game's main loop the local player's node position and the two
// area words go out as a pose message, and the newest pose from the peer is held on the second character node (a party member), so
// it stands where the other player stands. Positions are only shown when both players are in the same area.
//
// Settings (xcx.ini or the environment):
//   XCX_COOP_POSE_PEER=<ipv4>:<port>   where to send this game's pose
//   XCX_COOP_POSE_PORT=<port>          the local UDP port to listen on
// Either one turns the pose link on. Without both of them nothing is sent or shown.
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

enum { HOOK_COOP_TICK = 11, HOOK_COOP_NODE_COPY = 12 };

static struct {
    int init, active;
    unsigned seq, sent, received, rejected;
    uint64_t last_send_ns, last_log_ns, remote_ns;
    CoopPose remote;
    int have_remote, holding;
    uint64_t hold_node;   // the node being held, so the node copy hook can tell it from the others
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
    if (coop_link_open(peer && *peer ? peer : NULL, port && *port ? (unsigned)atoi(port) : 0)) {
        st.active = 1;
        rt_log("online co-op: pose link on, peer %s, local port %s", peer && *peer ? peer : "-", port && *port ? port : "-");
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

static void tick(void) {
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
    if (now - st.last_log_ns >= 5000000000ull) {
        st.last_log_ns = now;
        rt_log("online co-op: sent %u, received %u, rejected %u, area %u/%u", st.sent, st.received, st.rejected, area, area2);
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
    if (id == HOOK_COOP_TICK) tick();
    else if (id == HOOK_COOP_NODE_COPY) node_copy(c);
    return 0;
}
