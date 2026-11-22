// Pose reader tests on a fake memory (no game needed): the checks that decide whether a node is a pose.
#include "game_pose.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
using namespace xcx_pose;
static std::map<uint64_t,std::vector<unsigned char>> mem;
static bool fake(void *,uint64_t a,void *out,size_t n){
 for(auto &kv:mem)if(a>=kv.first&&a+n<=kv.first+kv.second.size()){std::memcpy(out,kv.second.data()+(a-kv.first),n);return true;}
 return false;
}
static void check(bool ok,const char *what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static void put_node(uint64_t node,float x,float y,float z,float fx,float fz,float w=1.0f){
 float f[8]={fx,0,fz,0,x,y,z,w};
 auto &v=mem[node+0x30];v.resize(sizeof f);std::memcpy(v.data(),f,sizeof f);
}
static void put_array(uint64_t e2,uint64_t e3){
 uint64_t e[entries*3]={};
 e[0]=0x7000019d08ull;e[1]=0x7001c21b08ull;e[2]=0x7001c21b28ull;  // entry 0 holds module addresses, not a node
 e[6]=e2;e[7]=0x70001f270cull;e[8]=2;e[9]=e3;e[10]=0x70001f270cull;e[11]=3;
 auto &v=mem[array_base];v.resize(sizeof e);std::memcpy(v.data(),e,sizeof e);
}
int main(){
 Pose p;
 check(!read_pose(fake,nullptr,p),"unmapped array reads as unavailable");
 put_array(0x34011e810ull,0x343671510ull);
 put_node(0x34011e810ull,-4435.36f,-11.86f,7027.10f,0.861f,-0.509f);
 put_node(0x343671510ull,-4436.19f,-11.78f,7029.05f,0.039f,-0.999f);
 {unsigned v[1]={29};for(uint64_t ad:area_words){auto &m=mem[ad];m.resize(4);std::memcpy(m.data(),v,4);}}
 check(read_pose(fake,nullptr,p),"two nodes are read");
 check(p.area_ok&&p.area==29,"the area number is read when the three words agree");
 check(p.count==2&&p.actor[0].entry==2&&p.actor[1].entry==3,"entries 2 and 3, in order");
 check(p.actor[0].x==-4435.36f&&p.actor[0].y==-11.86f&&p.actor[0].z==7027.10f,"player position");
 check(p.actor[0].fx==0.861f&&p.actor[0].fz==-0.509f,"player facing");
 put_node(0x343671510ull,0,0,0,0.2f,0.2f);
 check(read_pose(fake,nullptr,p)&&p.count==1,"a node whose facing is not a unit vector is refused");
 put_node(0x343671510ull,1,2,3,1,0,0.0f);
 check(read_pose(fake,nullptr,p)&&p.count==1,"a node whose fourth position float is not 1 is refused");
 put_node(0x34011e810ull,std::nanf(""),0,0,1,0);
 check(!read_pose(fake,nullptr,p),"non-finite values are refused");
 {unsigned v[1]={80};auto &m=mem[area_words[2]];std::memcpy(m.data(),v,4);}
 read_pose(fake,nullptr,p);
 check(!p.area_ok,"the area number is refused when the three words disagree");
 put_array(0x1234ull,0);
 check(!read_pose(fake,nullptr,p),"a pointer outside the heap range is refused");
 put_array(0x34011e810ull,0);
 put_node(0x34011e810ull,5,6,7,1,0);
 {auto &v=mem[array_base];uint64_t e[entries*3]={};std::memcpy(e,v.data(),sizeof(uint64_t)*12);v.assign(sizeof e,0);std::memcpy(v.data(),e,sizeof e);
  uint64_t moved[3]={0x34011e810ull,0x70001f270cull,9};  // a later entry whose number matches its index
  std::memcpy(v.data()+9*3*8,moved,sizeof moved);std::memset(v.data()+6*8,0,3*8);}  // entry 2 cleared
 check(read_pose(fake,nullptr,p)&&p.count==1&&p.actor[0].entry==9,"an entry far from the first ones is found");
 {auto &v=mem[array_base];uint64_t wrong=4;std::memcpy(v.data()+9*3*8+16,&wrong,8);}
 check(!read_pose(fake,nullptr,p),"an entry whose number differs from its index is refused");
 std::puts("pose tests passed");
 return 0;
}
