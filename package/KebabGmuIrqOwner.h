/* SPDX-License-Identifier: MIT */
#ifndef KEBAB_GMU_IRQ_OWNER_H
#define KEBAB_GMU_IRQ_OWNER_H
#include <stdint.h>
#if defined(KERNEL) || defined(KEBAB_RPMH_KERNEL)
#include <kern/KebabGpuColdOwner.h>
#else
#include "KebabGpuColdOwner.h"
#endif
#include "KebabGmuIrqCore.h"
/* Stable address required: copying these bytes does not copy the lease.
 * First version pins through boot, including all error/quarantine paths. */
typedef struct {
 const KebabGpuColdToken *cold;
 uint64_t epoch,generation;
 uint32_t held,reserved;
} KebabGmuIrqToken;
typedef struct {
 uint32_t magic,schema,bytes,result,phase,reason,installed,armed;
 uint32_t intid_hfi,intid_ao,gic_ctlr,gic_typer,enable,pending,active,config,priorities;
 uint32_t route_low[2],route_high[2],hfi_status,ao_status,hfi_mask,ao_mask;
 uint32_t touched,reads,writes,dispatches,overflow,pending_events,handler_observed,firmware_started,dma_proven;
 uint32_t valid_mask,mmio_reads,mmio_writes,containment_ack;
 uint64_t epoch,generation;
} KebabGmuIrqRecord;
#ifdef __cplusplus
extern "C" {
#endif
/* Ordinary worker, IRQs enabled, outside canonical VRM/SCM locks. */
int kebab_gpu_gmu_irq_acquire(const KebabGpuColdToken *,uint64_t,KebabGmuIrqToken *);
int kebab_gpu_gmu_irq_mask(const KebabGmuIrqToken *);
int kebab_gpu_gmu_irq_take(const KebabGmuIrqToken *,KebabGmuIrqEvent *);
int kebab_gpu_gmu_irq_snapshot(KebabGmuIrqRecord *);
/* Used by launch owner already holding canonical globalbusy. Never claims
 * that lock or invokes ordinary-worker operations; fresh finite MMIO. */
int kebab_gpu_gmu_irq_verify_locked(const KebabGmuIrqToken *,const KebabGpuColdToken *,uint64_t);
/* Pure atomic/private check and namespace epoch check, safe each FW poll. */
int kebab_gpu_gmu_irq_healthy(const KebabGmuIrqToken *);
/* pe_fiq.c local GICD owner; sleh alone reads IAR and writes EOI. */
int kebab_gpu_gmu_irq_dispatch(uint32_t);
/* Bridge functions live in the same canonical VRM TU after BootOwner.inc.
 * The first call claims globalbusy; CheckLocked must never claim it again. */
int kebab_gpu_gmu_irq_parent_pin(const KebabGpuColdToken *,uint64_t,KebabGmuIrqToken *);
int kebab_gpu_gmu_irq_parent_check_locked(const KebabGmuIrqToken *,const KebabGpuColdToken *,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
