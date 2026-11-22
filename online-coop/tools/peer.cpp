#include "xcx_coop/coop.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
static void log_line(void *, const char *s) { std::printf("%s\n", s); std::fflush(stdout); }
int main(int argc, char **argv) {
    if (argc != 5) { std::fprintf(stderr,"usage: coop-peer off|host|client PORT SECONDS MESSAGE_COUNT\n"); return 2; }
    char *end;
    auto port = std::strtoul(argv[2], &end, 10); if (*end || !port || port > 65535) return 2;
    auto seconds = std::strtoul(argv[3], &end, 10); if (*end || !seconds || seconds > 3600) return 2;
    auto count = std::strtoul(argv[4], &end, 10); if (*end || count > 1000000) return 2;
    coop_config config{argv[1], uint16_t(port), "0.0.1", "standalone", "none", log_line, nullptr, 0};
    auto *r = coop_start(&config); if (!r) { std::puts("startup failed"); return 1; }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    unsigned sent = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!std::strcmp(argv[1],"client")) {
            while (sent < count && coop_send_test(r, sent)) ++sent;
            if (count && coop_received(r) == count) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto received = coop_received(r);
    std::printf("received=%llu\n", static_cast<unsigned long long>(received));
    coop_stop(r);
    return !std::strcmp(argv[1],"client") && count && received != count ? 1 : 0;
}
