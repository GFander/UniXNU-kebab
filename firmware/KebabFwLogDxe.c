/** @file
  Kebab firmware log reservation.

  Reserves KEBAB_FWLOG_PA as EfiReservedMemoryType and marks it with the KFLG
  magic, so the framebuffer serial port library may start appending and the
  kernel can find the buffer at a fixed physical address.

  This is read/write memory only: no RPMh/RSC access, no protocol calls.
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <KebabFwLog.h>

EFI_STATUS
EFIAPI
KebabFwLogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;
  KEBAB_FWLOG           *Log;

  Address = KEBAB_FWLOG_PA;
  Status  = gBS->AllocatePages (AllocateAddress, EfiReservedMemoryType,
                  KEBAB_FWLOG_PAGES, &Address);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "[KFLG] reserve %lx failed: %r\n", (UINT64)KEBAB_FWLOG_PA, Status));
    return EFI_SUCCESS;
  }

  Log = (KEBAB_FWLOG *)(UINTN)Address;
  Log->Magic = 0;
  Log->Size  = KEBAB_FWLOG_DATA_SIZE;
  Log->Head  = 0;
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
  Log->Magic = KEBAB_FWLOG_MAGIC;

  DEBUG ((DEBUG_ERROR,
    "[KFLG] reserved %lx size %x, log starts here\n",
    (UINT64)Address, KEBAB_FWLOG_DATA_SIZE));
  return EFI_SUCCESS;
}
