// Shows another player's position in this game. Each pass of the game's main loop the local player's node position and the two
// area words go out as a pose message, and the newest pose from the peer is held on the second character node (a party member), so
// it stands where the other player stands. Positions are only shown when both players are in the same area (the same area numbers, or within 400 units).
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
//   XCX_COOP_TEST_CMD=<name>@<seconds> one teleport action from the list in online_coop_adapter.h (go, bring, force_go,
//                                      force_bring, accept, decline), run once that many seconds after the first pass (for tests)
//   XCX_COOP_AUTO_ACCEPT=1             answer a teleport question with yes when it can be answered (for tests)
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
#define ASK_TIMEOUT_NS 30000000000ull      // a teleport question that nobody answered is dropped after this
#define GO_TIMEOUT_NS 30000000000ull       // a teleport that has not landed after this is dropped
#define GO_OFFSET 2.0f                     // a teleport lands this far to the side of the target, not inside the other character

enum { HOOK_COOP_TICK = 11, HOOK_COOP_NODE_COPY = 12 };

static struct {
    int init, active;
    unsigned seq, sent, received, rejected;
    uint64_t last_send_ns, last_log_ns, remote_ns;
    CoopPose remote;
    int have_remote, holding;
    uint64_t hold_node;   // the node being held, so the node copy hook can tell it from the others
    unsigned battle_state;
    int remote_fight;       // the other player's last pose said they are in a fight
    float fight_start[3];   // where the player stood when the fight began
    int follow, pending_pos, hold_timer;
    unsigned timer_holds;
    uint64_t mismatch_ns, last_jump_ns;
    uint32_t tried_area; unsigned tries;
    // This game's player as of the last pass, for the teleport actions.
    int me_ok; uint32_t me_area, me_area2; float me_pos[3];
    unsigned ctl_seq, ctl_last_seq; int have_ctl_seq;
    unsigned ask_kind; uint64_t ask_ns;              // the question this game asked and waits for the answer to
    unsigned prompt_kind; uint64_t prompt_ns; CoopCtl prompt;   // the question the other player asked
    int go_active, go_jumped; uint64_t go_ns; uint32_t go_area, go_area2; float go_pos[3];   // a teleport under way
    unsigned cmd;                                    // a command from outside, taken by the next pass
    int auto_accept; unsigned test_cmd; uint64_t test_at_ns, first_tick_ns;
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
    const char *aa = cfg_get("XCX_COOP_AUTO_ACCEPT");
    st.auto_accept = aa && !strcmp(aa, "1");
    const char *tc = cfg_get("XCX_COOP_TEST_CMD");
    if (tc && *tc) {
        static const char *const names[] = { "", "go", "bring", "force_go", "force_bring", "accept", "decline" };
        const char *at = strchr(tc, '@');
        size_t len = at ? (size_t)(at - tc) : strlen(tc);
        for (unsigned k = 1; k <= 6; ++k)
            if (strlen(names[k]) == len && !strncmp(tc, names[k], len)) { st.test_cmd = k; st.test_at_ns = at ? (uint64_t)(atof(at + 1) * 1e9) : 0; }
    }
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

// Two games count as in the same area when the area words match, or when the other player stands within NEAR_BLOCK of this player in the
// world. A short walk can change the words (neighboring blocks of the same ground carry different ones: 73 and 83 were seen about 100 units
// apart), and the stand-in must not vanish then. The rule needs a pose from the other player.
#define NEAR_BLOCK 400.0f
static int same_area(int have_area, uint32_t area, uint32_t area2, const float here[3]) {
    if (!st.have_remote || !have_area) return 0;
    if (st.remote.area == area && st.remote.area2 == area2) return 1;
    float dx = st.remote.pos[0] - here[0], dz = st.remote.pos[2] - here[2];
    return dx * dx + dz * dz < NEAR_BLOCK * NEAR_BLOCK;
}

// With the follow setting on: the other player has been in another area for a while, so this game goes there, and then to the same spot.
static void follow_remote(Cpu *c, uint64_t now, int fresh, int have_area, uint32_t area, uint32_t area2, const float from[3]) {
    int same = fresh && same_area(have_area, area, area2, from);
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

static int remote_fresh(uint64_t now) { return st.have_remote && now - st.remote_ns < STALE_NS; }

static void send_ctl(unsigned kind) {
    if (!st.me_ok) return;
    CoopCtl m = { ++st.ctl_seq, kind, st.me_area, st.me_area2, { st.me_pos[0], st.me_pos[1], st.me_pos[2] } };
    uint8_t w[COOP_CTL_BYTES];
    coop_ctl_encode(&m, w);
    for (int i = 0; i < 3; ++i) coop_link_send(w, sizeof w);   // the same number three times: a lost datagram is not the end of it
}

// A teleport to a place in the world: the same area, by the position request, or another area, by its entry row and then the position request.
static void start_go(uint64_t now, uint32_t area, uint32_t area2, const float pos[3], const char *why) {
    st.go_active = 1; st.go_jumped = 0; st.go_ns = now; st.go_area = area; st.go_area2 = area2;
    st.go_pos[0] = pos[0] + GO_OFFSET; st.go_pos[1] = pos[1]; st.go_pos[2] = pos[2];
    rt_log("online co-op: teleport started (%s), to %.1f, %.1f, %.1f in area %u/%u", why, st.go_pos[0], st.go_pos[1], st.go_pos[2], area, area2);
}

static void go_step(Cpu *c, uint64_t now) {
    if (!st.go_active || !st.me_ok) return;
    if (now - st.go_ns > GO_TIMEOUT_NS) { st.go_active = 0; rt_log("online co-op: teleport dropped, it did not land in time"); return; }
    float dx = st.go_pos[0] - st.me_pos[0], dz = st.go_pos[2] - st.me_pos[2];
    int here = (st.go_area == st.me_area && st.go_area2 == st.me_area2) || dx * dx + dz * dz < NEAR_BLOCK * NEAR_BLOCK;
    if (here) {
        if (st.go_jumped && now - st.last_jump_ns <= POS_JUMP_GAP_NS) return;   // let the area load first
        st.go_active = 0;
        jump_to_position(c, st.go_area, st.go_area2, st.go_pos, st.me_pos);
    } else if (!st.go_jumped) {
        unsigned row = row_for_area(st.go_area);
        if (!row) { st.go_active = 0; rt_log("online co-op: teleport dropped, no entry is known for area %u", st.go_area); return; }
        st.go_jumped = 1; st.last_jump_ns = now;
        jump_to_row(c, row);
    }
}

static void run_command(unsigned cmd, uint64_t now) {
    switch (cmd) {
    case OC_CMD_GO: case OC_CMD_BRING:
        if (!remote_fresh(now)) { rt_log("online co-op: no other player to ask"); break; }
        send_ctl(cmd == OC_CMD_GO ? COOP_CTL_ASK_GO : COOP_CTL_ASK_BRING);
        st.ask_kind = cmd == OC_CMD_GO ? COOP_CTL_ASK_GO : COOP_CTL_ASK_BRING; st.ask_ns = now;
        rt_log("online co-op: asked the other player to %s", cmd == OC_CMD_GO ? "let you come" : "come to you");
        break;
    case OC_CMD_FORCE_GO:
        if (!remote_fresh(now)) { rt_log("online co-op: no other player to go to"); break; }
        start_go(now, st.remote.area, st.remote.area2, st.remote.pos, "forced, to the other player");
        break;
    case OC_CMD_FORCE_BRING:
        if (!remote_fresh(now)) { rt_log("online co-op: no other player to move"); break; }
        send_ctl(COOP_CTL_FORCE_BRING);
        rt_log("online co-op: moved the other player to you");
        break;
    case OC_CMD_ACCEPT:
        if (!st.prompt_kind) { rt_log("online co-op: no question to answer"); break; }
        if (st.prompt_kind == COOP_CTL_ASK_GO) send_ctl(COOP_CTL_ACCEPT_GO);
        else { start_go(now, st.prompt.area, st.prompt.area2, st.prompt.pos, "accepted, to the other player"); send_ctl(COOP_CTL_ACCEPT_BRING); }
        st.prompt_kind = 0;
        break;
    case OC_CMD_DECLINE:
        if (!st.prompt_kind) { rt_log("online co-op: no question to answer"); break; }
        send_ctl(COOP_CTL_DECLINE);
        st.prompt_kind = 0;
        break;
    }
}

void online_coop_command(unsigned cmd) { __atomic_store_n(&st.cmd, cmd, __ATOMIC_RELEASE); }

static void handle_ctl(const CoopCtl *m, uint64_t now) {
    if (st.have_ctl_seq && (int32_t)(m->seq - st.ctl_last_seq) <= 0) return;   // the second and third copy, or a late one
    st.ctl_last_seq = m->seq; st.have_ctl_seq = 1;
    switch (m->kind) {
    case COOP_CTL_ASK_GO: case COOP_CTL_ASK_BRING:
        st.prompt = *m; st.prompt_kind = m->kind; st.prompt_ns = now;
        rt_log("online co-op: the other player asks %s", m->kind == COOP_CTL_ASK_GO ? "to come to you" : "you to come to them");
        break;
    case COOP_CTL_FORCE_BRING:
        start_go(now, m->area, m->area2, m->pos, "forced by the other player");
        break;
    case COOP_CTL_ACCEPT_GO:
        if (st.ask_kind == COOP_CTL_ASK_GO && now - st.ask_ns < ASK_TIMEOUT_NS) { st.ask_kind = 0; start_go(now, m->area, m->area2, m->pos, "the other player said yes"); }
        break;
    case COOP_CTL_ACCEPT_BRING:
        if (st.ask_kind == COOP_CTL_ASK_BRING) { st.ask_kind = 0; rt_log("online co-op: the other player is coming"); }
        break;
    case COOP_CTL_DECLINE:
        if (st.ask_kind) { st.ask_kind = 0; rt_log("online co-op: the other player said no"); }
        break;
    }
}

// While the countdown runs the float is put back to its start value every pass, so it never reaches zero.
static void hold_countdown(void) {
    uint64_t obj = rt_native_ui_read64(COUNTDOWN_PTR);
    if (!obj || !readable_writable(obj, 0x20) || (uint32_t)LD32(obj + 8) != 3 || getf(obj + 0xc) >= COUNTDOWN_START - 0.1f) return;
    if (!st.timer_holds++) rt_log("online co-op: the death countdown is held");
    putf(obj + 0xc, COUNTDOWN_START);
}

// The battle state of a character node: the node points at its battle object at +0x1e0 and the state is the word at +0x34. At rest it
// reads 2; in a fight run it read 1 from the start of the fight until after the player went down and the party was put back. The game's own
// check for being in a battle (f_70003d8330) answers differently for 1, for 3 and for the rest; 3 has not been seen. Returns 99 when the
// object cannot be read.
#define NODE_BATTLE 0x1e0
#define BATTLE_STATE 0x34
static unsigned battle_state(uint64_t node) {
    uint64_t bp = rt_native_ui_read64(node + NODE_BATTLE);
    return readable_writable(bp, BATTLE_STATE + 4) ? (unsigned)LD32(bp + BATTLE_STATE) : 99u;
}

static void tick(Cpu *c) {
    uint64_t now = kern_now_ns();
    uint64_t nodes[2] = {0, 0};
    unsigned n = character_nodes(nodes);
    uint32_t area = 0, area2 = 0;
    int have_area = readable_writable(AREA_WORD, 8);
    if (have_area) { area = (uint32_t)LD32(AREA_WORD); area2 = (uint32_t)LD32(AREA_WORD2); }

    st.me_ok = n >= 1 && have_area;
    if (st.me_ok) {
        st.me_area = area; st.me_area2 = area2;
        for (int k = 0; k < 3; ++k) st.me_pos[k] = getf(nodes[0] + NODE_POS + 4 * k);
    }
    if (!st.first_tick_ns) st.first_tick_ns = now;

    if (n >= 1 && have_area && now - st.last_send_ns >= SEND_INTERVAL_NS) {
        st.last_send_ns = now;
        CoopPose p = { ++st.seq, area, area2, { getf(nodes[0] + NODE_POS), getf(nodes[0] + NODE_POS + 4), getf(nodes[0] + NODE_POS + 8) }, {0}, battle_state(nodes[0]) == 1 ? COOP_POSE_IN_FIGHT : 0u };
        for (int k = 0; k < 12; ++k) p.rot[k] = getf(nodes[0] + NODE_ROT + 4 * k);
        uint8_t msg[COOP_POSE_BYTES];
        coop_pose_encode(&p, msg);
        if (coop_link_send(msg, sizeof msg)) ++st.sent;
    }

    uint8_t msg[COOP_POSE_BYTES + 1];
    size_t got;
    while ((got = coop_link_recv(msg, sizeof msg)) != 0) {
        CoopCtl cm;
        if (got == COOP_CTL_BYTES) {
            if (coop_ctl_decode(msg, got, &cm)) handle_ctl(&cm, now); else ++st.rejected;
            continue;
        }
        CoopPose p;
        if (!coop_pose_decode(msg, got, &p)) { ++st.rejected; continue; }
        ++st.received;
        // Datagrams can arrive late or twice; an older number than the one shown is dropped.
        if (st.have_remote && (int32_t)(p.seq - st.remote.seq) <= 0) continue;
        st.remote = p; st.remote_ns = now; st.have_remote = 1;
        if (((p.flags & COOP_POSE_IN_FIGHT) != 0) != st.remote_fight) {
            st.remote_fight = (p.flags & COOP_POSE_IN_FIGHT) != 0;
            rt_log("online co-op: the other player %s a fight", st.remote_fight ? "is in" : "is no longer in");
        }
    }

    float here[3] = {0, 0, 0};
    if (n >= 1) for (int k = 0; k < 3; ++k) here[k] = getf(nodes[0] + NODE_POS + 4 * k);
    int same = same_area(have_area, area, area2, here);
    int shown = st.have_remote && now - st.remote_ns < STALE_NS && same && n >= 2;
    if (shown) {
        st.hold_node = nodes[1];
        if (!st.holding) rt_log("online co-op: showing the other player's position");
    } else if (st.holding) {
        rt_log("online co-op: the other player's position is no longer shown (in the same area %d, recent %d, party member %d)",
               same, st.have_remote && now - st.remote_ns < STALE_NS, n >= 2);
    }
    st.holding = shown;
    if (st.hold_timer && st.have_remote && now - st.remote_ns < STALE_NS) hold_countdown();
    {
        unsigned cmd = __atomic_exchange_n(&st.cmd, 0u, __ATOMIC_ACQ_REL);
        if (st.test_cmd && now - st.first_tick_ns >= st.test_at_ns) { cmd = st.test_cmd; st.test_cmd = 0; }
        if (cmd) run_command(cmd, now);
        if (st.ask_kind && now - st.ask_ns > ASK_TIMEOUT_NS) { st.ask_kind = 0; rt_log("online co-op: nobody answered the question"); }
        if (st.prompt_kind && now - st.prompt_ns > ASK_TIMEOUT_NS) { st.prompt_kind = 0; rt_log("online co-op: a question from the other player ran out"); }
        // A question waits while this player is in a fight the other player is not in.
        int can_answer = !(st.battle_state == 1 && !st.remote_fight);
        if (st.prompt_kind && st.auto_accept && can_answer) run_command(OC_CMD_ACCEPT, now);
        go_step(c, now);
    }
    if (st.follow) {
        follow_remote(c, now, st.have_remote && now - st.remote_ns < STALE_NS, have_area, area, area2, here);
    }
    if (n >= 1) {
        unsigned bs = battle_state(nodes[0]);
        if (bs != st.battle_state) {
            float at[3] = { getf(nodes[0] + NODE_POS), getf(nodes[0] + NODE_POS + 4), getf(nodes[0] + NODE_POS + 8) };
            if (bs == 1) memcpy(st.fight_start, at, sizeof at);
            if (st.battle_state == 1) {
                float dx = at[0] - st.fight_start[0], dz = at[2] - st.fight_start[2];
                rt_log("online co-op: the player's battle state changed from %u to %u, %.1f from where the fight began, now at %.1f, %.1f, %.1f", st.battle_state, bs, sqrtf(dx * dx + dz * dz), at[0], at[1], at[2]);
            } else
                rt_log("online co-op: the player's battle state changed from %u to %u, at %.1f, %.1f, %.1f", st.battle_state, bs, at[0], at[1], at[2]);
            st.battle_state = bs;
        }
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
