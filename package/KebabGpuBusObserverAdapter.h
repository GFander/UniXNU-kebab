/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GPU_BUS_OBSERVER_ADAPTER_H
#define KEBAB_GPU_BUS_OBSERVER_ADAPTER_H
#include "KebabNativeGpuSmmu.h"
#if defined(KEBAB_BUS_HOST)
#include "KebabGpuBusOwner.h"
#else
#include <kern/KebabGpuBusOwner.h>
#endif
/* Adapter for the sealed GPU SMMU native observer. No active namespace/reset
 * grant exists here; active requests are closed before touching the bus owner.
 * One fixed instance may serve the bounded observer/prepare-discovery domains. */
struct KebabGpuBusObserverSlot {KGLease *lease;KebabGpuBusToken token;};
struct KebabGpuBusObserver {uint32_t busy,last_native_error;KebabGpuBusObserverSlot slot[4];};
static int KebabGpuBusObserverAcquire(void *cookie,uint32_t role,uint32_t active,KGLease *l)
{
 KebabGpuBusObserver *a=(KebabGpuBusObserver *)cookie;
 if(!a || !l || active || role>2)return KG_CLOSED;
 if(!__sync_bool_compare_and_swap(&a->busy,0,1))return KG_BUSY;
 unsigned i;for(i=0;i<4;i++)if(!a->slot[i].lease)break;
 if(i==4){__sync_lock_release(&a->busy);return KG_BUSY;}
 int e=kebab_rpmh_gpu_bus_acquire(&a->slot[i].token);a->last_native_error=e;
 if(!e){memset(l,0,sizeof *l);l->held=1;l->generation=a->slot[i].token.generation;l->flags=KG_ACCESS;l->role=role;
  l->cb=255;l->smr=255;l->cb_read_mask=a->slot[i].token.read_cb_mask;a->slot[i].lease=l;}
 __sync_lock_release(&a->busy);return e?KG_CLOSED:KG_OK;
}
static int KebabGpuBusObserverCheck(void *cookie,const KGLease *l,uint32_t need)
{
 KebabGpuBusObserver *a=(KebabGpuBusObserver *)cookie;
 if(!a || !l || (need&~KG_ACCESS) || l->flags!=KG_ACCESS || !l->held)return KG_CLOSED;
 if(!__sync_bool_compare_and_swap(&a->busy,0,1))return KG_BUSY;
 unsigned i;for(i=0;i<4;i++)if(a->slot[i].lease==l)break;int e=22;
 if(i<4 && l->generation==a->slot[i].token.generation && l->cb_read_mask==a->slot[i].token.read_cb_mask)e=kebab_rpmh_gpu_bus_check(&a->slot[i].token);
 a->last_native_error=e;__sync_lock_release(&a->busy);return e?KG_CLOSED:KG_OK;
}
static int KebabGpuBusObserverQuiesce(void *,KGLease *){return KG_CLOSED;}
static int KebabGpuBusObserverEnd(void *cookie,KGLease *l)
{
 KebabGpuBusObserver *a=(KebabGpuBusObserver *)cookie;if(!a || !l)return KG_INVALID;
 if(!__sync_bool_compare_and_swap(&a->busy,0,1))return KG_BUSY;
 unsigned i;for(i=0;i<4;i++)if(a->slot[i].lease==l)break;int e=22;
 if(i<4)e=kebab_rpmh_gpu_bus_end(&a->slot[i].token);
 if(!e){a->slot[i].lease=NULL;l->held=0;}a->last_native_error=e;
 __sync_lock_release(&a->busy);return e?KG_CLOSED:KG_OK;
}
static KGNativeOwner KebabGpuBusObserverOwner(KebabGpuBusObserver *a)
{KGNativeOwner o={a,KebabGpuBusObserverAcquire,KebabGpuBusObserverCheck,KebabGpuBusObserverQuiesce,KebabGpuBusObserverEnd};return o;}
#endif
