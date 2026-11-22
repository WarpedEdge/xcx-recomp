// Loopback transport adapter. Control owns coop_start/coop_stop; ABI calls only copy bounded data.
#include "mod_provider_service_abi.h"
#include "xcx_coop/coop.h"
#include "game_pose.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#ifndef COOP_PROVIDER_BUILD
#define COOP_PROVIDER_BUILD "loopback1"
#endif
static constexpr const char *service="xcx.coop.loopback.v1";
static constexpr const char *host_identity="xcx-mods-service-2.0";
static constexpr const char *game_identity="xcx-de-2.0.0";
using Clock=std::chrono::steady_clock;
// The fourth command, "probe", is the provider's general send while the service interface allows only four commands. Its
// integer is a message code. Code 1 is the original test message; the others are the party invite messages (stage 1).
// TODO: give this command a clear name (send) and replace it with a real message call when the service interface grows.
enum {PROBE_TEST=1,PROBE_INVITE=2,PROBE_ACCEPT=3,PROBE_DECLINE=4};
static const char *party_name(uint32_t s){
 switch(s){case COOP_PARTY_INVITE_SENT:return "invite-sent";case COOP_PARTY_INVITE_RECEIVED:return "invite-received";
  case COOP_PARTY_ACCEPTED:return "in-party";case COOP_PARTY_DECLINED:return "invite-declined";default:return "none";}
}
struct Instance {
 XmpCreate config{};std::mutex lock;std::condition_variable wake;std::thread control;
 std::atomic<bool> stopping{false},done{false};
 XmsCommand commands[32]{};unsigned head=0,count=0,credits=0;
 XmsResult results[32]{};unsigned rh=0,rc=0;uint64_t last_request=0;
 XmsStatus latest{};char network[129]="";bool ended=false;coop_runtime *runtime=nullptr;bool hosting=false;
 // The player's pose, read from the game's memory about four times a second (control thread only). The status text carries it
 // while connected; if XCX_COOP_POSE_LOG names a file, every change of more than half a unit is also appended there (a
 // diagnostic for checking the read in the game, off by default).
 xcx_pose::Pose pose;bool pose_ok=false;Clock::time_point pose_next{},pose_logged{};xcx_pose::Actor pose_last;uint32_t area_last=0;
 void sample_pose() {
  auto now=Clock::now();if(now<pose_next)return;pose_next=now+std::chrono::milliseconds(250);
  xcx_pose::Pose p;const bool ok=xcx_pose::read_pose(p);pose=p;
  const char *path=std::getenv("XCX_COOP_POSE_LOG");
  if(path&&*path){
   const auto &a=p.actor[0];
   const bool moved=ok&&(!pose_ok||std::fabs(a.x-pose_last.x)+std::fabs(a.y-pose_last.y)+std::fabs(a.z-pose_last.z)>0.5f||p.area!=area_last);
   if(ok!=pose_ok||moved||(ok&&now-pose_logged>std::chrono::seconds(5))){
    if(FILE *f=std::fopen(path,"a")){
     if(!ok)std::fprintf(f,"pose unavailable\n");
     else{for(unsigned i=0;i<p.count;++i){const auto &c=p.actor[i];
       std::fprintf(f,"%sentry %u node 0x%llx pos %.2f %.2f %.2f facing %.2f %.2f",i?" | ":"",c.entry,(unsigned long long)c.node,c.x,c.y,c.z,c.fx,c.fz);}
      if(p.area_ok)std::fprintf(f," | area %u",p.area);
      std::fprintf(f,"\n");}
     std::fclose(f);
    }
    pose_logged=now;if(ok){pose_last=a;area_last=p.area;}
   }
  }
  pose_ok=ok;
 }
 void publish(const char *state,const char *reason="") {
  char text[257];std::snprintf(text,sizeof text,"%s | build=%s | %.128s",state,COOP_PROVIDER_BUILD,reason);
  std::lock_guard<std::mutex> g(lock);if(std::strcmp(latest.text,text)){std::snprintf(latest.text,sizeof latest.text,"%s",text);++latest.revision;}
 }
 static void log(void *user,const char *text) {
  auto *p=static_cast<Instance*>(user);std::lock_guard<std::mutex> g(p->lock);
  if(!std::strcmp(text,"network worker stopped"))p->ended=true;
  else std::snprintf(p->network,sizeof p->network,"%.128s",text);
 }
 void finish(const XmsCommand &c,int result,const char *message) {
  std::lock_guard<std::mutex> g(lock);auto &r=results[(rh+rc)%32];r={};r.size=sizeof r;r.owner=config.owner;r.generation=config.generation;r.request=c.request;r.result=result;std::snprintf(r.message,sizeof r.message,"%.256s",message);++rc;
 }
 void cleanup(){if(runtime){coop_stop(runtime);runtime=nullptr;}}
 void observe() {
  sample_pose();
  if(!runtime)return;char reason[129];bool stopped;
  {std::lock_guard<std::mutex> g(lock);std::snprintf(reason,sizeof reason,"%s",network);stopped=ended;}
  if(coop_connected(runtime)){
   char at[64]="";if(pose_ok)std::snprintf(at,sizeof at,"area=%u; pose=%.1f,%.1f,%.1f; ",pose.area_ok?pose.area:0u,pose.actor[0].x,pose.actor[0].y,pose.actor[0].z);
   char text[129];std::snprintf(text,sizeof text,"%s; %s=%llu; %sparty=%s",hosting?"host":"client",hosting?"test received":"echo received",(unsigned long long)coop_received(runtime),at,party_name(coop_party_state(runtime)));publish("Transport connected",text);
  } else if(stopped&&!hosting){cleanup();publish("Failed",*reason?reason:"peer disconnected");}
  else publish(hosting?"Hosting/listening":"Joining/handshaking",reason);
 }
 void run() noexcept {
  XmsCommand active{};bool executing=false;
  try {
   while(!stopping){
    XmsCommand c{};bool have=false;
    {std::unique_lock<std::mutex> g(lock);if(!count)wake.wait_for(g,std::chrono::milliseconds(5),[&]{return stopping||count;});if(stopping)break;if(count){c=commands[head];head=(head+1)%32;--count;have=true;}}
    if(have){active=c;executing=true;
     if(c.opcode==3){publish("Leaving/stopping");cleanup();publish("Idle/offline");finish(c,XMP_OK,"Session stopped; all network resources closed");}
     else if(c.opcode==4){
      int ok=0;const char *yes="",*no="";
      switch(c.value){
       case PROBE_TEST:ok=runtime&&coop_send_test(runtime,1);yes="Test queued";no="No connected client session";break;
       case PROBE_INVITE:ok=runtime&&coop_send_invite(runtime);yes="Invite queued";no="No connected session, or an invite is already pending or accepted";break;
       case PROBE_ACCEPT:ok=runtime&&coop_send_invite_reply(runtime,1);yes="Accept queued";no="No invite to accept";break;
       case PROBE_DECLINE:ok=runtime&&coop_send_invite_reply(runtime,0);yes="Decline queued";no="No invite to decline";break;
       default:no="Unknown message code";
      }
      finish(c,ok?XMP_OK:XMP_INVALID,ok?yes:no);
     }
     else if(runtime)finish(c,XMP_INVALID,"Session already active; leave first");
     else {
      hosting=c.opcode==1;{std::lock_guard<std::mutex> g(lock);ended=false;network[0]=0;}
      publish(hosting?"Hosting/listening":"Joining/handshaking","starting loopback transport");
      coop_config cfg{hosting?"host":"client",(uint16_t)c.value,"0.0.1",host_identity,game_identity,log,this,250};runtime=coop_start(&cfg);
      if(!runtime){publish("Failed","Transport startup failed (port conflict or connection refusal)");finish(c,XMP_FAILED,"Transport startup failed");}
      else if(hosting){publish("Hosting/listening","127.0.0.1 listener ready");finish(c,XMP_OK,"Listening on loopback; not gameplay ready");}
      else {
       auto deadline=Clock::now()+std::chrono::seconds(6);bool ended_now=false;
       while(!stopping&&!coop_connected(runtime)&&Clock::now()<deadline){
        {std::lock_guard<std::mutex> g(lock);ended_now=ended;}if(ended_now)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));
       }
       if(!stopping&&coop_connected(runtime)){finish(c,XMP_OK,"Handshake accepted; transport connected only");observe();}
       else{char reason[129];{std::lock_guard<std::mutex> g(lock);std::snprintf(reason,sizeof reason,"%s",network);}cleanup();publish(stopping?"Leaving/stopping":"Failed",*reason?reason:"Handshake timeout or stopped");finish(c,XMP_FAILED,"Join failed or stopped before handshake");}
      }
     }
    }
    executing=false;observe();
   }
  }catch(...){if(executing)finish(active,XMP_FAILED,"Provider control failure");publish("Failed","Provider control failure");stopping=true;}
  cleanup();
  // Every accepted command owns a terminal slot, including cancellation at shutdown.
  {std::lock_guard<std::mutex> g(lock);while(count){auto &c=commands[head];auto &r=results[(rh+rc)%32];r={};r.size=sizeof r;r.owner=config.owner;r.generation=config.generation;r.request=c.request;r.result=XMP_FAILED;std::strcpy(r.message,"Provider stopped");++rc;head=(head+1)%32;--count;}}
  done=true;
 }
};
static Instance *instance;static uint64_t token,next_token;
static XmpResult XMP_CALL describe(XmsInfo *out){if(!out||out->size!=sizeof *out)return XMP_INVALID;*out={};out->size=sizeof *out;out->major=2;out->platform=XMP_PLATFORM;out->architecture=XMP_ARCH_X64;out->capabilities=1;out->command_count=4;std::strcpy(out->provider_id,"coop.native");std::strcpy(out->build_id,COOP_PROVIDER_BUILD);std::strcpy(out->service_id,service);
 const char *ids[]={"host","join","leave","probe"};for(unsigned i=0;i<4;++i){auto &c=out->commands[i];c.opcode=i+1;c.type=(i<2||i==3)?XMS_INTEGER:XMS_NONE;c.minimum=i<2?1024:i==3?PROBE_TEST:0;c.maximum=i<2?65535:i==3?PROBE_DECLINE:0;std::strcpy(c.id,ids[i]);}return XMP_OK;}
