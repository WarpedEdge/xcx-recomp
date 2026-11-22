#include "xcx_coop/coop.h"
#include "protocol.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
static void close_socket(Socket s) { closesocket(s); }
static bool pending() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static bool nonblocking(Socket s) { u_long v = 1; return ioctlsocket(s, FIONBIO, &v) == 0; }
static Socket open_socket() { return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); }
static Socket accept_socket(Socket s) { return accept(s, nullptr, nullptr); }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket invalid = -1;
static void close_socket(Socket s) { close(s); }
static bool pending() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR; }
static bool nonblocking(Socket s) { return fcntl(s, F_SETFL, O_NONBLOCK) == 0; }
// The runtime starts shader translators concurrently; they must never inherit session sockets.
static Socket open_socket() { return socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP); }
static Socket accept_socket(Socket s) { return accept4(s, nullptr, nullptr, SOCK_CLOEXEC); }
#endif
using Clock = std::chrono::steady_clock;
struct coop_runtime {
    Socket listener = invalid, peer = invalid;
    bool host = false, sockets = false, connecting = false;
    uint32_t interval = 0, probe = 0;
    Clock::time_point next_probe = Clock::now();
    std::atomic<bool> stop{false}, connected{false};
    std::atomic<uint64_t> received{0};
    std::thread worker;
    std::mutex mutex;
    std::deque<uint32_t> queue;
    // Party invite plumbing: invite frames waiting to be sent (under the mutex), the state, and a counter for invite numbers.
    std::deque<std::pair<wire::Type, uint32_t>> control;
    std::atomic<uint32_t> party{COOP_PARTY_NONE};
    uint32_t invite_number = 0;
    std::vector<uint8_t> id, input, output;
    coop_log_fn logger = nullptr;
    void *user = nullptr;
    Clock::time_point progress = Clock::now();
    void log(const char *s) { if (logger) logger(user, s); }
    void drop(const char *reason) {
        connected = false;
        if (peer != invalid) close_socket(peer);
        peer = invalid; input.clear(); output.clear();
        { std::lock_guard<std::mutex> lock(mutex); queue.clear(); control.clear(); party = COOP_PARTY_NONE; }
        log(reason);
    }
    ~coop_runtime() {
        if (worker.joinable()) { stop = true; worker.join(); }
        if (peer != invalid) close_socket(peer);
        if (listener != invalid) close_socket(listener);
#ifdef _WIN32
        if (sockets) WSACleanup();
#endif
    }
    void append(wire::Type type, const std::vector<uint8_t>& body = {}) {
        auto b = wire::frame(type, body);
        if (output.size() + b.size() > 65536) throw std::runtime_error("outbound limit reached");
        output.insert(output.end(), b.begin(), b.end());
    }
    void run() {
        log(host ? "hosting on loopback" : "connecting on loopback");
        while (!stop) {
            try {
                if (peer == invalid && host) {
                    Socket s = accept_socket(listener);
                    if (s != invalid) {
                        if (!nonblocking(s)) { close_socket(s); continue; }
                        peer = s; progress = Clock::now(); log("peer accepted; waiting for handshake");
                    }
                }
                if (peer != invalid && connecting) {
                    fd_set writable, errors;
                    FD_ZERO(&writable); FD_ZERO(&errors);
                    FD_SET(peer, &writable); FD_SET(peer, &errors);
                    timeval wait{};
                    int ready = select(int(peer + 1), nullptr, &writable, &errors, &wait);
                    if (ready < 0) throw std::runtime_error("connect readiness failed");
                    if (!ready) {
                        if (Clock::now() - progress > std::chrono::seconds(5))
                            throw std::runtime_error("connect timeout");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        continue;
                    }
                    int error = 0;
#ifdef _WIN32
                    int size = sizeof error;
#else
                    socklen_t size = sizeof error;
#endif
                    if (getsockopt(peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) || error)
                        throw std::runtime_error("connect failed");
                    connecting = false;
                }
                if (peer != invalid) {
                    uint8_t buf[2048];
                    int n = recv(peer, reinterpret_cast<char *>(buf), sizeof buf, 0);
                    if (n == 0) { drop("peer disconnected"); if (!host) break; continue; }
                    if (n < 0 && !pending()) { drop("receive failed"); if (!host) break; continue; }
                    if (n > 0) {
                        input.insert(input.end(), buf, buf + n);
                        wire::Type type; std::vector<uint8_t> body;
                        while (wire::pop(input, type, body)) {
                            if (!connected) {
                                if (type != (host ? wire::hello : wire::welcome) || body != id)
                                    throw std::runtime_error("handshake/build mismatch");
                                if (host) append(wire::welcome, id);
                                connected = true; log("handshake accepted");
                            } else if (type == wire::goodbye) {
                                drop("peer disconnected cleanly"); break;
                            } else if (type == wire::test || type == wire::echo) {
                                if ((host && type != wire::test) || (!host && type != wire::echo))
                                    throw std::runtime_error("unexpected test message");
                                auto count = ++received;
                                if (count == 1 || count % 1000 == 0)
                                    log(host ? "test message received" : "test echo received");
                                if (host) append(wire::echo, body);
                            } else if (type == wire::invite || type == wire::invite_reply) {
                                uint32_t value = wire::read(body.data());
                                std::lock_guard<std::mutex> lock(mutex);
                                if (type == wire::invite) {
                                    if (party == COOP_PARTY_NONE || party == COOP_PARTY_DECLINED) {
                                        party = COOP_PARTY_INVITE_RECEIVED; log("party invite received");
                                    } else log("party invite ignored (one is already pending or accepted)");
                                } else {
                                    if (value != wire::accept && value != wire::decline) throw std::runtime_error("invalid invite reply");
                                    if (party == COOP_PARTY_INVITE_SENT) {
                                        party = value == wire::accept ? COOP_PARTY_ACCEPTED : COOP_PARTY_DECLINED;
                                        log(value == wire::accept ? "party invite accepted" : "party invite declined");
                                    } else log("party invite reply ignored (no invite was sent)");
                                }
                            } else throw std::runtime_error("unexpected control message");
                        }
                    }
                    if (peer == invalid) { if (!host) break; continue; }
                    if (connected && !host && interval && Clock::now() >= next_probe) {
                        std::vector<uint8_t> b; wire::put(b, probe++); append(wire::test, b);
                        next_probe = Clock::now() + std::chrono::milliseconds(interval);
                    }
                    if (connected) {
                        std::lock_guard<std::mutex> lock(mutex);
                        while (!queue.empty() && output.size() < 32768) {
                            std::vector<uint8_t> b; wire::put(b, queue.front()); queue.pop_front();
                            append(wire::test, b);
                        }
                        while (!control.empty() && output.size() < 32768) {
                            std::vector<uint8_t> b; wire::put(b, control.front().second);
                            append(control.front().first, b); control.pop_front();
                        }
                    }
                    if (!output.empty()) {
#ifdef _WIN32
                        constexpr int flags = 0;
#else
                        constexpr int flags = MSG_NOSIGNAL;
#endif
                        int sent = send(peer, reinterpret_cast<const char *>(output.data()), int(output.size()), flags);
                        if (sent > 0) { output.erase(output.begin(), output.begin() + sent); progress = Clock::now(); }
                        else if (sent < 0 && !pending()) throw std::runtime_error("send failed");
                    }
                    if ((!connected || !output.empty()) && Clock::now() - progress > std::chrono::seconds(5))
                        throw std::runtime_error("handshake/send timeout");
                }
            } catch (const std::exception& e) { drop(e.what()); if (!host) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (peer != invalid) {
            auto b = wire::frame(wire::goodbye);
            // Only send a close frame between complete frames; shutdown never waits for a peer.
            if (output.empty()) {
#ifdef _WIN32
                send(peer, reinterpret_cast<const char *>(b.data()), int(b.size()), 0);
#else
                send(peer, b.data(), b.size(), MSG_NOSIGNAL);
#endif
            }
            drop("session stopped");
        }
        log("network worker stopped");
    }
};
extern "C" coop_runtime *coop_start(const coop_config *c) {
    if (!c || !c->mode) return nullptr;
    try {
        auto r = std::make_unique<coop_runtime>();
        r->logger = c->log; r->user = c->user; r->interval = c->test_interval_ms;
        if (!std::strcmp(c->mode, "off")) return r.release();
        r->host = !std::strcmp(c->mode, "host");
        if ((!r->host && std::strcmp(c->mode,"client")) || !c->port) return nullptr;
        r->id = wire::identity(c->mod_id, c->recomp_id, c->game_id);
#ifdef _WIN32
        WSADATA w;
        if (WSAStartup(MAKEWORD(2,2), &w)) return nullptr;
        r->sockets = true;
#endif
        Socket s = open_socket();
        if (s == invalid) return nullptr;
        if (r->host) r->listener = s; else r->peer = s;
        if (!nonblocking(s)) return nullptr;
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_port = htons(c->port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (r->host) {
#ifndef _WIN32
            int reuse = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif
            if (bind(s, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(s, 1)) return nullptr;
        } else {
            if (connect(s, reinterpret_cast<sockaddr *>(&address), sizeof address) && !pending()) return nullptr;
            r->connecting = true;
            r->append(wire::hello, r->id);
        }
        r->worker = std::thread([p = r.get()] { p->run(); });
        return r.release();
    } catch (...) { return nullptr; }
}
extern "C" int coop_send_test(coop_runtime *r, uint32_t value) {
    if (!r || r->host || !r->connected) return 0;
    std::lock_guard<std::mutex> lock(r->mutex);
    if (!r->connected || r->queue.size() >= 1024) return 0;
    try { r->queue.push_back(value); return 1; } catch (...) { return 0; }
}
extern "C" int coop_send_invite(coop_runtime *r) {
    if (!r || !r->connected) return 0;
    std::lock_guard<std::mutex> lock(r->mutex);
    if (!r->connected || r->control.size() >= 16) return 0;
    uint32_t state = r->party;
    if (state != COOP_PARTY_NONE && state != COOP_PARTY_DECLINED) return 0;
    try { r->control.push_back({wire::invite, ++r->invite_number}); r->party = COOP_PARTY_INVITE_SENT; return 1; } catch (...) { return 0; }
}
extern "C" int coop_send_invite_reply(coop_runtime *r, int accept) {
    if (!r || !r->connected) return 0;
    std::lock_guard<std::mutex> lock(r->mutex);
    if (!r->connected || r->control.size() >= 16 || r->party != COOP_PARTY_INVITE_RECEIVED) return 0;
    try {
        r->control.push_back({wire::invite_reply, accept ? uint32_t(wire::accept) : uint32_t(wire::decline)});
        r->party = accept ? COOP_PARTY_ACCEPTED : COOP_PARTY_NONE; return 1;
    } catch (...) { return 0; }
}
extern "C" uint32_t coop_party_state(const coop_runtime *r) { return r ? static_cast<uint32_t>(r->party.load()) : static_cast<uint32_t>(COOP_PARTY_NONE); }
extern "C" int coop_connected(const coop_runtime *r) { return r && r->connected; }
extern "C" uint64_t coop_received(const coop_runtime *r) { return r ? r->received.load() : 0; }
extern "C" void coop_stop(coop_runtime *r) { delete r; }
