// Party invite plumbing: two runtimes in one process exchange invites over loopback.
#include "xcx_coop/coop.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <unistd.h>
using namespace std::chrono;

static bool wait_for(const std::function<bool()>& done, int ms = 3000) {
    auto end = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < end) { if (done()) return true; std::this_thread::sleep_for(milliseconds(5)); }
    return done();
}

struct Pair {
    coop_runtime *host, *client;
    explicit Pair(uint16_t port) {
        coop_config h{"host", port, "0.0.1", "runtime", "game", nullptr, nullptr, 0};
        host = coop_start(&h); assert(host);
        coop_config c{"client", port, "0.0.1", "runtime", "game", nullptr, nullptr, 0};
        client = coop_start(&c); assert(client);
        assert(wait_for([&] { return coop_connected(host) && coop_connected(client); }));
    }
    ~Pair() { coop_stop(client); coop_stop(host); }
};

int main() {
    uint16_t base = uint16_t(41000 + (getpid() % 2000) * 2);

    // Nothing to answer, and nothing to send before a connection.
    {
        coop_config c{"client", base, "0.0.1", "runtime", "game", nullptr, nullptr, 0};
        coop_runtime *r = coop_start(&c);
        if (r) { assert(!coop_send_invite(r) && !coop_send_invite_reply(r, 1)); assert(coop_party_state(r) == COOP_PARTY_NONE); coop_stop(r); }
    }

    // The client invites, the host accepts; a second invite is refused while accepted.
    {
        Pair p(base);
        assert(coop_party_state(p.host) == COOP_PARTY_NONE && coop_party_state(p.client) == COOP_PARTY_NONE);
        assert(!coop_send_invite_reply(p.host, 1));            // no invite yet
        assert(coop_send_invite(p.client));
        assert(coop_party_state(p.client) == COOP_PARTY_INVITE_SENT);
        assert(!coop_send_invite(p.client));                   // already pending
        assert(wait_for([&] { return coop_party_state(p.host) == COOP_PARTY_INVITE_RECEIVED; }));
        assert(coop_send_invite_reply(p.host, 1));
        assert(wait_for([&] { return coop_party_state(p.client) == COOP_PARTY_ACCEPTED && coop_party_state(p.host) == COOP_PARTY_ACCEPTED; }));
        assert(!coop_send_invite(p.host) && !coop_send_invite(p.client)); // already in a party
    }

    // The host invites, the client declines, the host may invite again, the client accepts.
    {
        Pair p(uint16_t(base + 1));
        assert(coop_send_invite(p.host));
        assert(wait_for([&] { return coop_party_state(p.client) == COOP_PARTY_INVITE_RECEIVED; }));
        assert(coop_send_invite_reply(p.client, 0));
        assert(coop_party_state(p.client) == COOP_PARTY_NONE);
        assert(wait_for([&] { return coop_party_state(p.host) == COOP_PARTY_DECLINED; }));
        assert(coop_send_invite(p.host));
        assert(wait_for([&] { return coop_party_state(p.client) == COOP_PARTY_INVITE_RECEIVED; }));
        assert(coop_send_invite_reply(p.client, 1));
        assert(wait_for([&] { return coop_party_state(p.host) == COOP_PARTY_ACCEPTED && coop_party_state(p.client) == COOP_PARTY_ACCEPTED; }));
    }

    // The party state resets when the peer goes away.
    {
        coop_config h{"host", uint16_t(base + 1), "0.0.1", "runtime", "game", nullptr, nullptr, 0};
        coop_runtime *host = coop_start(&h); assert(host);
        {
            coop_config c{"client", uint16_t(base + 1), "0.0.1", "runtime", "game", nullptr, nullptr, 0};
            coop_runtime *client = coop_start(&c); assert(client);
            assert(wait_for([&] { return coop_connected(host) && coop_connected(client); }));
            assert(coop_send_invite(client));
            assert(wait_for([&] { return coop_party_state(host) == COOP_PARTY_INVITE_RECEIVED; }));
            coop_stop(client);
        }
        assert(wait_for([&] { return !coop_connected(host) && coop_party_state(host) == COOP_PARTY_NONE; }));
        coop_stop(host);
    }
    std::puts("party tests passed");
}
