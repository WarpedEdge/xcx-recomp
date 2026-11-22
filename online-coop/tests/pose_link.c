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
    CoopPose p = { 7, 73, 73, { 1.5f, -2.25f, 300.0f }, {0} };
    for (int k = 0; k < 12; ++k) p.rot[k] = 0.125f * (float)k;
    uint8_t wire[COOP_POSE_BYTES];
    coop_pose_encode(&p, wire);
    // The layout other builds already send: magic, seq, area, area2, then the floats, each little endian.
    assert(wire[0] == 0x50 && wire[1] == 0x58 && wire[2] == 0x43 && wire[3] == 0x58 && wire[4] == 7 && wire[8] == 73);
    CoopPose q;
    assert(coop_pose_decode(wire, sizeof wire, &q));
    assert(q.seq == 7 && q.area == 73 && q.area2 == 73 && q.pos[0] == 1.5f && q.pos[1] == -2.25f && q.pos[2] == 300.0f && q.rot[11] == 1.375f);
    // Anything that is not exactly one good message is refused and leaves the output alone.
    CoopPose keep = q;
    uint8_t bad[COOP_POSE_BYTES + 1]; memcpy(bad, wire, sizeof wire); bad[sizeof wire] = 0;
    assert(!coop_pose_decode(bad, sizeof bad, &q) && !coop_pose_decode(wire, sizeof wire - 1, &q) && !coop_pose_decode(wire, 0, &q));
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
    puts("pose and link tests passed");
    return 0;
}
