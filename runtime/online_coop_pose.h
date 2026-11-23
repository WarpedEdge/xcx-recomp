// The pose message the online co-op add-on sends between two games: where the player stands and which way they face.
// 80 bytes, little endian, one UDP datagram (an older build sends 76 bytes, without the flags, and is still accepted). The numbers come from another machine, so decode checks them before the game uses them.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define COOP_POSE_MAGIC 0x58435850u
#define COOP_POSE_BYTES 80
#define COOP_POSE_BYTES_V1 76
#define COOP_POSE_IN_FIGHT 1u   // flags bit 0: the player is in a fight (battle state 1)

typedef struct {
    uint32_t seq;     // counts up with every message sent, so a late or repeated datagram can be told from a new one
    uint32_t area;    // the game's two area words (0x7001df5944 and 0x7001df5948); both must match before a pose is shown
    uint32_t area2;
    float pos[3];     // the character node's position
    float rot[12];    // the node's rotation rows (node +0x10 to +0x3c)
    uint32_t flags;   // COOP_POSE_IN_FIGHT; unknown bits are carried as they come
} CoopPose;

void coop_pose_encode(const CoopPose *p, uint8_t out[COOP_POSE_BYTES]);
// 1 when n is COOP_POSE_BYTES or COOP_POSE_BYTES_V1 (flags 0), the magic matches and every float is finite and below 100000 in size; else 0 and *p is left alone.
int coop_pose_decode(const uint8_t *in, size_t n, CoopPose *p);

// The control message: one player asks the other to teleport, or answers. 32 bytes, little endian, one UDP datagram. The position and
// area words are the sender's, so the receiver needs no pose to act on it.
#define COOP_CTL_MAGIC 0x58435443u
#define COOP_CTL_BYTES 32
enum {
    COOP_CTL_ASK_GO = 1,       // the sender asks to come to the receiver
    COOP_CTL_ASK_BRING = 2,    // the sender asks the receiver to come to the sender
    COOP_CTL_FORCE_BRING = 3,  // the receiver is moved to the sender with no question
    COOP_CTL_ACCEPT_GO = 4,    // answer to ASK_GO: the sender of ASK_GO may come (the position is the answerer's)
    COOP_CTL_ACCEPT_BRING = 5, // answer to ASK_BRING: the answerer is on the way
    COOP_CTL_DECLINE = 6,      // answer to either question
    COOP_CTL_KIND_MAX = 6
};
typedef struct {
    uint32_t seq;      // counts up with every control message; the same number is sent three times, only the first is acted on
    uint32_t kind;
    uint32_t area, area2;
    float pos[3];
} CoopCtl;
void coop_ctl_encode(const CoopCtl *m, uint8_t out[COOP_CTL_BYTES]);
// 1 when n is exactly COOP_CTL_BYTES, the magic matches, the kind is known and the position is finite and below 100000 in size; else 0.
int coop_ctl_decode(const uint8_t *in, size_t n, CoopCtl *m);

// The pose link of a squad session. In the first stage a squad's passcode is the loopback port P of its host (1024 to 65533); the host
// listens on P + 1 and sends to P + 2, the joined player the other way round, both on 127.0.0.1. joined is 0 for the player who created
// the squad and 1 for the one who joined. 1 and the two ports when the passcode is plain digits and in range; else 0.
int coop_session_ports(int joined, const char *code, unsigned *local_port, unsigned *peer_port);

