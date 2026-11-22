#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace wire {
constexpr unsigned version = 1, header = 12, max_body = 192;
// invite and invite_reply are the party invite messages (either side may send them once the handshake is done).
// An invite's body is an invite number; a reply's body is accept or decline.
enum Type { hello = 1, welcome, test, echo, goodbye, invite, invite_reply };
enum Reply : uint32_t { accept = 1, decline = 2 };
inline uint32_t read(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void put(std::vector<uint8_t>& b, uint32_t n) {
    b.push_back(uint8_t(n >> 24)); b.push_back(uint8_t(n >> 16));
    b.push_back(uint8_t(n >> 8)); b.push_back(uint8_t(n));
}
inline std::vector<uint8_t> frame(Type t, const std::vector<uint8_t>& body = {}) {
    std::vector<uint8_t> b{'X','C','X','C',0,version,0,uint8_t(t)};
    put(b, uint32_t(body.size())); b.insert(b.end(), body.begin(), body.end()); return b;
}
inline std::vector<uint8_t> identity(const char *mod, const char *recomp, const char *game) {
    std::vector<uint8_t> b;
    for (const char *s : {mod, recomp, game}) {
        if (!s || !*s || std::strlen(s) > 63) throw std::runtime_error("invalid build identity");
        for (const char *p = s; *p; ++p)
            if (*p < 33 || *p > 126) throw std::runtime_error("invalid build identity");
        size_t at = b.size(); b.resize(at + 64, 0); std::memcpy(b.data() + at, s, std::strlen(s));
    }
    return b;
}
/* A TCP read may contain part of a frame or several frames. Never allocate from its length. */
inline bool pop(std::vector<uint8_t>& input, Type& type, std::vector<uint8_t>& body) {
    if (input.size() < header) return false;
    const auto *p = input.data();
    if (std::memcmp(p,"XCXC",4) || p[4] || p[5] != version || p[6] || p[7] < hello || p[7] > invite_reply)
        throw std::runtime_error("incompatible or malformed header");
    auto n = read(p + 8);
    auto t = Type(p[7]);
    unsigned expected = t == hello || t == welcome ? 192 : t == goodbye ? 0 : 4;
    if (n != expected) throw std::runtime_error("invalid message length");
    if (input.size() < header + n) return false;
    type = t; body.assign(input.begin() + header, input.begin() + header + n);
    input.erase(input.begin(), input.begin() + header + n); return true;
}
}
