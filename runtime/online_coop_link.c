#include "online_coop_link.h"
#include <stdio.h>
#include <string.h>

void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   // runtime/host.h; not included, so this file builds on its own in the tests
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define sock_close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define sock_close close
#endif

static sock_t sock = SOCK_BAD;
static struct sockaddr_in peer_addr;
static int have_peer;

static int set_nonblocking(sock_t s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void coop_link_close(void) {
    if (sock != SOCK_BAD) sock_close(sock);
    sock = SOCK_BAD;
    have_peer = 0;
}

int coop_link_open(const char *peer, unsigned port) {
    coop_link_close();
    memset(&peer_addr, 0, sizeof peer_addr);
    if (peer) {
        char host[64]; unsigned peer_port = 0;
        if (sscanf(peer, "%63[^:]:%u", host, &peer_port) != 2 || !peer_port || peer_port > 65535u ||
            inet_pton(AF_INET, host, &peer_addr.sin_addr) != 1) {
            rt_log("online co-op: the peer address is not <ipv4>:<port>");
            return 0;
        }
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons((uint16_t)peer_port);
        have_peer = 1;
    }
    if (port > 65535u) { rt_log("online co-op: the local port is out of range"); return 0; }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) { rt_log("online co-op: the network could not start"); return 0; }
#endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local; memset(&local, 0, sizeof local);
    local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_ANY); local.sin_port = htons((uint16_t)port);
    if (sock == SOCK_BAD || !set_nonblocking(sock) || bind(sock, (struct sockaddr *)&local, sizeof local)) {
        rt_log("online co-op: the UDP socket could not be opened or bound to port %u", port);
        coop_link_close();
        return 0;
    }
    return 1;
}

int coop_link_send(const void *data, size_t n) {
    if (sock == SOCK_BAD || !have_peer) return 0;
    return sendto(sock, (const char *)data, (int)n, 0, (const struct sockaddr *)&peer_addr, sizeof peer_addr) == (int)n;
}

size_t coop_link_recv(void *buf, size_t cap) {
    for (;;) {
        if (sock == SOCK_BAD) return 0;
        struct sockaddr_in from; socklen_t len = sizeof from;
        // One byte more than cap tells a datagram that is too long from one that fits exactly.
        char big[2048];
        if (cap + 1 > sizeof big) return 0;
        int got = recvfrom(sock, big, (int)(cap + 1), 0, (struct sockaddr *)&from, &len);
        if (got < 0) return 0;
        if (have_peer && from.sin_addr.s_addr != peer_addr.sin_addr.s_addr) continue;
        if ((size_t)got > cap) continue;
        memcpy(buf, big, (size_t)got);
        return (size_t)got;
    }
}
