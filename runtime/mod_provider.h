#pragma once
#include "mod_internal.h"
#include "mod_provider_abi.h"
typedef struct ModProviders ModProviders;
typedef struct {uint64_t generation,owner;uint32_t slot,reserved;} ModProviderToken;
// One serialized control domain: mod worker (or standalone caller). Never UI/network threads.
ModProviders *mod_providers_start(const ModPlan *plan,ModLog log);
int mod_providers_token(ModProviders *p,uint64_t owner,ModProviderToken *out);
int mod_providers_stop_owner(ModProviders *p,uint64_t owner,ModProviderToken token);
int mod_providers_stop(ModProviders *p);
void mod_providers_free(ModProviders *p); // Only after stop succeeds; mapped modules stay resident.
