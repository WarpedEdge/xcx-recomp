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
