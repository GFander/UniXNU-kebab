/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_NATIVE_GPU_BUS_HOLD_H
#define KEBAB_NATIVE_GPU_BUS_HOLD_H
#include <sys/errno.h>
#include "KebabNativeGpuBus.h"
/* GPUBUS explicit normal worker only. Native prepare already owns the
 * one-shot state and persistent312B receipt, including refused attempts. */
static int kebab_gpu_bus_worker_check(void *cookie)
{
 int e=kebab_gpu_gcc_worker_access(cookie);if(e)return e;
 if(ml_at_interrupt_context() || get_preemption_level() || !ml_get_interrupts_enabled())return 95;
 return 0;
}
extern "C" void kebab_gpu_bus_prepare(void)
{
 uint32_t before=__atomic_load_n(&kebab_native_gpu_bus_state,__ATOMIC_ACQUIRE);
 int result=kebab_native_gpu_bus_prepare(NULL,kebab_gpu_bus_worker_check);
 uint32_t state=__atomic_load_n(&kebab_native_gpu_bus_state,__ATOMIC_ACQUIRE);
 printf("KEBAB GPUBUS: end; result=%d; state=%u; cached=%u; RO bus only\n",result,state,before!=0);
}
/* CAP22 allocates/copies only, never map/prepare/check live clocks. */
static int kebab_native_gpu_bus_hold_snapshot(KebabGpuBusRecord *out)
{
 if(!out)return EINVAL;
 if(__atomic_load_n(&kebab_native_gpu_bus_state,__ATOMIC_ACQUIRE)!=2)return EAGAIN;
 return kebab_native_gpu_bus_snapshot(out);
}
#endif
