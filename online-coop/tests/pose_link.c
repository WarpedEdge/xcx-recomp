// The pose message and the UDP link of the online co-op add-on (runtime/online_coop_pose.c, runtime/online_coop_link.c), on loopback.
#include "online_coop_link.h"
#include "online_coop_pose.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_log(const char *fmt, ...) { (void)fmt; }

static size_t wait_recv(void *buf, size_t cap) {
    for (int i = 0; i < 200; ++i) {
        size_t n = coop_link_recv(buf, cap);
        if (n) return n;
        struct timespec ts = { 0, 5000000 };
        nanosleep(&ts, NULL);
    }
    return 0;
}

int main(void) {
    CoopPose p = { 7, 73, 73, { 1.5f, -2.25f, 300.0f }, {0}, COOP_POSE_IN_FIGHT };
    for (int k = 0; k < 12; ++k) p.rot[k] = 0.125f * (float)k;
    uint8_t wire[COOP_POSE_BYTES];
    coop_pose_encode(&p, wire);
    // The layout other builds already send: magic, seq, area, area2, then the floats, each little endian.
    assert(wire[0] == 0x50 && wire[1] == 0x58 && wire[2] == 0x43 && wire[3] == 0x58 && wire[4] == 7 && wire[8] == 73);
    CoopPose q;
    assert(coop_pose_decode(wire, sizeof wire, &q));
    assert(q.seq == 7 && q.area == 73 && q.area2 == 73 && q.pos[0] == 1.5f && q.pos[1] == -2.25f && q.pos[2] == 300.0f && q.rot[11] == 1.375f && q.flags == COOP_POSE_IN_FIGHT);
    // An older build sends 76 bytes without the flags: accepted, flags 0.
    CoopPose old;
    assert(coop_pose_decode(wire, COOP_POSE_BYTES_V1, &old) && old.seq == 7 && old.flags == 0 && old.pos[2] == 300.0f);
    // Anything that is not exactly one good message is refused and leaves the output alone.
    CoopPose keep = q;
    uint8_t bad[COOP_POSE_BYTES + 1]; memcpy(bad, wire, sizeof wire); bad[sizeof wire] = 0;
    assert(!coop_pose_decode(bad, sizeof bad, &q) && !coop_pose_decode(wire, sizeof wire - 1, &q) && !coop_pose_decode(wire, COOP_POSE_BYTES_V1 + 1, &q) && !coop_pose_decode(wire, 0, &q));
    memcpy(bad, wire, sizeof wire); bad[0] ^= 1; assert(!coop_pose_decode(bad, sizeof wire, &q));
    for (int field = 0; field < 15; ++field) {
        float values[] = { NAN, INFINITY, -INFINITY, 100001.0f, -100001.0f };
        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v) {
            CoopPose r = p;
            if (field < 3) r.pos[field] = values[v]; else r.rot[field - 3] = values[v];
            coop_pose_encode(&r, bad);
            assert(!coop_pose_decode(bad, COOP_POSE_BYTES, &q));
        }
    }
    assert(!memcmp(&q, &keep, sizeof q));

    // The control message.
    CoopCtl cm = { 9, COOP_CTL_ASK_BRING, 73, 83, { -2664.0f, -51.1f, 6493.7f } };
    uint8_t cw[COOP_CTL_BYTES];
    coop_ctl_encode(&cm, cw);
    assert(cw[0] == 0x43 && cw[1] == 0x54 && cw[2] == 0x43 && cw[3] == 0x58 && cw[4] == 9 && cw[8] == COOP_CTL_ASK_BRING);
    CoopCtl cq;
    assert(coop_ctl_decode(cw, sizeof cw, &cq) && cq.seq == 9 && cq.kind == COOP_CTL_ASK_BRING && cq.area == 73 && cq.area2 == 83 && cq.pos[0] == -2664.0f && cq.pos[2] == 6493.7f);
    CoopCtl ckeep = cq;
    assert(!coop_ctl_decode(cw, sizeof cw - 1, &cq) && !coop_ctl_decode(wire, sizeof wire, &cq) && !coop_pose_decode(cw, sizeof cw, &q));
    uint8_t cbad[COOP_CTL_BYTES]; memcpy(cbad, cw, sizeof cw);
    cbad[8] = 0; assert(!coop_ctl_decode(cbad, sizeof cbad, &cq));
    cbad[8] = COOP_CTL_KIND_MAX + 1; assert(!coop_ctl_decode(cbad, sizeof cbad, &cq));
    for (int field = 0; field < 3; ++field) {
        float values[] = { NAN, INFINITY, 100001.0f, -100001.0f };
        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v) {
            CoopCtl r = cm; r.pos[field] = values[v];
            coop_ctl_encode(&r, cbad);
            assert(!coop_ctl_decode(cbad, sizeof cbad, &cq));
        }
    }
    assert(!memcmp(&cq, &ckeep, sizeof cq));

    // Two links on loopback: each sends to the other's port.
    assert(!coop_link_open("not an address", 0) && !coop_link_open("127.0.0.1:0", 0) && !coop_link_open("127.0.0.1:47011", 70000));
    uint8_t buf[COOP_POSE_BYTES + 1];
    assert(coop_link_open("127.0.0.1:47012", 47011));
    assert(coop_link_recv(buf, sizeof buf) == 0);   // nothing waiting
    assert(coop_link_send(wire, sizeof wire));      // goes to port 47012, where nobody listens yet
    coop_link_close();
    // The second link is the only one in this process, so a message to itself shows the receive path and the peer filter.
    assert(coop_link_open("127.0.0.1:47011", 47011));
    assert(coop_link_send(wire, sizeof wire));
    size_t n = wait_recv(buf, sizeof buf);
    assert(n == COOP_POSE_BYTES && !memcmp(buf, wire, n));
    uint8_t big[COOP_POSE_BYTES + 5] = {0};
    assert(coop_link_send(big, sizeof big));
    assert(wait_recv(buf, sizeof wire) == 0);       // longer than the buffer: dropped, not cut
    coop_link_close();
    assert(!coop_link_send(wire, sizeof wire));     // closed
    // The pose link of a squad session: the passcode is the host's loopback port, the two sides use the next two ports in turn.
    {
        unsigned lp = 0, pp = 0;
        assert(coop_session_ports(0, "27015", &lp, &pp) && lp == 27016 && pp == 27017);
        assert(coop_session_ports(1, "27015", &lp, &pp) && lp == 27017 && pp == 27016);
        assert(coop_session_ports(0, "1024", &lp, &pp) && lp == 1025 && coop_session_ports(1, "65533", &lp, &pp) && lp == 65535 && pp == 65534);
        assert(!coop_session_ports(0, "1023", &lp, &pp) && !coop_session_ports(0, "65534", &lp, &pp) && !coop_session_ports(0, "", &lp, &pp));
        assert(!coop_session_ports(0, NULL, &lp, &pp) && !coop_session_ports(0, "27 15", &lp, &pp) && !coop_session_ports(0, "123456", &lp, &pp));
        assert(!coop_session_ports(1, "abcde", &lp, &pp) && !coop_session_ports(1, "-2701", &lp, &pp));
    }
    puts("pose and link tests passed");
    return 0;
}
