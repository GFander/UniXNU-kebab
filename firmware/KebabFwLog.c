/** @file
  Kebab firmware log ring buffer helper (shared by the serial library and the
  reservation driver).  No allocation, no UEFI services: just memory writes to
  a fixed physical page.
**/
#include <Uefi.h>
#include "KebabFwLog.h"

STATIC KEBAB_FWLOG *mLog = (KEBAB_FWLOG *)(UINTN)KEBAB_FWLOG_PA;

/**
  Append bytes to the firmware log.  Does nothing until KebabFwLogDxe has
  reserved the page and set the magic.
**/
VOID
KebabFwLogAppend (
  IN CONST UINT8  *Buffer,
  IN UINTN        Length
  )
{
  UINT64  Head;
  UINTN   Index;

  if (Buffer == NULL || Length == 0) {
    return;
  }
  if (mLog->Magic != KEBAB_FWLOG_MAGIC) {
    return;
  }

  Head = __atomic_load_n (&mLog->Head, __ATOMIC_RELAXED);
  for (Index = 0; Index < Length; Index++) {
    mLog->Data[(Head + Index) % KEBAB_FWLOG_DATA_SIZE] = Buffer[Index];
  }
  __atomic_store_n (&mLog->Head, Head + Length, __ATOMIC_RELEASE);
}
