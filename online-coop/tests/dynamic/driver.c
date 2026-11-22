#include "mod_provider_service_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
int main(int argc,char **argv){if(argc!=2)return 2;
#ifdef _WIN32
 HMODULE lib=LoadLibraryExA(argv[1],NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);XmsQuery query=lib?(XmsQuery)GetProcAddress(lib,"xcx_provider_query_v2"):NULL;
#else
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);XmsQuery query=lib?(XmsQuery)dlsym(lib,"xcx_provider_query_v2"):NULL;
#endif
 if(!query)return 3;XmsApi api={0};api.size=sizeof api;if(query(1,0,&api)!=XMP_INCOMPATIBLE||query(2,1,&api)!=XMP_INCOMPATIBLE||query(2,0,&api)!=XMP_OK)return 4;
 XmsInfo info={0};info.size=sizeof info;if(api.describe(&info))return 5;
 XmpCreate c={0};c.size=sizeof c;c.major=2;c.owner=1;c.generation=9;c.required_capabilities=1;strcpy(c.package_id,"coop");uint64_t token=0;if(api.create_idle(&c,&token))return 6;
 printf("ready %s %s\n",info.service_id,info.build_id);fflush(stdout);char line[256];
 while(fgets(line,sizeof line,stdin)){unsigned op;unsigned long long req;long long value;
  if(sscanf(line,"submit %u %llu %lld",&op,&req,&value)==3){XmsCommand cmd={0};cmd.size=sizeof cmd;cmd.opcode=op;cmd.type=(op<3||op==4)?XMS_INTEGER:XMS_NONE;cmd.owner=1;cmd.generation=9;cmd.request=req;cmd.value=value;printf("submit %d\n",api.submit(token,&cmd));}
  else if(!strncmp(line,"invalid",7)){XmsCommand bad={0};bad.size=sizeof bad;bad.opcode=1;bad.type=XMS_INTEGER;bad.owner=2;bad.generation=9;bad.request=999;bad.value=27015;int a=api.submit(token,&bad);bad.owner=1;bad.generation=8;int b=api.submit(token,&bad);bad.generation=9;bad.type=XMS_BOOL;int c=api.submit(token,&bad);bad.type=XMS_INTEGER;bad.value=80;int d=api.submit(token,&bad);printf("invalid %d %d %d %d\n",a,b,c,d);}
  else if(!strncmp(line,"status",6)){XmsStatus s={0};s.size=sizeof s;int r=api.status(token,&s);printf("status %d %s\n",r,s.text);}
  else if(!strncmp(line,"poll",4)){XmsResult r={0};r.size=sizeof r;int got=api.poll(token,&r);printf("poll %d %llu %d %s\n",got,(unsigned long long)r.request,r.result,r.message);}
  else if(!strncmp(line,"stop",4)){printf("stop %d\n",api.request_stop(token));}
  else if(!strncmp(line,"exit",4))break;
  else return 7;fflush(stdout);
 }
 api.request_stop(token);int joined=api.join_destroy(token,4000);printf("closed %d\n",joined);fflush(stdout);return joined?8:0;
}