static bool valid(uint64_t t){return instance&&t==token;}
static XmpResult XMP_CALL create(const XmpCreate *c,uint64_t *out){if(!out)return XMP_INVALID;*out=0;if(!c||c->size!=sizeof *c||c->major!=2||c->minor||c->reserved||!c->owner||!c->generation||(!std::memchr(c->package_id,0,sizeof c->package_id)||std::strcmp(c->package_id,"coop"))||(c->required_capabilities&~1ull)||instance||next_token==UINT64_MAX)return XMP_INVALID;for(auto p:c->padding)if(p)return XMP_INVALID;
 Instance *p=nullptr;try{p=new Instance;p->config=*c;p->latest.size=sizeof p->latest;p->latest.owner=c->owner;p->latest.generation=c->generation;p->publish("Idle/offline");p->control=std::thread([p]{p->run();});instance=p;token=++next_token;*out=token;return XMP_OK;}catch(...){delete p;return XMP_FAILED;}}
static XmpResult XMP_CALL stop(uint64_t t){if(!valid(t))return XMP_INVALID;instance->stopping=true;instance->wake.notify_all();return XMP_OK;}
static XmpResult XMP_CALL join(uint64_t t,uint32_t timeout){if(!valid(t))return XMP_INVALID;auto until=Clock::now()+std::chrono::milliseconds(timeout);while(!instance->done){if(Clock::now()>=until)return XMP_BUSY;std::this_thread::sleep_for(std::chrono::milliseconds(1));}instance->control.join();delete instance;instance=nullptr;token=0;return XMP_OK;}
static XmpResult XMP_CALL submit(uint64_t t,const XmsCommand *c){if(!valid(t)||!c||c->size!=sizeof *c||c->reserved||c->owner!=instance->config.owner||c->generation!=instance->config.generation||!c->request||c->opcode<1||c->opcode>4||c->type!=((c->opcode<3||c->opcode==4)?XMS_INTEGER:XMS_NONE)||(c->opcode<3?(c->value<1024||c->value>65535):c->opcode==4?(c->value<PROBE_TEST||c->value>PROBE_DECLINE):c->value!=0))return XMP_INVALID;
 std::lock_guard<std::mutex> g(instance->lock);if(instance->stopping)return XMP_FAILED;if(c->request<=instance->last_request)return XMP_INVALID;if(instance->credits==32)return XMP_BUSY;instance->commands[(instance->head+instance->count)%32]=*c;++instance->count;++instance->credits;instance->last_request=c->request;instance->wake.notify_all();return XMP_OK;}
static XmpResult XMP_CALL poll(uint64_t t,XmsResult *out){if(!valid(t)||!out||out->size!=sizeof *out)return XMP_INVALID;std::lock_guard<std::mutex> g(instance->lock);if(!instance->rc)return XMS_EMPTY;*out=instance->results[instance->rh];instance->rh=(instance->rh+1)%32;--instance->rc;--instance->credits;return XMP_OK;}
static XmpResult XMP_CALL status(uint64_t t,XmsStatus *out){if(!valid(t)||!out||out->size!=sizeof *out)return XMP_INVALID;std::lock_guard<std::mutex> g(instance->lock);*out=instance->latest;return XMP_OK;}
extern "C" XMP_EXPORT XmpResult XMP_CALL xcx_provider_query_v2(uint32_t major,uint32_t minor,XmsApi *out){if(!out||out->size!=sizeof *out||major!=2||minor)return XMP_INCOMPATIBLE;*out={};out->size=sizeof *out;out->major=2;out->describe=describe;out->create_idle=create;out->request_stop=stop;out->join_destroy=join;out->submit=submit;out->poll=poll;out->status=status;return XMP_OK;}
