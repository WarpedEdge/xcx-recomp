// Reads the player's pose from the game's memory (research note 13). Read only: nothing here writes to the game.
//
// Guest addresses are host pointers and the module base is 0x7000000000, so the provider (loaded in the game process) can read
// the game's memory directly. The read goes through process_vm_readv on the provider's own process, which returns an error
// for an unmapped address instead of crashing.
//
// Layout [E, one run, note 13]: a global array at module base + 0x1dc1860 has 24-byte entries (node pointer, function
// address, number); the two filled entries (index 2 and 3, at module base + 0x1dc1890 and + 0x1dc18a8) are the characters, and
// their number equals the index [I: only two entries were seen]. Entries 0 and 1 hold other data and fail the checks. A node holds three
// rotation rows at +0x10..+0x3f (the row at +0x30 is the facing direction) and the position at +0x40 as x, y, z, 1.0 (y up).
// The index of the filled entries is not fixed: 2 and 3 in the field after one start, 4 to 6 in the barracks after loading a second
// save, 7 after leaving the barracks (companions not yet shown). [I] The first listed entry is the player and the later ones are
// companions: the entry that moved when the owner moved was the first one in all three loads. The array is empty while an area loads.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__linux__)
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace xcx_pose {
constexpr uint64_t array_base=0x7001dc1860ull;  // entry i is at array_base + 24 * i
constexpr unsigned entries=32;                   // entries 0..31 are looked at; the filled index moved from 2 and 3 to 4 to 6 to 7 over three loads
constexpr uint64_t heap_low=0x300000000ull,heap_high=0x700000000ull;

struct Actor{
 bool ok=false;
 unsigned entry=0;
 uint64_t node=0;
 float x=0,y=0,z=0;   // position, y is up
 float fx=0,fz=0;     // facing direction on the ground
};
// The area number [E, one run, note 13]: three words in the module's data hold the same number while the player is in one place:
// 1 in the barracks, 29 and 80 at two places in the field. They are accepted only if all three agree.
constexpr uint64_t area_words[3]={0x7001df5944ull,0x7001df5948ull,0x7001f97e48ull};
struct Pose{Actor actor[4];unsigned count=0;bool area_ok=false;uint32_t area=0;};

// Reads n bytes at a guest address; returns false if they cannot all be read.
using ReadFn=bool(*)(void *context,uint64_t address,void *out,size_t n);

inline bool read_self(void *,uint64_t address,void *out,size_t n){
#if defined(__linux__)
 iovec local{out,n},remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address)),n};
 return process_vm_readv(getpid(),&local,1,&remote,1,0)==static_cast<ssize_t>(n);
#else
 (void)address;(void)out;(void)n;return false;
#endif
}

// A node is accepted only if its rows look like a pose: finite values, a unit facing row, the fourth float of the position row 1.
inline bool read_actor(ReadFn read,void *context,uint64_t node,unsigned entry,Actor &out){
 float f[8];
 if(node<heap_low||node>=heap_high||!read(context,node+0x30,f,sizeof f))return false;
 for(float v:f)if(!std::isfinite(v))return false;
 const float len=f[0]*f[0]+f[1]*f[1]+f[2]*f[2];
 if(len<0.9f||len>1.1f||f[3]!=0.0f||f[7]!=1.0f)return false;
 for(int i=4;i<7;++i)if(std::fabs(f[i])>1.0e6f)return false;
 out.ok=true;out.entry=entry;out.node=node;out.fx=f[0];out.fz=f[2];out.x=f[4];out.y=f[5];out.z=f[6];
 return true;
}

// Fills pose with the characters found, in entry order. Returns true if at least one was found.
inline bool read_pose(ReadFn read,void *context,Pose &pose){
 pose=Pose{};
 uint32_t w[3];
 if(read(context,area_words[0],&w[0],4)&&read(context,area_words[1],&w[1],4)&&read(context,area_words[2],&w[2],4)&&w[0]==w[1]&&w[1]==w[2]){pose.area_ok=true;pose.area=w[0];}
 uint64_t e[entries*3];
 if(!read(context,array_base,e,sizeof e))return false;
 for(unsigned i=0;i<entries&&pose.count<4;++i){
  Actor a;
  if(e[i*3]&&e[i*3+2]==i&&read_actor(read,context,e[i*3],i,a))pose.actor[pose.count++]=a;
 }
 return pose.count>0;
}
inline bool read_pose(Pose &pose){return read_pose(read_self,nullptr,pose);}
}
