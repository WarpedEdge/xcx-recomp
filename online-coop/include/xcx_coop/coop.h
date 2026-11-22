#ifndef XCX_COOP_H
#define XCX_COOP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Calls and callbacks must not access guest memory. Log runs on the network thread.
   Callbacks must return promptly, must not throw, and must not call coop_stop.
   Serialize lifecycle calls and finish other API calls before stopping.
   Stop joins the thread before returning. */
typedef struct coop_runtime coop_runtime;
typedef void (*coop_log_fn)(void *user, const char *message);
typedef struct {
    const char *mode; /* off, host, client */
    uint16_t port; /* loopback only; nonzero */
    const char *mod_id;
    const char *recomp_id;
    const char *game_id;
    coop_log_fn log;
    void *user;
    uint32_t test_interval_ms; /* 0 disables automatic client probes */
} coop_config;
/* Returns NULL on invalid configuration or startup failure. Off creates no socket/thread. */
coop_runtime *coop_start(const coop_config *config);
/* Requests a bounded test message. Returns 0 if disconnected or the queue is full. */
int coop_send_test(coop_runtime *runtime, uint32_t value);
/* Party invite plumbing (stage 1). Either side may invite once connected; the other side answers. */
enum {
    COOP_PARTY_NONE = 0,
    COOP_PARTY_INVITE_SENT,     /* we invited and wait for an answer */
    COOP_PARTY_INVITE_RECEIVED, /* the peer invited us and waits for our answer */
    COOP_PARTY_ACCEPTED,        /* an invite was accepted (by us or by the peer): both are in the party */
    COOP_PARTY_DECLINED         /* the peer declined our invite */
};
/* Queues an invite. Returns 0 if disconnected, the queue is full, or an invite is pending or already accepted. */
int coop_send_invite(coop_runtime *runtime);
/* Answers a received invite (accept nonzero = accept). Returns 0 unless an invite was received. */
int coop_send_invite_reply(coop_runtime *runtime, int accept);
/* One of the COOP_PARTY_* values; NONE when disconnected. */
uint32_t coop_party_state(const coop_runtime *runtime);
int coop_connected(const coop_runtime *runtime);
uint64_t coop_received(const coop_runtime *runtime);
void coop_stop(coop_runtime *runtime);
#ifdef __cplusplus
}
#endif
#endif
