// Linux test adapter for the real lifecycle coordinator; no game/runtime services.
#define _GNU_SOURCE
#include "platform/platform.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
struct PlatThread { pthread_t thread; PlatThreadFn fn; void *arg; };
static void *entry(void *p) { PlatThread *t=p; t->fn(t->arg); return NULL; }
PlatThread *plat_thread_new(PlatThreadFn fn,void *arg,const char *name) { (void)name; PlatThread *t=malloc(sizeof *t); if (!t) return NULL; t->fn=fn; t->arg=arg; if (pthread_create(&t->thread,NULL,entry,t)) { free(t); return NULL; } return t; }
void plat_thread_join(PlatThread *t) { pthread_join(t->thread,NULL); free(t); }
uint32_t plat_thread_id(void) { return (uint32_t)syscall(SYS_gettid); }
// Test static storage avoids adding unrelated allocation behavior to lock setup.
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
void plat_mutex_lock(PlatMutex *m) { (void)m; pthread_mutex_lock(&mutex); }
void plat_mutex_unlock(PlatMutex *m) { (void)m; pthread_mutex_unlock(&mutex); }
void plat_cond_wait(PlatCond *c,PlatMutex *m) { (void)c; (void)m; pthread_cond_wait(&condition,&mutex); }
void plat_cond_broadcast(PlatCond *c) { (void)c; pthread_cond_broadcast(&condition); }
void plat_sleep_ms(uint32_t ms) { struct timespec t={ms/1000,(long)(ms%1000)*1000000}; nanosleep(&t,NULL); }
int plat_exe_path(char *out,size_t cap) { (void)out; (void)cap; return 0; }
