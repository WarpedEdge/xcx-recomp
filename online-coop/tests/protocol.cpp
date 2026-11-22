#include "protocol.h"
#include "xcx_coop/coop.h"
#include <cassert>
#include <random>
int main() {
    auto id = wire::identity("0.0.1", "runtime", "game");
    for (auto type : {wire::hello, wire::welcome, wire::test, wire::echo, wire::goodbye, wire::invite, wire::invite_reply}) {
        std::vector<uint8_t> body = type == wire::hello || type == wire::welcome ? id :
            type == wire::goodbye ? std::vector<uint8_t>{} : std::vector<uint8_t>{0,0,0,42};
        auto frame = wire::frame(type, body);
        for (size_t split = 0; split < frame.size(); ++split) {
            std::vector<uint8_t> in(frame.begin(), frame.begin() + split), out;
            wire::Type got;
            assert(!wire::pop(in, got, out));
            in.insert(in.end(), frame.begin() + split, frame.end());
            assert(wire::pop(in, got, out) && got == type && out == body && in.empty());
        }
    }
    std::mt19937 rng(42);
    for (unsigned i = 0; i < 100000; ++i) {
        std::vector<uint8_t> in(rng() % 300), out;
        for (auto& byte : in) byte = uint8_t(rng());
        wire::Type got;
        try { wire::pop(in, got, out); } catch (const std::runtime_error&) {}
    }
    assert(!coop_start(nullptr));
    coop_config c{"invalid", 27015, "a", "b", "c", nullptr, nullptr, 0};
    assert(!coop_start(&c));
    c.mode = "client"; c.port = 0; assert(!coop_start(&c));
    c.port = 27015; c.mod_id = ""; assert(!coop_start(&c));
    c.mod_id = "bad identity"; assert(!coop_start(&c));
    c.mode = "off";
    for (unsigned i = 0; i < 100; ++i) {
        auto *r = coop_start(&c); assert(r && !coop_connected(r));
        assert(!coop_send_test(r, 1) && !coop_received(r));
        assert(!coop_send_invite(r) && !coop_send_invite_reply(r, 1) && coop_party_state(r) == COOP_PARTY_NONE);
        coop_stop(r);
    }
    coop_stop(nullptr);
}
