// The UDP link between two games for the online co-op add-on. One non-blocking socket both sends to the peer and listens, so a
// datagram arrives from the address it was sent to. Nothing is opened until coop_link_open is called.
#pragma once
#include <stddef.h>

// peer is "<ipv4 address>:<port>" or NULL (listen only); port is the local UDP port to listen on, or 0 to let the system choose.
// 1 on success, 0 on failure (the reason goes to the log). Calling it again closes the earlier link first.
int coop_link_open(const char *peer, unsigned port);
// 1 when the datagram was handed to the system, 0 otherwise (no peer, would block, error).
int coop_link_send(const void *data, size_t n);
// The next waiting datagram from the peer's address (any address when there is no peer) into buf; its size, or 0 when none is waiting.
// A datagram longer than cap is dropped.
size_t coop_link_recv(void *buf, size_t cap);
void coop_link_close(void);
